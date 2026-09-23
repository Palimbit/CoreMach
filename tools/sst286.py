#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Daniel Balsom
# Copyright (c) 2026 BluMach contributors
# MOO decoding adapted after consulting SingleStepTests/80286 tools/moo2json.py
# at 37c73caf53dcd22d3dd369ff09305d13d117a4fe. Bounded parser, transport,
# validation and reporting added for BluMach. No CPU implementation copied.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
"""Offline functional hardware comparison. No downloads, timing or CPU emulation.

Exit 0: selected supported cases pass (pending cases remain explicitly counted).
Exit 1: discrepancy, 2: evidence/transport error, 77: no comparisons passed.
--require-complete also returns 1 if any non-revoked selected case is pending.
"""
import argparse
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

REGS = 'ax bx cx dx cs ss ds es sp bp si di ip flags'.split()
LIMIT = 256 * 1024 * 1024
LOCK = Path(__file__).resolve().parent.parent / 'tests/data/sst286-lock.json'


def chunks(data):
    offset = 0
    while offset < len(data):
        if len(data) - offset < 8:
            raise ValueError('truncated chunk header')
        tag, size = struct.unpack_from('<4sI', data, offset)
        offset += 8
        if size > len(data) - offset:
            raise ValueError('chunk exceeds container')
        yield tag, data[offset:offset + size]
        offset += size


def unique_chunks(data):
    result = {}
    for tag, payload in chunks(data):
        if tag in result:
            raise ValueError(f'duplicate chunk {tag!r}')
        result[tag] = payload
    return result


def counted_bytes(data):
    if len(data) < 4:
        raise ValueError('missing byte count')
    count, = struct.unpack_from('<I', data)
    if len(data) != count + 4:
        raise ValueError('invalid byte count')
    return bytes(data[4:])


def registers(data):
    if len(data) < 2:
        raise ValueError('missing register mask')
    mask, = struct.unpack_from('<H', data)
    if mask >> 14 or len(data) != 2 + 2 * mask.bit_count():
        raise ValueError('invalid register data')
    values = iter(struct.unpack_from('<' + 'H' * mask.bit_count(), data, 2))
    return {name: next(values) for i, name in enumerate(REGS) if mask & (1 << i)}


def cpu_state(data):
    parts = unique_chunks(data)
    if b'RG32' in parts or b'RM32' in parts:
        raise ValueError('not a 16-bit state')
    regs = registers(parts[b'REGS'])
    ram = parts[b'RAM ']
    if len(ram) < 4:
        raise ValueError('missing RAM count')
    count, = struct.unpack_from('<I', ram)
    if count > 65536 or len(ram) != 4 + count * 5:
        raise ValueError('invalid RAM count')
    memory = {}
    for address, value in struct.iter_unpack('<IB', ram[4:]):
        if address >= 0x1000000 or address in memory:
            raise ValueError('invalid/duplicate RAM address')
        memory[address] = value
    if b'QUEU' in parts and counted_bytes(parts[b'QUEU']):
        raise ValueError('prefilled queue not supported')
    return {'regs': regs, 'ram': memory,
            'masks': registers(parts[b'RMSK']) if b'RMSK' in parts else {}}


def parse_moo(data):
    top = iter(chunks(memoryview(data)))
    tag, header = next(top)
    if tag != b'MOO ' or len(header) != 12 or bytes(header[:2]) != b'\x01\x01':
        raise ValueError('expected MOO 1.1 header')
    if bytes(header[8:12]) != b'C286':
        raise ValueError('expected Harris C286 corpus')
    count, = struct.unpack_from('<I', header, 4)
    tests, masks, indices, hashes = [], {}, set(), set()
    for tag, payload in top:
        if tag == b'RMSK':
            masks = registers(payload)
        elif tag == b'META':
            if len(payload) < 28 or payload[27] != 0:
                raise ValueError('only real mode supported')
        elif tag == b'TEST':
            if len(payload) < 4:
                raise ValueError('truncated TEST')
            index, = struct.unpack_from('<I', payload)
            p = unique_chunks(payload[4:])
            ident = bytes(p[b'HASH']).hex()
            if len(ident) != 40 or index in indices or ident in hashes:
                raise ValueError('invalid/duplicate test identity')
            indices.add(index); hashes.add(ident)
            initial, final = cpu_state(p[b'INIT']), cpu_state(p[b'FINA'])
            if set(initial['regs']) != set(REGS):
                raise ValueError('incomplete initial registers')
            code = counted_bytes(p[b'BYTS'])
            if not code:
                raise ValueError('empty instruction')
            exception = bytes(p[b'EXCP']) if b'EXCP' in p else None
            if exception is not None and len(exception) != 5:
                raise ValueError('invalid exception record')
            tests.append({'idx': index, 'hash': ident, 'code': code,
                          'initial': initial, 'final': final,
                          'name': counted_bytes(p[b'NAME']).decode('utf-8'),
                          'exception': exception})
    if len(tests) != count or not tests:
        raise ValueError('header/test count mismatch or empty corpus')
    return tests, masks


def pending_reason(test):
    if test['exception'] is not None:
        return f"guest-exception-{test['exception'][0]}"
    code = test['code']
    i = 0
    while i < len(code) and code[i] in (0x26, 0x2e, 0x36, 0x3e):
        i += 1
    if i == len(code):
        raise ValueError('prefix-only instruction')
    if code[i] in (0xf0, 0xf2, 0xf3):
        return 'lock-or-repeat-prefix'
    # Deliberately bounded first corpus selection, not a full ISA classifier.
    if code[i] == 0xff:
        if i + 1 >= len(code):
            raise ValueError('missing Group 5 ModR/M')
        if ((code[i + 1] >> 3) & 7) == 5 and code[i + 1] >> 6 != 3:
            return None
    if code[i] not in (0x00, 0x01, 0x31, 0x54, 0x60, 0x61, 0x8b, 0x90, 0xc8, 0xc9, 0xe8, 0xea):
        return 'instruction-outside-initial-scope'
    return None


def packet(test):
    s = test['initial']
    return (struct.pack('<14HI', *(s['regs'][r] for r in REGS), len(s['ram'])) +
            b''.join(struct.pack('<IB', a, v) for a, v in s['ram'].items()))


def replies(data, count):
    offset, result = 0, []
    for _ in range(count):
        if len(data) - offset < 36:
            raise ValueError('truncated probe response')
        status, *fields = struct.unpack_from('<i14HI', data, offset)
        offset += 36
        n = fields.pop()
        if n > 65536 or len(data) - offset < n * 5:
            raise ValueError('invalid probe write count')
        writes = dict(struct.iter_unpack('<IB', data[offset:offset + n * 5]))
        if any(a >= 0x1000000 for a in writes):
            raise ValueError('invalid probe address')
        offset += n * 5
        result.append((status, dict(zip(REGS, fields)), writes))
    if offset != len(data):
        raise ValueError('trailing probe output')
    return result


def differences(test, result, masks):
    status, actual, writes = result
    if status:
        return [f'probe status {status}: unsupported here is a discrepancy, not a pass']
    expected = dict(test['initial']['regs'])
    expected.update(test['final']['regs'])
    # The capture's final state includes its injected terminating HLT. We
    # compare ONE instruction, not HLT execution, NMI collection or bus cycles.
    expected['ip'] = (expected['ip'] - 1) & 0xffff
    result = []
    for name in REGS:
        mask = masks.get(name, 0xffff) & test['final']['masks'].get(name, 0xffff)
        if (actual[name] ^ expected[name]) & mask:
            result.append(f'{name}: expected {expected[name]:04x}, got {actual[name]:04x}, mask {mask:04x}')
    memory = dict(test['initial']['ram'])
    memory.update(test['final']['ram'])
    for address in memory.keys() | writes.keys():
        got = writes.get(address, test['initial']['ram'].get(address))
        if got != memory.get(address) or address not in memory:
            result.append(f'RAM {address:06x}: expected {memory.get(address)}, got {got}')
    return result


def verified_inputs(root, lock):
    root = root.resolve()
    files = lock['test_files']
    if not files or len(set(files)) != len(files):
        raise ValueError('empty/duplicate corpus selection')
    required = set(files) | {'v1_real_mode/metadata.json', 'revocation_list.txt'}
    if not required <= lock['sha256'].keys():
        raise ValueError('unhashed corpus/control input')
    for relative, digest in lock['sha256'].items():
        path = (root / relative).resolve()
        if root not in path.parents or not path.is_file():
            raise ValueError(f'missing/outside input: {relative}')
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError(f'input hash mismatch: {relative}')
    return root


def opcode_metadata(metadata, key):
    opcode, separator, group = key.partition('.')
    entry = metadata['opcodes'][opcode]
    if separator:
        subentry = entry['reg'][group]
        combined = dict(entry)
        combined.update(subentry)
        combined['flags-mask'] = entry.get('flags-mask', 0xffff) & subentry.get('flags-mask', 0xffff)
        return combined
    return entry


def run(root, probe, lock, limit=0):
    root = verified_inputs(root, lock)
    revocations = set()
    for line in (root / 'revocation_list.txt').read_text().splitlines():
        text = line.split('#', 1)[0].strip()
        if text:
            if len(text) != 40 or any(c not in '0123456789abcdef' for c in text):
                raise ValueError('invalid revocation hash')
            revocations.add(text)
    metadata = json.loads((root / 'v1_real_mode/metadata.json').read_text())
    report = {'schema': 'blumach-sst286-report-v1', 'corpus_commit': lock['commit'],
              'input_sha256': lock['sha256'],
              'adapter_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'probe_sha256': hashlib.sha256(probe.read_bytes()).hexdigest(),
              'scope': 'functional-one-instruction; no HLT, timing, protected mode or physical-bus certification',
              'files': {}, 'counts': {}, 'details': []}
    total = Counter()
    for filename in lock['test_files']:
        with gzip.open(root / filename, 'rb') as stream:
            data = stream.read(LIMIT + 1)
        if len(data) > LIMIT:
            raise ValueError('uncompressed corpus exceeds bound')
        tests, masks = parse_moo(data)
        key = Path(filename).name.removesuffix('.MOO.gz')
        opmeta = opcode_metadata(metadata, key)
        masks['flags'] = masks.get('flags', 0xffff) & opmeta.get('flags-mask', 0xffff)
        counts = Counter(available=len(tests), not_selected=0, passed=0, failed=0, pending=0, revoked=0)
        if limit:
            counts['not_selected'] = max(0, len(tests) - limit)
            tests = tests[:limit]
        selected = []
        for test in tests:
            reason = 'revoked' if test['hash'] in revocations else pending_reason(test)
            if reason:
                category = 'revoked' if reason == 'revoked' else 'pending'
                counts[category] += 1
                report['details'].append({'file': filename, 'idx': test['idx'],
                                          'hash': test['hash'], 'category': category, 'reason': reason})
            else:
                selected.append(test)
        if selected:
            completed = subprocess.run([str(probe)], input=b''.join(map(packet, selected)),
                                       capture_output=True, timeout=120, check=True)
            for test, reply in zip(selected, replies(completed.stdout, len(selected))):
                diff = differences(test, reply, masks)
                counts['failed' if diff else 'passed'] += 1
                if diff:
                    report['details'].append({'file': filename, 'idx': test['idx'], 'hash': test['hash'],
                                              'category': 'failed', 'name': test['name'], 'differences': diff})
        report['files'][filename] = dict(counts)
        total.update(counts)
        print(filename, dict(counts), flush=True)
    report['counts'] = dict(total)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True, help='offline pinned SST checkout/cache')
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--lock', type=Path, default=LOCK)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--limit', type=int, default=0, help='first N per file; remainder reported untested')
    parser.add_argument('--require-complete', action='store_true')
    args = parser.parse_args()
    try:
        if args.limit < 0:
            raise ValueError('negative limit')
        report = run(args.root, args.probe.resolve(), json.loads(args.lock.read_text()), args.limit)
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(report['counts']))
        counts = report['counts']
        if counts['failed'] or (args.require_complete and (counts['pending'] or counts['not_selected'])):
            return 1
        return 0 if counts['passed'] else 77
    except (ValueError, KeyError, OSError, struct.error, StopIteration, subprocess.SubprocessError) as error:
        print(f'SST286 evidence/transport error: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())

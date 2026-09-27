#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 BluMach contributors
"""Offline capture inventory or narrowly scoped ADD data-access comparison.

Uses the separately attributed MIT MOO decoder in sst286.py. Inventory returns77;
--compare-add-data returns0 only for data address/order matches,1 for mismatches.
Neither mode validates elapsed timing. No downloading or firmware execution.
"""
import argparse
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path
import sys
import struct
import subprocess

try:
    from . import sst286
except ImportError:
    import sst286

STATUS = {0: 'IRQA', 4: 'HALT', 5: 'MEMR', 6: 'MEMW',
          9: 'IOR', 10: 'IOW', 13: 'CODE'}


def data_replies(data, count):
    """Bounded private probe protocol; not a timestamped physical-bus trace."""
    result, offset = [], 0
    for _ in range(count):
        if len(data) - offset < 8:
            raise ValueError('truncated data trace header')
        status, n = struct.unpack_from('<iI', data, offset)
        offset += 8
        if n > 65536 or len(data) - offset < n * 5:
            raise ValueError('invalid data trace count')
        ops = list(struct.iter_unpack('<IB', data[offset:offset + n * 5]))
        if any(a >= 0x1000000 or w > 1 for a, w in ops):
            raise ValueError('invalid data trace transaction')
        result.append((status, [(a, 'MEMW' if w else 'MEMR') for a, w in ops]))
        offset += n * 5
    if offset != len(data):
        raise ValueError('trailing data trace output')
    return result


def compare_add(root, lock, probe):
    """Only successful unprefixed ADD r/m16,r16 memory cases, not timing."""
    filename = 'v1_real_mode/01.MOO.gz'
    report = inventory(root, lock, [filename])
    with gzip.open(root / filename, 'rb') as stream:
        data = stream.read(sst286.LIMIT + 1)
    if len(data) > sst286.LIMIT:
        raise ValueError('decompressed corpus exceeds limit')
    tests, _ = sst286.parse_moo(data)
    allowed = {t['hash']: t for t in report['files'][filename]['captures']}
    selected = [t for t in tests if t['hash'] in allowed and t['exception'] is None
                and len(t['code']) >= 2 and t['code'][0] == 1 and t['code'][1] >> 6 != 3]
    completed = subprocess.run([str(probe.resolve()), '--data-bus'],
                               input=b''.join(sst286.packet(t) for t in selected),
                               capture_output=True, timeout=120, check=True)
    failures = []
    for test, (status, actual) in zip(selected, data_replies(completed.stdout, len(selected))):
        expected = [(x['address'], x['status']) for x in allowed[test['hash']]['transactions']
                    if x['status'] in ('MEMR', 'MEMW')]
        if status or actual != expected:
            failures.append({'idx': test['idx'], 'hash': test['hash'], 'status': status,
                             'expected': expected, 'actual': actual})
    return {'schema': 'blumach-sst286-data-shape-v1', 'corpus_commit': lock['commit'],
            'input_sha256': report['input_sha256'], 'tool_sha256': report['tool_sha256'],
            'decoder_sha256': report['decoder_sha256'],
            'probe_sha256': hashlib.sha256(probe.read_bytes()).hexdigest(),
            'scope': 'unprefixed nonfaulting ADD memory data address/order only; no widths, values, prefetch or timing',
            'temporal_comparison': 'not-performed', 'selected': len(selected),
            'passed': len(selected) - len(failures), 'failures': failures}


def compare_supply(root, lock, probe):
    """Falsification test for the serialized adapter, NOT a calibrated EU model."""
    filename = 'v1_real_mode/01.MOO.gz'
    report = inventory(root, lock, [filename])
    with gzip.open(root / filename, 'rb') as stream:
        raw = stream.read(sst286.LIMIT + 1)
    if len(raw) > sst286.LIMIT:
        raise ValueError('decompressed corpus exceeds limit')
    tests, masks = sst286.parse_moo(raw)
    allowed = {t['hash']: t for t in report['files'][filename]['captures']}
    selected = [t for t in tests if t['hash'] in allowed and t['exception'] is None
                and len(t['code']) >= 2 and t['code'][0] == 1 and t['code'][1] >> 6 != 3]
    request = b''.join(sst286.packet(t) for t in selected)
    functional = subprocess.run([str(probe.resolve()), '--supply'], input=request,
        capture_output=True, timeout=120, check=True)
    result = subprocess.run([str(probe.resolve()), '--supply-data-bus'], input=request,
        capture_output=True, timeout=120, check=True)
    traces = supply_replies(result.stdout, len(selected))
    functional_results = sst286.replies(functional.stdout, len(selected))
    counts = Counter()
    details = []
    for test, (status, actual), registers in zip(selected, traces, functional_results):
        functional_diff = sst286.differences(test, registers, masks)
        expected = [(x['address'], 1 if x['status'] == 'MEMW' else 0, x['sample'])
                    for x in allowed[test['hash']]['transactions'] if x['status'] in ('MEMR','MEMW')]
        shape_match = not status and [(a,w) for a,w,c in actual] == [(a,w) for a,w,c in expected]
        gap_match = shape_match and relative_gaps(actual) == relative_gaps(expected)
        counts['functional_passed' if not functional_diff else 'functional_failed'] += 1
        counts['shape_passed' if shape_match else 'shape_failed'] += 1
        counts['relative_spacing_passed' if gap_match else 'relative_spacing_failed'] += 1
        if not gap_match or functional_diff:
            details.append({'idx': test['idx'], 'hash': test['hash'], 'status': status,
                            'functional_differences': functional_diff, 'expected': expected, 'actual': actual})
    return {'schema': 'blumach-sst286-supply-experiment-v1', 'corpus_commit': lock['commit'],
            'probe_sha256': hashlib.sha256(probe.read_bytes()).hexdigest(),
            'tool_sha256': report['tool_sha256'], 'decoder_sha256': report['decoder_sha256'],
            'input_sha256': report['input_sha256'], 'selected': len(selected), 'counts': dict(counts),
            'scope': 'ADD data relative spacing only; serialized demand-supply hypothesis lacks EU delays and overlap',
            'cpu_elapsed_qualified': False, 'capture_origin_compared': False, 'details': details}


def relative_gaps(transfers):
    return [c - transfers[0][2] for a,w,c in transfers] if transfers else []


def supply_replies(data, count):
    offset, result = 0, []
    for _ in range(count):
        if len(data) - offset < 8:
            raise ValueError('truncated supply trace')
        status, n = struct.unpack_from('<iI', data, offset); offset += 8
        if n > 65536 or len(data) - offset < 9 * n:
            raise ValueError('invalid supply trace count')
        ops = list(struct.iter_unpack('<IBI', data[offset:offset + 9 * n])); offset += 9 * n
        if any(a >= 0x1000000 or w > 1 for a,w,c in ops) or any(
                ops[i][2] <= ops[i-1][2] for i in range(1, len(ops))):
            raise ValueError('invalid supply trace fields/order')
        result.append((status, ops))
    if offset != len(data):
        raise ValueError('trailing supply trace')
    return result


def summarize(test):
    """Keep sample offsets; do not infer EU boundaries from bus activity."""
    states = Counter()
    transfers = []
    halts = []
    for offset, sample in enumerate(test['cycles']):
        pins, address, segment, memory, io, bhe, data, status, phase, qop, qread = sample
        phase &= 7
        status &= 15
        if phase not in (0, 1, 2):
            raise ValueError('invalid C286 T-state')
        states[('Ti', 'Ts', 'Tc')[phase]] += 1
        if status == 4:
            halts.append(offset)
        if phase == 1 and status in STATUS and status != 4:
            transfers.append({'sample': offset, 'address': address,
                              'status': STATUS[status]})
    return {'idx': test['idx'], 'hash': test['hash'],
            'code_hex': test['code'].hex(),
            'exception_hex': test['exception'].hex() if test['exception'] is not None else None,
            'capture_samples': len(test['cycles']), 't_states': dict(states),
            'first_halt_sample': halts[0] if halts else None,
            'transactions': transfers, 'comparison': 'not-performed'}


def inventory(root, lock, filenames):
    root = sst286.verified_inputs(root, lock)
    if not filenames or len(set(filenames)) != len(filenames):
        raise ValueError('empty/duplicate file selection')
    if any(f not in lock['test_files'] for f in filenames):
        raise ValueError('selection outside pinned corpus')
    revoked = set()
    for line in (root / 'revocation_list.txt').read_text().splitlines():
        value = line.split('#', 1)[0].strip()
        if value:
            if len(value) != 40 or any(c not in '0123456789abcdef' for c in value):
                raise ValueError('invalid revocation hash')
            revoked.add(value)
    report = {'schema': 'blumach-sst286-capture-inventory-v1',
              'corpus_commit': lock['commit'], 'cpu': lock['cpu'],
              'scope': 'whole hardware capture including instruction supply and terminal HLT; not instruction clocks',
              'comparison': 'not-performed', 'input_sha256': lock['sha256'],
              'tool_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'decoder_sha256': hashlib.sha256(Path(sst286.__file__).read_bytes()).hexdigest(),
              'files': {}}
    for filename in filenames:
        with gzip.open(root / filename, 'rb') as stream:
            data = stream.read(sst286.LIMIT + 1)
        if len(data) > sst286.LIMIT:
            raise ValueError('decompressed corpus exceeds limit')
        tests, _ = sst286.parse_moo(data, include_cycles=True)
        selected = [summarize(t) for t in tests if t['hash'] not in revoked]
        report['files'][filename] = {'total': len(tests),
                                    'revoked': len(tests) - len(selected), 'captures': selected}
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--file', action='append',
                        help='Pinned relative filename; repeat to select more captures')
    parser.add_argument('--compare-add-data', type=Path, metavar='PROBE',
                        help='Compare ADD data access order/addresses only, not elapsed timing')
    parser.add_argument('--compare-supply', type=Path, metavar='PROBE',
                        help='Test serialized demand-supply hypothesis against relative ADD data spacing')
    args = parser.parse_args()
    try:
        lock = json.loads(sst286.LOCK.read_text())
        if args.compare_supply:
            if args.file or args.compare_add_data:
                raise ValueError('choose one comparison mode')
            report = compare_supply(args.corpus, lock, args.compare_supply)
            print(json.dumps(report, indent=2))
            return 1 if report['details'] else 77  # Even spacing matches cannot certify elapsed time.
        if args.compare_add_data:
            if args.file:
                raise ValueError('--file cannot accompany --compare-add-data')
            report = compare_add(args.corpus, lock, args.compare_add_data)
            print(json.dumps(report, indent=2))
            return 1 if report['failures'] else (0 if report['selected'] else 77)
        report = inventory(args.corpus, lock, args.file)
        print(json.dumps(report, indent=2))
        return 77
    except (ValueError, KeyError, OSError, StopIteration, subprocess.SubprocessError) as error:
        print(f'evidence error: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())

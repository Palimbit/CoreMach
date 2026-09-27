# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 BluMach contributors
"""Authored miniature records: no hardware vectors downloaded by these tests."""
import gzip
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from tools import sst286 as sst


def chunk(tag, data):
    return tag + struct.pack('<I', len(data)) + data


def counted(data):
    return struct.pack('<I', len(data)) + data


def regs(values):
    mask = sum(1 << i for i, name in enumerate(sst.REGS) if name in values)
    return struct.pack('<H', mask) + b''.join(struct.pack('<H', values[n]) for n in sst.REGS if n in values)


def initial():
    values = dict.fromkeys(sst.REGS, 0)
    values.update(cs=0x1000, ss=0x2000, ip=0x100, sp=0x200, ax=0x1234, flags=2)
    return values


def state(values, memory):
    return chunk(b'REGS', regs(values)) + chunk(b'RAM ', struct.pack('<I', len(memory)) +
        b''.join(struct.pack('<IB', a, v) for a, v in memory.items()))


def corpus():
    test = (struct.pack('<I', 0) + chunk(b'NAME', counted(b'nop')) +
            chunk(b'BYTS', counted(b'\x90\xf4')) +
            chunk(b'INIT', state(initial(), {0x10100: 0x90, 0x10101: 0xf4})) +
            chunk(b'FINA', state({'ip': 0x102}, {})) + chunk(b'HASH', b'\x11' * 20))
    return chunk(b'MOO ', struct.pack('<BBHI4s', 1, 1, 0, 1, b'C286')) + chunk(b'TEST', test)


class SST286Tests(unittest.TestCase):
    def test_grouped_metadata(self):
        metadata = {'opcodes': {'FF': {'flags-mask': 0x0fff,
                                      'reg': {'5': {'flags-mask': 0xffef}}}}}
        self.assertEqual(sst.opcode_metadata(metadata, 'FF.5')['flags-mask'], 0x0fef)
        with self.assertRaises(KeyError): sst.opcode_metadata(metadata, 'FF.8')

    def test_sparse_final_and_halt_normalization(self):
        tests, masks = sst.parse_moo(corpus())
        actual = initial(); actual['ip'] += 1
        self.assertEqual(sst.differences(tests[0], (0, actual, {}), masks), [])
        actual['bx'] = 1  # Unmentioned final register must still be compared.
        self.assertTrue(sst.differences(tests[0], (0, actual, {}), masks))

    def test_flags_mask_and_memory_mismatch(self):
        test = sst.parse_moo(corpus())[0][0]
        actual = initial(); actual.update(ip=0x101, flags=0x12)
        self.assertEqual(sst.differences(test, (0, actual, {}), {'flags': 0xffef}), [])
        self.assertTrue(sst.differences(test, (0, actual, {}), {}))
        self.assertTrue(sst.differences(test, (0, actual, {0x10100: 0}), {'flags': 0xffef}))
        self.assertTrue(sst.differences(test, (0, actual, {0x500: 0}), {'flags': 0xffef}))

    def test_missing_expected_write(self):
        test = sst.parse_moo(corpus())[0][0]
        actual = initial(); actual['ip'] += 1
        test['final']['ram'][0x123] = 5
        self.assertTrue(sst.differences(test, (0, actual, {}), {}))
        self.assertEqual(sst.differences(test, (0, actual, {0x123: 5}), {}), [])

    def test_pending_not_failure_suppression(self):
        test = sst.parse_moo(corpus())[0][0]
        self.assertIsNone(sst.pending_reason(test))
        self.assertTrue(sst.differences(test, (-8, initial(), {}), {}))
        test['exception'] = b'\x0d' + bytes(4)
        self.assertEqual(sst.pending_reason(test), 'guest-exception-13')
        test['exception'] = None; test['code'] = b'\x26\xf0\x90'
        self.assertEqual(sst.pending_reason(test), 'lock-or-repeat-prefix')
        test['code'] = b'\x36\xff\x2f'
        self.assertIsNone(sst.pending_reason(test))
        test['code'] = b'\xff\xe8'  # Register form is not a far pointer.
        self.assertEqual(sst.pending_reason(test), 'instruction-outside-initial-scope')
        test['code'] = b'\xff\x1f'  # Far CALL is still outside this tranche.
        self.assertEqual(sst.pending_reason(test), 'instruction-outside-initial-scope')
        test['code'] = b'\x17'
        self.assertIsNone(sst.pending_reason(test))
        test['code'] = b'\x8e\xd0'
        self.assertIsNone(sst.pending_reason(test))

    def test_truncated_chunks_and_duplicate_identity(self):
        data = corpus()
        for i in (0, 1, 7, 10, 19, len(data) - 1):
            with self.assertRaises((ValueError, StopIteration)):
                sst.parse_moo(data[:i])
        with self.assertRaises(ValueError):
            sst.parse_moo(data + data[20:])
        with self.assertRaises(ValueError):
            sst.registers(b'\xff\xff' + bytes(32))
        with self.assertRaises(ValueError):
            sst.unique_chunks(chunk(b'ABCD', b'') * 2)

    def test_transport_rejects_truncation_and_extra_output(self):
        data = struct.pack('<i14HI', 0, *(initial()[n] for n in sst.REGS), 0)
        self.assertEqual(len(sst.replies(data, 1)), 1)
        for bad in (data[:-1], data + b'X'):
            with self.assertRaises(ValueError):
                sst.replies(bad, 1)

    def test_hash_and_revocation(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); (root / 'v1_real_mode').mkdir()
            files = {'v1_real_mode/90.MOO.gz': gzip.compress(corpus()),
                     'v1_real_mode/metadata.json': b'{"opcodes":{"90":{}}}',
                     'revocation_list.txt': ('11' * 20 + '\n').encode()}
            for name, data in files.items(): (root / name).write_bytes(data)
            lock = {'commit': 'test-only', 'test_files': ['v1_real_mode/90.MOO.gz'],
                    'sha256': {n: hashlib.sha256(d).hexdigest() for n, d in files.items()}}
            probe = root / 'unused'; probe.write_bytes(b'not executable')
            with patch.object(sst.subprocess, 'run') as run:
                report = sst.run(root, probe, lock)
                run.assert_not_called()
            self.assertEqual(report['counts']['revoked'], 1)
            self.assertEqual(report['counts']['passed'], 0)
            with self.assertRaises(ValueError):
                sst.verified_inputs(root, dict(lock, test_files=['not-pinned.MOO.gz']))
            (root / 'revocation_list.txt').write_text('changed')
            with self.assertRaises(ValueError): sst.verified_inputs(root, lock)

    @unittest.skipUnless(os.environ.get('BM_SST286_PROBE'), 'C probe supplied by CTest')
    def test_real_probe_and_session_isolation(self):
        probe = os.environ['BM_SST286_PROBE']
        test = sst.parse_moo(corpus())[0][0]
        result = subprocess.run([probe], input=sst.packet(test) * 2, capture_output=True,
                                timeout=10, check=True)
        for reply in sst.replies(result.stdout, 2):
            self.assertEqual(sst.differences(test, reply, {}), [])
        result = subprocess.run([probe], input=b'\x01', capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 2)


if __name__ == '__main__':
    unittest.main()

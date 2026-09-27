# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 BluMach contributors
"""Synthetic capture fixtures; no external data or emulated firmware."""
import struct
import gzip
import hashlib
from pathlib import Path
import tempfile
import os
import subprocess
import unittest
from unittest.mock import patch
from tools import sst286, sst286_trace
from tools.tests.test_sst286 import corpus, chunk


def sample(status=13, phase=1, address=0x100):
    return (0, address, 0, 0, 0, 0, 0, status, phase, 0, 0)


def with_cycles(samples):
    blob = struct.pack('<I', len(samples)) + b''.join(
        struct.pack('<BIBBBBHBBBB', *s) for s in samples)
    records = list(sst286.chunks(corpus()))
    return b''.join(chunk(tag, bytes(payload) + (chunk(b'CYCL', blob) if tag == b'TEST' else b''))
                    for tag, payload in records)


class TraceTests(unittest.TestCase):
    def test_supply_trace_protocol_and_relative_origin(self):
        a = [(0x200, 0, 11), (0x200, 1, 15)]
        response = struct.pack('<iI', 0, 2) + b''.join(struct.pack('<IBI', *x) for x in a)
        self.assertEqual(sst286_trace.supply_replies(response, 1), [(0, a)])
        self.assertEqual(sst286_trace.relative_gaps(a), [0, 4])
        self.assertEqual(sst286_trace.relative_gaps([(0x200,0,4),(0x200,1,8)]), [0,4])
        self.assertNotEqual(sst286_trace.relative_gaps([(0x200,0,4),(0x200,1,6)]), [0,4])
        for bad in (b'', response[:-1], response + b'\0', struct.pack('<iI', 0, 65537),
                    struct.pack('<iIIBI', 0, 1, 0x1000000, 0, 1),
                    struct.pack('<iIIBI', 0, 1, 0x200, 2, 1),
                    struct.pack('<iI',0,2) + struct.pack('<IBI',0,0,4)*2):
            with self.assertRaises(ValueError):
                sst286_trace.supply_replies(bad, 1)

    @unittest.skipUnless(os.environ.get('BM_SST286_PROBE'), 'compiled probe not supplied')
    def test_live_supply_preserves_result_but_does_not_claim_eu_time(self):
        from tools.tests.test_sst286 import initial
        registers = initial(); registers.update(ds=0, bx=0x200, ax=1)
        test = {'initial': {'regs': registers,
                'ram': {0x10100: 0x01, 0x10101: 0x07, 0x200: 2, 0x201: 0}}}
        request = sst286.packet(test)
        baseline = subprocess.run([os.environ['BM_SST286_PROBE']], input=request,
            capture_output=True, check=True, timeout=15)
        supply = subprocess.run([os.environ['BM_SST286_PROBE'], '--supply'], input=request,
            capture_output=True, check=True, timeout=15)
        self.assertEqual(baseline.stdout, supply.stdout)
        trace = subprocess.run([os.environ['BM_SST286_PROBE'], '--supply-data-bus'], input=request,
            capture_output=True, check=True, timeout=15)
        self.assertEqual(sst286_trace.supply_replies(trace.stdout, 1),
                         [(0, [(0x200,0,2),(0x200,1,4)])])

    def test_comparison_reports_order_and_probe_failures(self):
        t = sst286.parse_moo(corpus())[0][0]
        t['code'] = b'\x01\x07\xf4'
        filename = 'v1_real_mode/01.MOO.gz'
        capture = {'hash': t['hash'], 'transactions': [
            {'address': 0x200, 'status': 'MEMR'}, {'address': 0x200, 'status': 'MEMW'}]}
        evidence = {'files': {filename: {'captures': [capture]}},
                    'input_sha256': {}, 'tool_sha256': 'test', 'decoder_sha256': 'test'}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'v1_real_mode').mkdir()
            (root / filename).write_bytes(gzip.compress(b'fixture'))
            probe = root / 'probe'; probe.write_bytes(b'not executed')
            with patch.object(sst286_trace, 'inventory', return_value=evidence), \
                 patch.object(sst286, 'parse_moo', return_value=([t], {})), \
                 patch.object(subprocess, 'run') as run:
                for status, ops, expected_pass in (
                    (0, [(0x200, 0), (0x200, 1)], 1),
                    (0, [(0x200, 1), (0x200, 0)], 0),
                    (-8, [(0x200, 0), (0x200, 1)], 0)):
                    run.return_value.stdout = struct.pack('<iI', status, len(ops)) + b''.join(
                        struct.pack('<IB', *op) for op in ops)
                    result = sst286_trace.compare_add(root, {'commit': 'test'}, probe)
                    self.assertEqual(result['passed'], expected_pass)
                    self.assertEqual(len(result['failures']), 1 - expected_pass)
                    self.assertEqual(result['temporal_comparison'], 'not-performed')

    def test_data_reply_bounds_and_errors(self):
        response = struct.pack('<iIIB', 0, 1, 0x1234, 1)
        self.assertEqual(sst286_trace.data_replies(response, 1), [(0, [(0x1234, 'MEMW')])])
        self.assertEqual(sst286_trace.data_replies(struct.pack('<iI', -8, 0), 1), [(-8, [])])
        for bad in (b'', response[:-1], response + b'\0',
                    struct.pack('<iI', 0, 65537),
                    struct.pack('<iIIB', 0, 1, 0x1000000, 0),
                    struct.pack('<iIIB', 0, 1, 1, 2)):
            with self.assertRaises(ValueError):
                sst286_trace.data_replies(bad, 1)

    @unittest.skipUnless(os.environ.get('BM_SST286_PROBE'), 'compiled probe not supplied')
    def test_live_probe_add_parity(self):
        from tools.tests.test_sst286 import initial
        tests = []
        for address in (0x200, 0x201):
            registers = initial()
            registers.update(ds=0, bx=address, ax=1)
            tests.append({'initial': {'regs': registers,
                'ram': {0x10100: 0x01, 0x10101: 0x07, address: 2, address + 1: 0}}})
        result = subprocess.run([os.environ['BM_SST286_PROBE'], '--data-bus'],
            input=b''.join(map(sst286.packet, tests)), capture_output=True, check=True, timeout=15)
        self.assertEqual(sst286_trace.data_replies(result.stdout, 2), [
            (0, [(0x200, 'MEMR'), (0x200, 'MEMW')]),
            (0, [(0x201, 'MEMR'), (0x202, 'MEMR'), (0x201, 'MEMW'), (0x202, 'MEMW')])])

    def test_pinned_inventory_and_revocation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'v1_real_mode').mkdir()
            filename = 'v1_real_mode/90.MOO.gz'
            (root / filename).write_bytes(gzip.compress(with_cycles([sample(4, 0)])))
            (root / 'v1_real_mode/metadata.json').write_text('{}')
            (root / 'revocation_list.txt').write_text('# comment\n' + '11' * 20 + '\n')
            lock = {'test_files': [filename], 'commit': 'synthetic', 'cpu': 'synthetic',
                    'sha256': {f: hashlib.sha256((root / f).read_bytes()).hexdigest()
                               for f in (filename, 'revocation_list.txt', 'v1_real_mode/metadata.json')}}
            result = sst286_trace.inventory(root, lock, [filename])
            self.assertEqual(result['files'][filename], {'total': 1, 'revoked': 1, 'captures': []})
            self.assertEqual(result['comparison'], 'not-performed')
            for files in ([], [filename, filename], ['unlisted.gz']):
                with self.assertRaises(ValueError):
                    sst286_trace.inventory(root, lock, files)
            (root / 'revocation_list.txt').write_text('')
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                sst286_trace.inventory(root, lock, [filename])

    def test_opt_in_does_not_change_functional_parser(self):
        self.assertNotIn('cycles', sst286.parse_moo(corpus())[0][0])
        with self.assertRaisesRegex(ValueError, 'missing CYCL'):
            sst286.parse_moo(corpus(), include_cycles=True)

    def test_full_capture_not_instruction_cost(self):
        samples = [sample(), sample(13, 2), sample(5), sample(5, 2),
                   sample(6), sample(6, 2), sample(4, 0), sample(7, 0)]
        t = sst286.parse_moo(with_cycles(samples), include_cycles=True)[0][0]
        result = sst286_trace.summarize(t)
        self.assertEqual(result['capture_samples'], 8)
        self.assertEqual(result['first_halt_sample'], 6)
        self.assertEqual(result['t_states'], {'Ts': 3, 'Tc': 3, 'Ti': 2})
        self.assertEqual([x['status'] for x in result['transactions']], ['CODE', 'MEMR', 'MEMW'])
        self.assertEqual([x['sample'] for x in result['transactions']], [0, 2, 4])
        self.assertEqual(result['comparison'], 'not-performed')
        self.assertNotIn('instruction_clocks', result)

    def test_missing_halt_not_invented(self):
        t = sst286.parse_moo(with_cycles([sample()]), include_cycles=True)[0][0]
        self.assertIsNone(sst286_trace.summarize(t)['first_halt_sample'])

    def test_raw_fields_preserved(self):
        samples = [sample(0xed, 0x81, 0xffffff)]
        t = sst286.parse_moo(with_cycles(samples), include_cycles=True)[0][0]
        self.assertEqual(t['cycles'], samples)
        self.assertEqual(sst286_trace.summarize(t)['transactions'][0]['status'], 'CODE')

    def test_bad_cycle_records(self):
        for blob in (b'', b'\0', bytes(4), struct.pack('<I', 1),
                     struct.pack('<I', 0xffffffff) + bytes(15),
                     struct.pack('<I', 1) + bytes(16)):
            with self.assertRaises(ValueError):
                sst286.cycle_records(blob)
        t = sst286.parse_moo(with_cycles([sample(5, 3)]), include_cycles=True)[0][0]
        with self.assertRaises(ValueError):
            sst286_trace.summarize(t)


if __name__ == '__main__':
    unittest.main()

# SPDX-License-Identifier: GPL-2.0-or-later
import unittest
from tools.portable_latency_report import report


class LatencyReportTest(unittest.TestCase):
    def test_coalesced_frame_and_multiple_inputs(self):
        data = "\n".join([
            "bm-latency input-queued id=1 us=1000 value=0",
            "bm-latency input-queued id=2 us=2000 value=0",
            "bm-latency input-dispatch id=1 us=3000 value=0",
            "bm-latency input-dispatch id=2 us=4000 value=0",
            "bm-latency frame-ready id=3 us=5000 value=2",
            # Frame 3 was coalesced; only frame 4 was painted.
            "bm-latency frame-ready id=4 us=6000 value=2",
            "bm-latency paint-submit id=4 us=7000 value=0",
            "bm-latency paint-submit id=4 us=9000 value=0",
        ])
        result = report(data)
        self.assertEqual(result['enqueue_to_dispatch']['max_ms'], 2)
        self.assertEqual(result['enqueue_to_first_post_input_paint_submission'],
                         {'samples': 2, 'p50_ms': 5, 'p95_ms': 6, 'max_ms': 6})

    def test_no_samples_is_not_zero_latency(self):
        self.assertEqual(report('')['enqueue_to_dispatch'], {'samples': 0})


if __name__ == '__main__':
    unittest.main()

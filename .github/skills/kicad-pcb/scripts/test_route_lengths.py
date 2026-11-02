"""Focused graph tests; run with KiCad's bundled Python."""

import runpy
import unittest
from pathlib import Path


shortest_metrics = runpy.run_path(
    str(Path(__file__).with_name("report-route-lengths.py"))
)["shortest_metrics"]


class RouteLengthTests(unittest.TestCase):
    def test_prefers_fewer_layer_changes_for_equal_length(self):
        start, same_layer, other_layer, finish = (
            (0, 0, 0), (1, 0, 0), (1, 0, 2), (2, 0, 0)
        )
        graph = {
            start: [(same_layer, 2.0), (other_layer, 2.0)],
            same_layer: [(finish, 3.0)],
            other_layer: [(finish, 3.0)],
            finish: [],
        }
        self.assertEqual(shortest_metrics(graph, (0, 0), (2, 0)), (5.0, 0))

    def test_counts_layer_transition(self):
        graph = {
            (0, 0, 0): [((1, 0, 0), 2.0)],
            (1, 0, 0): [((1, 0, 2), 0.0)],
            (1, 0, 2): [((2, 0, 2), 3.0)],
            (2, 0, 2): [],
        }
        self.assertEqual(shortest_metrics(graph, (0, 0), (2, 0)), (5.0, 1))

    def test_rejects_disconnected_or_missing_endpoints(self):
        graph = {(0, 0, 0): [], (1, 0, 0): []}
        with self.assertRaisesRegex(ValueError, "Disconnected"):
            shortest_metrics(graph, (0, 0), (1, 0))
        with self.assertRaisesRegex(ValueError, "Missing"):
            shortest_metrics(graph, (0, 0), (2, 0))


if __name__ == "__main__":
    unittest.main()

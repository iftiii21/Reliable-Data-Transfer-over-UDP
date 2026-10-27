"""Synthetic records here test analysis rejection/aggregation, not performance."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

import pandas as pd

spec = importlib.util.spec_from_file_location(
    "plot_runs", Path(__file__).resolve().parents[1] / "analysis" / "plot_runs.py")
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


class AnalysisTests(unittest.TestCase):
    def row(self, seed=1, **changes):
        row = dict(run_id=f"run{seed}", protocol="fixture", loss=0, reorder=0,
                   window=1, seed=seed, delay_ms=10, jitter_ms=0, reorder_ms=25,
                   duplicate=0, corrupt=0, direction="both", bytes=100,
                   elapsed_s=2, retransmissions=3, goodput_bps=400,
                   sha256_source="a" * 64, sha256_received="a" * 64, status="ok")
        return row | changes

    def load(self, rows):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "runs.csv"
            pd.DataFrame(rows).to_csv(path, index=False)
            return analysis.load_runs(path)

    def test_failures_remain_in_denominator(self):
        frame = self.load([self.row(), self.row(2, status="hash_mismatch",
                                               sha256_received="b" * 64)])
        result = analysis.summarize(frame).iloc[0]
        self.assertEqual(result["success_rate"], 0.5)
        self.assertEqual(result["goodput_mean"], 400)
        self.assertEqual(result["verified_runs"], 1)

    def test_invalid_success_rejected(self):
        for changes in [dict(sha256_received="b" * 64), dict(goodput_bps=800),
                        dict(elapsed_s=0), dict(retransmissions=-1),
                        dict(retransmissions=0.5), dict(goodput_bps="")]:
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.load([self.row(**changes)])

    def test_seeds_and_controls(self):
        with self.assertRaises(ValueError):
            self.load([self.row(), self.row(run_id="other")])
        for changes in [dict(loss=-1), dict(window=0), dict(seed=-1),
                        dict(direction="else"), dict(sha256_source="xyz")]:
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.load([self.row(**changes)])

    def test_failed_only_group_and_plot(self):
        frame = self.load([self.row(status="missing_adapter", elapsed_s="",
                                   retransmissions="", goodput_bps="", sha256_received="")])
        result = analysis.summarize(frame)
        self.assertEqual(result.iloc[0]["success_rate"], 0)
        with tempfile.TemporaryDirectory() as directory:
            analysis.plot_summary(result, Path(directory), "loss")
            self.assertTrue(list(Path(directory).glob("*.png")))

    def test_mean_and_sample_sd(self):
        frame = self.load([self.row(), self.row(2, elapsed_s=1, goodput_bps=800)])
        result = analysis.summarize(frame).iloc[0]
        self.assertEqual(result["goodput_mean"], 600)
        self.assertAlmostEqual(result["goodput_std"], 282.842712, places=5)


if __name__ == "__main__":
    unittest.main()

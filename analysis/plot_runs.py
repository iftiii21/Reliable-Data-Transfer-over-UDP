"""Analyze measured, hash-verified runs. This never simulates protocol results."""
import argparse
from pathlib import Path
import re

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

CONTROLS = [
    "delay_ms", "jitter_ms", "reorder_ms", "duplicate", "corrupt",
    "direction", "bytes", "sha256_source",
]
GROUP = ["protocol", "loss", "reorder", "window", *CONTROLS]
METRICS = ["goodput_bps", "elapsed_s", "retransmissions"]


def load_runs(path):
    frame = pd.read_csv(path, keep_default_na=False)
    required = {"run_id", "seed", "status", "sha256_received", *GROUP, *METRICS}
    missing = required.difference(frame.columns)
    if missing or frame.empty:
        raise ValueError(f"Empty or incomplete runs CSV; missing columns: {sorted(missing)}")
    for column in [*METRICS, "loss", "reorder", "window", "seed", "delay_ms",
                   "jitter_ms", "reorder_ms", "duplicate", "corrupt", "bytes"]:
        frame[column] = pd.to_numeric(frame[column], errors="coerce")
    for column in ["loss", "reorder", "duplicate", "corrupt"]:
        if not frame[column].between(0, 1).all():
            raise ValueError(f"{column} must be within [0, 1]")
    for column in ["window", "seed", "delay_ms", "jitter_ms", "reorder_ms", "bytes"]:
        values = frame[column]
        if not (np.isfinite(values) & (values >= 0) & (values % 1 == 0)).all():
            raise ValueError(f"Invalid integer control: {column}")
    if (frame["window"] < 1).any() or not frame["direction"].isin(["data", "ack", "both"]).all():
        raise ValueError("Invalid window or channel direction")
    if not frame["sha256_source"].map(lambda value: bool(re.fullmatch(r"[a-f0-9]{64}", str(value)))).all():
        raise ValueError("Invalid source SHA-256")
    if frame.duplicated(["run_id"]).any() or frame.duplicated([*GROUP, "seed"]).any():
        raise ValueError("Duplicate run IDs or repeated seed within a comparison group")
    ok = frame["status"].eq("ok")
    valid = frame.loc[ok]
    if not valid["sha256_source"].eq(valid["sha256_received"]).all():
        raise ValueError("A run marked ok has mismatching file hashes")
    if not np.isfinite(valid[METRICS].to_numpy(dtype=float)).all():
        raise ValueError("A run marked ok has missing or non-finite metrics")
    if (valid["elapsed_s"] <= 0).any() or (valid["retransmissions"] < 0).any() or (
            valid["retransmissions"] % 1 != 0).any():
        raise ValueError("Invalid elapsed time or retransmission count")
    expected = valid["bytes"] * 8 / valid["elapsed_s"]
    if not np.allclose(expected, valid["goodput_bps"], rtol=1e-6, atol=1e-6):
        raise ValueError("Goodput must use useful file bytes / elapsed time")
    frame["verified"] = ok
    return frame


def summarize(frame):
    totals = frame.groupby(GROUP, dropna=False, as_index=False).agg(
        total_runs=("status", "size"), verified_runs=("verified", "sum"))
    valid = frame.loc[frame["verified"]]
    stats = valid.groupby(GROUP, dropna=False, as_index=False).agg(
        goodput_mean=("goodput_bps", "mean"), goodput_std=("goodput_bps", "std"),
        elapsed_mean=("elapsed_s", "mean"), elapsed_std=("elapsed_s", "std"),
        retransmissions_mean=("retransmissions", "mean"),
        retransmissions_std=("retransmissions", "std"))
    result = totals.merge(stats, on=GROUP, how="left")
    result["success_rate"] = result["verified_runs"] / result["total_runs"]
    # n=1 has no estimable sample standard deviation; plots show no error bar.
    return result


def plot_summary(summary, output, x):
    fixed = [column for column in ["loss", "reorder", *CONTROLS] if column != x]
    labels = {"loss": "Packet loss probability", "reorder": "Reordering probability",
              "window": "Sender window (packets)"}
    for index, (keys, subset) in enumerate(summary.groupby(fixed, dropna=False)):
        figure, axes = plt.subplots(1, 4, figsize=(17, 4.5), layout="constrained")
        series_keys = ["protocol"] if x == "window" else ["protocol", "window"]
        for series, group in subset.groupby(series_keys, dropna=False):
            group = group.sort_values(x)
            series = series if isinstance(series, tuple) else (series,)
            label = str(series[0]) + (f" (w={int(series[1])})" if len(series) > 1 else "")
            successful = group.loc[group["verified_runs"] > 0]
            for axis, mean, std, scale, ylabel in zip(
                axes[:3], ["goodput_mean", "elapsed_mean", "retransmissions_mean"],
                ["goodput_std", "elapsed_std", "retransmissions_std"],
                [1e6, 1, 1], ["Goodput (Mbit/s)", "Sender time (s)", "Retransmissions"],
            ):
                axis.errorbar(successful[x], successful[mean] / scale,
                              yerr=successful[std].fillna(0) / scale,
                              marker="o", capsize=3, label=label)
                axis.set_ylabel(ylabel)
            axes[3].plot(group[x], group["success_rate"], marker="o", label=label)
        axes[3].set_ylabel("Verified success fraction")
        axes[3].set_ylim(-0.05, 1.05)
        for axis in axes:
            axis.set_xlabel(labels[x])
            axis.grid(alpha=0.25)
        axes[0].legend(fontsize=8)
        keys = keys if isinstance(keys, tuple) else (keys,)
        context = dict(zip(fixed, keys))
        title = ", ".join(f"{key}={value}" for key, value in context.items()
                          if key not in {"sha256_source"})
        fixture = set(subset["protocol"]) == {"fixture"}
        prefix = "TEST FIXTURE ONLY — " if fixture else ""
        figure.suptitle(prefix + title + "\nMeans ± sample SD; failures excluded from metrics and retained in success fraction",
                        fontsize=10)
        figure.savefig(output / f"{x}_{index:02d}.png", dpi=160)
        plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runs", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--x", choices=["loss", "reorder", "window"], default="loss")
    args = parser.parse_args()
    frame = load_runs(args.runs)
    summary = summarize(frame)
    args.output.mkdir(parents=True, exist_ok=False)
    summary.to_csv(args.output / "summary.csv", index=False)
    plot_summary(summary, args.output, args.x)
    print(f"Analyzed {len(frame)} runs; {int(frame['verified'].sum())} hash-verified successes")


if __name__ == "__main__":
    main()

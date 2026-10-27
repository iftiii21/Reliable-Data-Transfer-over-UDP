# Protocol experiments

## Implemented adapters and endpoints

Runnable file-transfer adapters are implemented for all three protocols:

- Stop-and-Wait: `experiments/adapters/sw.sh`
- Go-Back-N: `experiments/adapters/gbn.sh`
- Selective Repeat: `experiments/adapters/sr.sh`

They use the project's sender/receiver endpoints through the channel emulator and report retransmission metrics to the shared experiment runner. The runner starts bounded sender and receiver processes, records channel events and timing, and accepts a run only when transfer completion succeeds and the received file's SHA-256 matches the input.

## Retained matrix and measured scope

The retained run set is in `docs/validation/protocol-matrix-2026-10-28/runs.csv`; its aggregate CSVs and plots are alongside it. It contains 39 runs: 9 SW, 15 GBN, and 15 SR. Each protocol uses seeds 101, 202, and 303. All runs completed with status `ok`, and all source/received SHA-256 values match.

The same 12,675-byte binary fixture was used throughout (SHA-256 `c873768eb3dd9bd67c4afc11a5856dce1e9003d5e4a193d07d1f9af77258b508`). The matrix covers baseline traffic, loss, reordering, and combined loss/reordering at selected GBN/SR window sizes. It is a small controlled validation matrix, not a broad performance study. Its results do not establish that one protocol is universally faster or more reliable. Goodput and retransmission figures are conditional measurements for these seeds, fixture, implementation settings, and host; some groups show substantial variation.

For example, on the clean baseline cases the mean goodput in the retained summary was approximately 0.280 Mbit/s for SW, 1.029 Mbit/s for GBN, and 0.789 Mbit/s for SR (three runs per protocol). This is an observed result for this setup, not a general ranking. Consult the per-group summaries and plots for variability and the specific channel conditions.

## Reproducing the matrix

From a Linux/WSL environment with GNU Make, Python dependencies from `analysis/requirements.txt`, and the built project endpoints:

```sh
make all
bash experiments/run_matrix.sh experiments/matrix.csv \
  docs/validation/fixture-input.bin build/reproduction experiments/adapters
python3 analysis/plot_runs.py build/reproduction/runs.csv \
  --output build/reproduction/plots-loss --x loss
python3 analysis/plot_runs.py build/reproduction/runs.csv \
  --output build/reproduction/plots-reorder --x reorde
python3 analysis/plot_runs.py build/reproduction/runs.csv \
  --output build/reproduction/plots-window --x window
```

The runner refuses to reuse an existing output directory. Timings include sender process startup and transfer completion but exclude receiver startup. Run experiments on a quiet host and compare runs using the same fixture, channel controls, and build. The checked-in matrix is the selected 39-case scope; expanding seeds, file sizes, loss rates, delay distributions, and window sizes would be needed for stronger performance conclusions.

## Test record and limitations

The latest validation report recorded `make test` and `make test_analysis` as passing; the analysis suite contained five tests. The regression target covers protocol and transport tests, emulator impairments, transfer integrity, and experiment-runner behavior. Those test commands were not rerun while preparing these documents and copied result artifacts.

The emulator provides controlled impairment conditions, but measured localhost/WSL results are not a substitute for measurements on real networks. The matrix uses only three seeds per applicable group and one small fixture, and some combinations do not include all three protocols. Do not infer universal protocol superiority from this dataset.

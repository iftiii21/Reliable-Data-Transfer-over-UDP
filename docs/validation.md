# Validation results

## What was tested

The retained protocol matrix contains 39 successful file transfers: 9 Stop-and-Wait (SW), 15 Go-Back-N (GBN), and 15 Selective Repeat (SR). Each protocol's cases use seeds 101, 202, and 303. Every received file's SHA-256 matched its source file's SHA-256.

The matrix uses one 12,675-byte binary fixture with SHA-256 `c873768eb3dd9bd67c4afc11a5856dce1e9003d5e4a193d07d1f9af77258b508`. Conditions include baseline, 10% loss, 30% reordering, and combined loss/reordering at selected GBN/SR windows. The experiment runner measured sender elapsed time with a monotonic clock, including sender startup and transfer completion, and excluded receiver startup.

These are measurements from a small fixture matrix on one environment. They show that all configured runs completed and passed integrity checking; three seeds and a small file do not establish a universal protocol winner or predict performance on other machines, file sizes, or network conditions. Treat throughput and retransmission differences as observations within this matrix. In particular, variability across seeds is material in some groups.

## Previously recorded regression validation

The latest validation report recorded successful runs of `make test` and `make test_analysis`. The regression target covered ARQ, packet, file-transfer, timer/RTT, channel, UDP smoke, SR transfer/loss, emulator integration, experiment-runner behavior, and SW/GBN/SR emulator transfers. The analysis command ran five Python unit tests. C builds used strict warning flags including `-Werror`.

Those tests were not rerun during this documentation and artifact-copy preparation. The retained experiment CSV was checked separately for 39 rows, protocol counts (9/15/15), `ok` statuses, and matching source/received hashes.

## Retained evidence

The protocol matrix CSV, aggregate summaries, and plots are in [protocol-matrix-2026-10-28](protocol-matrix-2026-10-28/). The original run bundle remains under the ignored `build/` directory in the working tree. The earlier fixture-pipeline evidence remains below and documents emulator/runner validation, not protocol comparison results.

The fixture-pipeline input is `fixture-input.bin` (12,675 bytes) with the same SHA-256 above. Its retained summaries and plots cover baseline, loss, and additional holding. These results validate the experiment pipeline and should not be combined with or presented as the 39-run protocol comparison.

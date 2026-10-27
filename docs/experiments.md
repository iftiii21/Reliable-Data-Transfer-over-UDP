# Experiment execution and analysis

## Current availability

The main branch had no executable transports when this contribution began.
Syed's branch provides sender-side SW/GBN state machines; Indraneel's branch
provides SR command-line programs. We do not duplicate their protocol work here.
The SR adapter is supplied against that branch's CLI and must be exercised after
building those binaries on Linux. It rejects transfers without an explicit FIN
ACK confirmation, even if the existing CLI returns zero on a protocol failure.

## Adapter contract

Place one trusted shell adapter per protocol in an adapter directory:

    bash ADAPTER receiver PORT OUTPUT_FILE WINDOW
    bash ADAPTER sender HOST PORT INPUT_FILE WINDOW METRICS_FILE

The receiver must bind before printing a line beginning READY, write OUTPUT_FILE,
and exit zero only after the complete transfer has been persisted. Disable
stdout buffering so readiness reaches the runner. The sender must return nonzero
on transfer failure and write this plain-text metrics field:

    retransmissions=INTEGER

Count actual retransmitted datagrams, including control retransmissions, and
document any intentional difference. The supplied SR adapter sums the data
retransmission statistic and logged FIN retries. Its line-buffered receiver
requires GNU stdbuf. The adapter must not synthesize a valid output file after a
failed transfer. All children must finish or respond to the process-group
termination used by GNU timeout. Adapters are executable trusted code, not CSV
fragments; the runner never evaluates shell commands from the matrix.

For SR, build origin/indraneel in a separate checkout, then run with:

    SR_BIN_DIR=/absolute/path/to/sr-build bash experiments/run_matrix.sh \
      sr-only.csv input.bin results/sr1 experiments/adapters

Create sr-only.csv by filtering the supplied matrix while retaining its header.
SW and GBN adapters belong to the later runnable transport integration. Their
absence is reported as missing_adapter, not silently substituted with SR at
window=1. Never treat the echo test fixture as an ARQ protocol result.

## Controlled factors

The committed matrix has 39 cases: three independent seeds for baseline, 10%
loss, 30% reordering, and combined loss/reordering at windows 4 and 16. SW uses
window 1; standalone GBN/SR sweeps use window 8. This is an initial matrix;
extend with 0.05/0.15/0.2 loss and additional reordering/window values for the
final report. At least three seeds are required; five or more are preferable.

Defaults held constant: 10ms one-way base delay, zero jitter, 25ms reorder hold,
zero duplication, zero corruption, both directions impaired, same input file.
Set DELAY_MS, JITTER_MS, REORDER_MS, DUPLICATE, CORRUPT, DIRECTION as environment
variables to run separate controlled studies. RUN_TIMEOUT defaults to 30 seconds;
BASE_PORT defaults to 29000 (receiver uses BASE_PORT+1). Use separate port pairs
for concurrent experiment jobs. Run from a quiet machine; distinguish receiver
window policy and RTO differences before attributing results to ARQ alone.

## Evidence and failure handling

Every run gets its own directory containing sender/receiver logs, channel event
CSV, proxy summary, elapsed-time output, metrics, and received file. The output
directory must not already exist, preventing accidental overwrite. metadata.txt
records input SHA-256, commit, dirty-file count, platform, adapter file hashes,
controls, and timing definition. Preserve binary revisions/adapter dependencies
alongside the result bundle when comparing builds.

GNU time measures the sender process, including CLI startup and transfer
completion handshake, but excluding receiver startup. Its 10ms resolution is
unsuitable for tiny transfers; use a sufficiently large file and report the
timing definition. The runner rejects nonpositive elapsed time instead of
inventing a duration. An independent SHA-256 comparison and receiver exit status
must pass before any result is counted as successful.

Goodput is useful input bytes * 8 / measured elapsed seconds (bits/s), excluding
headers and retransmitted bytes. Empty input is allowed and has zero goodput.
Retransmissions are taken from the sender adapter, not guessed from channel
packet IDs. UDP retries and duplicate packets cannot be reliably distinguished
by a packet-independent relay.

Missing adapters, startup failures, send/receive failures, timeouts, hash
mismatches, missing/invalid metrics, queue overflow, send errors, and discarded
queued packets are retained with explicit failure status; the runner exits
nonzero when any case fails. Failed cases never receive goodput values. Child
cleanup uses only PIDs started by this script.
On a successful transfer, a per-run stop marker requests a normal draining
shutdown of the proxy; its exit status and log close must succeed before the
runner accepts the result. Signal termination is reserved for failed runs.

## Plotting

    python analysis/plot_runs.py results/run1/runs.csv --output results/run1/loss-plots --x loss
    python analysis/plot_runs.py results/run1/runs.csv --output results/run1/reorder-plots --x reorder
    python analysis/plot_runs.py results/run1/runs.csv --output results/run1/window-plots --x window

Python is used only for data validation, aggregation, plotting, and tests of that
analysis. C implements all network and channel behavior. summary.csv reports
total and verified counts, success fraction, means, and sample standard
deviations. Each plot holds unvaried channel controls and the source file/hash
fixed; protocol/window series stay separate. Error bars are sample SD across
verified runs, not confidence intervals. Single-success groups have no SD
estimate. Failures remain in success-rate denominators and are excluded from
time/goodput/retransmission means. Compare success rates alongside conditional
means to avoid favoring a protocol that only completes easy cases.

The claim that SR outperforms GBN/SW is a hypothesis until all three actual
protocols have been integrated and measured. The fixture pipeline validates
measurement plumbing only and cannot support that claim.

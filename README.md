# Reliable Data Transfer over UDP

CS-30003 · Coding Assignment 1 · P3 · Section CSE 33

This branch contains **Aayushmann Kumar Singh's (2405253)** channel emulator,
automated tests, experiment orchestration, logging, and initial analysis tools.
The protocol implementations remain on the teammates' branches.

## Build and test

On Linux with GCC, GNU Make, Bash, and GNU coreutils/time:

    make test
    make integration
    bash tests/test_runner.sh
    python -m pip install -r analysis/requirements.txt
    python -m unittest discover -s tests -p 'test_analysis.py'

The C code also builds with MinGW GCC on Windows. Run shell targets from an
MSYS2 shell with the UCRT64 compiler and /usr/bin on PATH.

## Place the channel between sender and receiver

Start the receiver on port 9001. Run:

    ./build/channel_proxy --listen-port 9000 --receiver-port 9001 \
      --seed 2026 --loss 0.1 --duplicate 0.02 --reorder 0.2 \
      --corrupt 0.01 --delay-ms 10 --jitter-ms 3 --reorder-ms 25 \
      --log channel.csv

Point the sender at **127.0.0.1:9000**, not directly at the receiver.
Replies return through the proxy automatically. A new process is required for
each sender session. The listener is loopback-only for local experiments.

## Experiments

    bash experiments/run_matrix.sh experiments/matrix.csv input.bin results/run1 experiments/adapters
    python analysis/plot_runs.py results/run1/runs.csv --output results/run1/plots --x loss

The 39-run matrix covers clean, loss, reordering, and window-size scenarios with
three seeds. The runner requires a runnable adapter for every selected protocol.
An adapter for the existing SR branch is supplied; SW/GBN need the team's UDP
sender/receiver integration first. Missing adapters produce explicit failure
rows and a nonzero exit status, never invented measurements.

To verify the experiment pipeline independently using **test fixture traffic**:

    bash experiments/run_matrix.sh tests/fixture_matrix.csv build/channel_proxy results/fixture tests/adapters
    python analysis/plot_runs.py results/fixture/runs.csv --output results/fixture/plots --x loss

On Windows use build/channel_proxy.exe as the input path. Fixture output is
reassembled at the echo client; these are plumbing checks, not SW/GBN/SR results.

See [channel design](docs/channel-emulator.md),
[experiment contract and method](docs/experiments.md), and
[branch/integration handoff](docs/aayushman-handoff.md). AI assistance is
disclosed in [AI-USE.md](AI-USE.md).

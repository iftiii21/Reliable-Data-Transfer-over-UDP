# Aayushmann's branch and integration handoff

Contribution owner: Aayushmann Kumar Singh (2405253), GitHub AAYUSHMAN1901.
Commit identity is repository-local and uses the verified GitHub numeric-ID
noreply address. Other repositories' global Git identities are unaffected.

Branch: aayushman/channel-emulator-experiments, based on main at fc38156.
Submit to main as a pull request; do not push directly to any teammate's branch.
The originally supplied CN-Project URL redirects to
iftiii21/Reliable-Data-Transfer-over-UDP.

Branches inspected:

| Branch | Inspected revision | Existing responsibility |
| --- | --- | --- |
| main | fc38156 | Initial structure |
| utkarsh | 5d63822 | Packet serialization, UDP and file I/O |
| syed-stopwait-gbn | aaafba0 | SW/GBN sender state machines, cumulative ACKs |
| indraneel | 8a7fa8e | SR sender/receiver, individual ACKs, timers and RTT |

No teammate commits are merged, copied, rebased, or re-authored in this branch.
Channel files have independent names and treat all wire bytes as opaque. The
new root Makefile builds only the channel tools and tests. During final team
integration, retain all teammates' build targets and add these targets; do not
resolve Makefile conflicts by discarding either side.

## Integration checklist

1. Merge reviewed protocol branches through the team workflow.
2. Wire Syed's state machines into runnable sender/receiver programs using the
   agreed packet/checksum/ACK contracts; provide sw.sh and gbn.sh adapters.
3. Build Indraneel's SR binaries and verify the supplied sr.sh adapter on Linux.
4. Ensure ACKs return to the source address so both directions traverse the relay.
5. Verify retries for lost ACK/FIN, receiver completion, and nonzero exits on
   failure. The current SR sender logs FIN retry exhaustion but returns success;
   its adapter explicitly requires the FIN ACK message.
6. Run make integration, then the actual 39-case protocol experiment matrix.
7. Review success rates, full hashes, raw logs, and repeatability before writing
   the comparison report. Do not relabel fixture validation as protocol results.

## Viva preparation

Be ready to explain why checksums are not recalculated after corruption, how ACK
loss differs from DATA loss, why a delayed packet needs a priority queue, why
reorder=1 can preserve ordering, and why identical seeds cannot force identical
UDP scheduling across protocols. Explain goodput versus total transmitted
payload and why SHA-256 and exit codes are both checked.

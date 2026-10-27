#!/usr/bin/env python3
"""Small localhost-only test relay that drops one selected SR datagram."""

import argparse
import socket


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--listen-port", type=int, required=True)
    parser.add_argument("--receiver-port", type=int, required=True)
    parser.add_argument("--drop", choices=("data", "ack"), required=True)
    parser.add_argument("--sequence", type=int, required=True)
    return parser.parse_args()


def main():
    args = parse_args()
    receiver = ("127.0.0.1", args.receiver_port)
    sender = None
    dropped = False

    relay = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    relay.bind(("127.0.0.1", args.listen_port))
    relay.settimeout(0.25)
    print("READY", flush=True)

    try:
        while True:
            try:
                packet, source = relay.recvfrom(2048)
            except socket.timeout:
                continue

            if len(packet) < 13:
                continue

            if source == receiver:
                if sender is None:
                    continue
                packet_type = packet[0]
                ack_number = int.from_bytes(packet[5:9], "big")
                if (args.drop == "ack" and packet_type == 2 and
                        ack_number == args.sequence):
                    if not dropped:
                        dropped = True
                        print(f"DROPPED_ACK seq={ack_number}", flush=True)
                        continue
                    print(f"FORWARDED_ACK_RETRY seq={ack_number}", flush=True)
                relay.sendto(packet, sender)
            else:
                sender = source
                packet_type = packet[0]
                sequence = int.from_bytes(packet[1:5], "big")
                if (args.drop == "data" and packet_type == 1 and
                        sequence == args.sequence):
                    if not dropped:
                        dropped = True
                        print(f"DROPPED_DATA seq={sequence}", flush=True)
                        continue
                    print(f"FORWARDED_DATA_RETRY seq={sequence}", flush=True)
                relay.sendto(packet, receiver)
    finally:
        relay.close()


if __name__ == "__main__":
    main()

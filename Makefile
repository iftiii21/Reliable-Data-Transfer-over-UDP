CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude
BUILD_DIR := build
ARQ_TEST := $(BUILD_DIR)/test_arq
PACKET_TEST := $(BUILD_DIR)/test_packet
FILE_TRANSFER_TEST := $(BUILD_DIR)/test_file_transfer
TIMER_RTT_TEST := $(BUILD_DIR)/test_timer_rtt
UDP_SENDER_TEST := $(BUILD_DIR)/udp_sender_test
UDP_RECEIVER_TEST := $(BUILD_DIR)/udp_receiver_test
SR_SENDER := $(BUILD_DIR)/sr_sender
SR_RECEIVER := $(BUILD_DIR)/sr_receiver
CHANNEL_PROXY := $(BUILD_DIR)/channel_proxy
CHANNEL_TEST := $(BUILD_DIR)/test_channel
UDP_FIXTURE := $(BUILD_DIR)/udp_fixture
ARQ_ENDPOINT := $(BUILD_DIR)/arq_endpoint

SR_COMMON := src/packet.c src/udp.c src/file_transfer.c src/rtt.c \
	src/timer.c src/stats.c src/selective_repeat.c

.PHONY: all test test_packet test_file_transfer test_timer_rtt test_udp test_sr test_sr_loss test_sr_emulator test_arq_emulator test_channel integration test_runner test_analysis clean
all: $(ARQ_TEST) $(PACKET_TEST) $(FILE_TRANSFER_TEST) $(TIMER_RTT_TEST) \
	$(UDP_SENDER_TEST) $(UDP_RECEIVER_TEST) $(SR_SENDER) $(SR_RECEIVER) \
	$(CHANNEL_PROXY) $(CHANNEL_TEST) $(UDP_FIXTURE) $(ARQ_ENDPOINT)

$(BUILD_DIR):
	mkdir -p $@

$(ARQ_TEST): src/arq.c src/packet.c tests/test_arq.c include/arq.h include/packet.h src/packet.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Isrc src/arq.c src/packet.c tests/test_arq.c -o $@

$(PACKET_TEST): src/packet.c tests/test_packet.c src/packet.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Isrc src/packet.c tests/test_packet.c -o $@

$(FILE_TRANSFER_TEST): src/packet.c src/file_transfer.c src/file_transfer_test.c src/file_transfer.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Isrc src/packet.c src/file_transfer.c src/file_transfer_test.c -o $@

$(TIMER_RTT_TEST): src/timer.c src/rtt.c tests/test_timer_rtt.c src/timer.h src/rtt.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -Isrc src/timer.c src/rtt.c tests/test_timer_rtt.c -o $@

$(UDP_SENDER_TEST): src/packet.c src/udp.c src/udp_sender_test.c src/udp.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Isrc src/packet.c src/udp.c src/udp_sender_test.c -o $@

$(UDP_RECEIVER_TEST): src/packet.c src/udp.c src/udp_receiver_test.c src/udp.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Isrc src/packet.c src/udp.c src/udp_receiver_test.c -o $@

$(SR_SENDER): $(SR_COMMON) src/sr_sender_main.c src/selective_repeat.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200112L -Isrc $(SR_COMMON) src/sr_sender_main.c -o $@ -lrt

$(SR_RECEIVER): $(SR_COMMON) src/sr_receiver_main.c src/selective_repeat.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200112L -Isrc $(SR_COMMON) src/sr_receiver_main.c -o $@ -lrt

$(CHANNEL_PROXY): src/channel.c src/channel_proxy.c include/channel.h include/net_portable.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) src/channel.c src/channel_proxy.c -o $@

$(CHANNEL_TEST): src/channel.c tests/test_channel.c include/channel.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) src/channel.c tests/test_channel.c -o $@

$(UDP_FIXTURE): tests/udp_fixture.c include/net_portable.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) tests/udp_fixture.c -o $@

$(ARQ_ENDPOINT): src/arq_endpoint.c src/arq.c src/packet.c src/udp.c src/file_transfer.c include/arq.h src/packet.h src/udp.h src/file_transfer.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -Isrc src/arq_endpoint.c src/arq.c src/packet.c src/udp.c src/file_transfer.c -o $@

test_packet: $(PACKET_TEST)
	./$(PACKET_TEST)

test_file_transfer: $(FILE_TRANSFER_TEST)
	./$(FILE_TRANSFER_TEST)

test_timer_rtt: $(TIMER_RTT_TEST)
	./$(TIMER_RTT_TEST)

test_udp: $(UDP_SENDER_TEST) $(UDP_RECEIVER_TEST)
	sh tests/test_udp_smoke.sh

test_sr: $(SR_SENDER) $(SR_RECEIVER)
	sh tests/test_sr_transfer.sh

test_sr_loss: $(SR_SENDER) $(SR_RECEIVER)
	sh tests/test_sr_loss.sh

test_sr_emulator: $(SR_SENDER) $(SR_RECEIVER) $(CHANNEL_PROXY)
	bash tests/test_sr_emulator.sh

test_arq_emulator: $(ARQ_ENDPOINT) $(CHANNEL_PROXY)
	bash tests/test_arq_emulator.sh

test_channel: $(CHANNEL_TEST)
	./$(CHANNEL_TEST)

integration: $(CHANNEL_PROXY) $(CHANNEL_TEST) $(UDP_FIXTURE)
	./$(CHANNEL_TEST)
	bash tests/integration.sh

test_runner: $(CHANNEL_PROXY) $(UDP_FIXTURE)
	bash tests/test_runner.sh

test_analysis:
	python3 -m unittest discover -s tests -p 'test_analysis.py'

test: $(ARQ_TEST) $(PACKET_TEST) $(FILE_TRANSFER_TEST) $(TIMER_RTT_TEST) \
	$(UDP_SENDER_TEST) $(UDP_RECEIVER_TEST) $(SR_SENDER) $(SR_RECEIVER) \
	$(CHANNEL_PROXY) $(CHANNEL_TEST) $(UDP_FIXTURE) $(ARQ_ENDPOINT)
	./$(ARQ_TEST)
	./$(PACKET_TEST)
	./$(FILE_TRANSFER_TEST)
	./$(TIMER_RTT_TEST)
	./$(CHANNEL_TEST)
	sh tests/test_udp_smoke.sh
	sh tests/test_sr_transfer.sh
	sh tests/test_sr_loss.sh
	bash tests/integration.sh
	bash tests/test_runner.sh
	bash tests/test_sr_emulator.sh
	bash tests/test_arq_emulator.sh

clean:
	rm -rf $(BUILD_DIR)

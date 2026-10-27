CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude
ifeq ($(OS),Windows_NT)
LDLIBS = -lws2_32
EXE = .exe
endif

.PHONY: all test integration clean
all: build/channel_proxy$(EXE) build/test_channel$(EXE) build/udp_fixture$(EXE)

build:
	mkdir -p build

build/channel_proxy$(EXE): src/channel.c src/channel_proxy.c include/channel.h include/net_portable.h | build
	$(CC) $(CFLAGS) src/channel.c src/channel_proxy.c -o $@ $(LDLIBS)

build/test_channel$(EXE): src/channel.c tests/test_channel.c include/channel.h | build
	$(CC) $(CFLAGS) src/channel.c tests/test_channel.c -o $@

build/udp_fixture$(EXE): tests/udp_fixture.c include/net_portable.h | build
	$(CC) $(CFLAGS) tests/udp_fixture.c -o $@ $(LDLIBS)

test: all
	./build/test_channel$(EXE)

integration: test
	bash tests/integration.sh

clean:
	rm -f build/channel_proxy build/channel_proxy.exe build/test_channel build/test_channel.exe build/udp_fixture build/udp_fixture.exe

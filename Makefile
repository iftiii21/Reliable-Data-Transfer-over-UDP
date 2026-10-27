CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude
BUILD_DIR := build
ARQ_TEST := $(BUILD_DIR)/test_arq

.PHONY: all test clean
all: $(ARQ_TEST)

$(BUILD_DIR):
	mkdir -p $@

$(ARQ_TEST): src/arq.c tests/test_arq.c include/arq.h include/packet.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) src/arq.c tests/test_arq.c -o $@

test: $(ARQ_TEST)
	./$(ARQ_TEST)

clean:
	rm -rf $(BUILD_DIR)

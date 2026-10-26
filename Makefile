# =============================================================================
# Makefile — Selective Repeat ARQ (Indraneel's portion)
#
# Targets:
#   all          — build sr_sender and sr_receiver
#   sr_sender    — build the SR sender binary
#   sr_receiver  — build the SR receiver binary
#   clean        — remove all build artifacts
#
# Compiler flags:
#   -Wall -Wextra  : enable broad warnings
#   -std=c99       : strict C99 compliance
#   -O2            : optimisation
#   -lpthread      : POSIX threads (for future use / portability)
#   -lrt           : POSIX real-time extensions (clock_gettime on some distros)
# =============================================================================

CC      := gcc
CFLAGS  := -Wall -Wextra -std=c99 -O2 -D_POSIX_C_SOURCE=200112L
LDFLAGS := -lpthread -lrt

SRCDIR  := src
BINDIR  := .

# ---------------------------------------------------------------------------
# Sources shared by both binaries
# ---------------------------------------------------------------------------
COMMON_SRCS := \
    $(SRCDIR)/packet.c           \
    $(SRCDIR)/udp.c              \
    $(SRCDIR)/file_transfer.c    \
    $(SRCDIR)/rtt.c              \
    $(SRCDIR)/timer.c            \
    $(SRCDIR)/stats.c            \
    $(SRCDIR)/selective_repeat.c

COMMON_OBJS := $(COMMON_SRCS:.c=.o)

# ---------------------------------------------------------------------------
# sr_sender
# ---------------------------------------------------------------------------
SENDER_SRCS := $(SRCDIR)/sr_sender_main.c
SENDER_OBJS := $(SENDER_SRCS:.c=.o)
SENDER_BIN  := $(BINDIR)/sr_sender

# ---------------------------------------------------------------------------
# sr_receiver
# ---------------------------------------------------------------------------
RECEIVER_SRCS := $(SRCDIR)/sr_receiver_main.c
RECEIVER_OBJS := $(RECEIVER_SRCS:.c=.o)
RECEIVER_BIN  := $(BINDIR)/sr_receiver

# ---------------------------------------------------------------------------
# Build rules
# ---------------------------------------------------------------------------
.PHONY: all clean

all: $(SENDER_BIN) $(RECEIVER_BIN)

$(SENDER_BIN): $(COMMON_OBJS) $(SENDER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built: $@"

$(RECEIVER_BIN): $(COMMON_OBJS) $(RECEIVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built: $@"

# Pattern rule: compile any .c to .o
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------
clean:
	rm -f $(COMMON_OBJS) $(SENDER_OBJS) $(RECEIVER_OBJS)
	rm -f $(SENDER_BIN) $(RECEIVER_BIN)
	@echo "Cleaned."

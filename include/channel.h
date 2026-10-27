#ifndef CHANNEL_H
#define CHANNEL_H

#include <stddef.h>
#include <stdint.h>

#define CHANNEL_SCALE 1000000u
#define CHANNEL_MAX_DATAGRAM 65507u
#define CHANNEL_MAX_DELAY_MS 3600000u

/* Probabilities are integer parts per million. Seeded decisions do not depend
 * on libc rand(), the wall clock, packet format, or socket scheduling. */
typedef struct {
    uint32_t loss, duplicate, reorder, corrupt;
    uint32_t delay_ms, jitter_ms, reorder_ms;
} ChannelConfig;

typedef struct { uint64_t rng; } ChannelState;

typedef struct {
    unsigned copies;
    uint64_t delay_us;
    int reordered;
    int corrupted;
    size_t corrupt_byte;
    uint8_t corrupt_mask;
} ChannelDecision;

int channel_config_valid(const ChannelConfig *config);
void channel_init(ChannelState *state, uint64_t seed);
/* Returns -1 for invalid arguments, 0 for a valid decision (copies=0 means loss).
 * Corruption affects the original and its duplicate identically. Reordering adds
 * a holding delay; actual order inversion depends on traffic spacing. */
int channel_decide(ChannelState *state, const ChannelConfig *config,
                   size_t length, ChannelDecision *decision);

#endif

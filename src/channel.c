#include "channel.h"
#include <string.h>

/* SplitMix64: fixed-width unsigned arithmetic makes seeded traces portable. */
static uint64_t next_random(ChannelState *state)
{
    uint64_t value;
    state->rng += UINT64_C(0x9e3779b97f4a7c15);
    value = state->rng;
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

static uint64_t bounded(ChannelState *state, uint64_t bound)
{
    uint64_t value, threshold = (uint64_t)(0 - bound) % bound;
    do { value = next_random(state); } while (value < threshold);
    return value % bound;
}

static int happens(ChannelState *state, uint32_t probability)
{
    return bounded(state, CHANNEL_SCALE) < probability;
}

int channel_config_valid(const ChannelConfig *config)
{
    return config && config->loss <= CHANNEL_SCALE &&
        config->duplicate <= CHANNEL_SCALE && config->reorder <= CHANNEL_SCALE &&
        config->corrupt <= CHANNEL_SCALE &&
        config->delay_ms <= CHANNEL_MAX_DELAY_MS &&
        config->jitter_ms <= CHANNEL_MAX_DELAY_MS &&
        config->reorder_ms <= CHANNEL_MAX_DELAY_MS;
}

void channel_init(ChannelState *state, uint64_t seed)
{
    if (state) state->rng = seed;
}

int channel_decide(ChannelState *state, const ChannelConfig *config,
                   size_t length, ChannelDecision *decision)
{
    int64_t delay;
    if (!state || !decision || !channel_config_valid(config) ||
        length > CHANNEL_MAX_DATAGRAM) return -1;
    memset(decision, 0, sizeof(*decision));
    if (happens(state, config->loss)) return 0;
    decision->copies = happens(state, config->duplicate) ? 2u : 1u;
    decision->reordered = happens(state, config->reorder);
    decision->corrupted = happens(state, config->corrupt) && length != 0;
    delay = (int64_t)config->delay_ms * 1000;
    if (config->jitter_ms) {
        uint64_t jitter = (uint64_t)config->jitter_ms * 1000;
        delay += (int64_t)bounded(state, 2 * jitter + 1) - (int64_t)jitter;
    }
    if (delay < 0) delay = 0;
    if (decision->reordered) delay += (int64_t)config->reorder_ms * 1000;
    decision->delay_us = (uint64_t)delay;
    if (decision->corrupted) {
        decision->corrupt_byte = (size_t)bounded(state, length);
        decision->corrupt_mask = (uint8_t)(1u << bounded(state, 8));
    }
    return 0;
}

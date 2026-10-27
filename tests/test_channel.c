#include "channel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void limits(void)
{
    ChannelConfig config = {0};
    ChannelState state;
    ChannelDecision d;
    channel_init(&state, 0);
    assert(channel_decide(&state, &config, 0, &d) == 0);
    assert(d.copies == 1 && d.delay_us == 0 && !d.corrupted);
    assert(channel_decide(NULL, &config, 1, &d) == -1);
    assert(channel_decide(&state, NULL, 1, &d) == -1);
    assert(channel_decide(&state, &config, 1, NULL) == -1);
    assert(channel_decide(&state, &config, CHANNEL_MAX_DATAGRAM + 1, &d) == -1);
    config.loss = CHANNEL_SCALE + 1;
    assert(!channel_config_valid(&config));
    config.loss = 0;
    config.delay_ms = CHANNEL_MAX_DELAY_MS + 1;
    assert(!channel_config_valid(&config));
}

static void extremes(void)
{
    ChannelConfig config = {0};
    ChannelState state;
    ChannelDecision d;
    channel_init(&state, UINT64_MAX);
    config.loss = CHANNEL_SCALE;
    assert(channel_decide(&state, &config, 100, &d) == 0 && d.copies == 0);
    config.loss = 0;
    config.duplicate = config.reorder = config.corrupt = CHANNEL_SCALE;
    config.delay_ms = 12;
    config.reorder_ms = 41;
    assert(channel_decide(&state, &config, 100, &d) == 0);
    assert(d.copies == 2 && d.reordered && d.corrupted && d.delay_us == 53000);
    assert(d.corrupt_byte < 100 && d.corrupt_mask && !(d.corrupt_mask & (d.corrupt_mask - 1)));
    assert(channel_decide(&state, &config, 0, &d) == 0 && !d.corrupted);
    config.jitter_ms = 100;
    config.delay_ms = 0;
    config.reorder = 0;
    for (int i = 0; i < 10000; i++) {
        assert(channel_decide(&state, &config, CHANNEL_MAX_DATAGRAM, &d) == 0);
        assert(d.delay_us <= 100000);
        assert(d.corrupt_byte < CHANNEL_MAX_DATAGRAM);
    }
}

static void deterministic_distribution(void)
{
    ChannelConfig config = {250000, 300000, 400000, 200000, 30, 5, 25};
    ChannelState a, b, other;
    unsigned losses = 0, duplicates = 0, corrupted = 0, reordered = 0, different = 0;
    channel_init(&a, 2026);
    channel_init(&b, 2026);
    channel_init(&other, 2027);
    for (int i = 0; i < 100000; i++) {
        ChannelDecision x, y, z;
        assert(channel_decide(&a, &config, 1024, &x) == 0);
        assert(channel_decide(&b, &config, 1024, &y) == 0);
        assert(channel_decide(&other, &config, 1024, &z) == 0);
        assert(memcmp(&x, &y, sizeof(x)) == 0);
        different += memcmp(&x, &z, sizeof(x)) != 0;
        if (!x.copies) { losses++; continue; }
        duplicates += x.copies == 2;
        corrupted += x.corrupted != 0;
        reordered += x.reordered != 0;
        assert(x.delay_us >= (x.reordered ? 50000u : 25000u));
        assert(x.delay_us <= (x.reordered ? 60000u : 35000u));
    }
    assert(different > 50000);
    assert(losses > 24000 && losses < 26000);
    assert(duplicates > 21000 && duplicates < 24000);
    assert(corrupted > 14000 && corrupted < 16000);
    assert(reordered > 28500 && reordered < 31500);
    printf("100000 seeded decisions: loss=%u duplicate=%u corrupt=%u reorder=%u\n",
           losses, duplicates, corrupted, reordered);
}

int main(void)
{
    limits();
    extremes();
    deterministic_distribution();
    puts("channel unit tests passed");
    return 0;
}

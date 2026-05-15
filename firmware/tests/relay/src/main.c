/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include <rp2350_relay_6ch/relay.h>

static void *relay_suite_setup(void)
{
	zassert_equal(relay_init(), 0, "relay_init failed");
	return NULL;
}

static void relay_before(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_equal(relay_off_all(), 0, "relay_off_all failed");
}

ZTEST_SUITE(relay, NULL, relay_suite_setup, relay_before, NULL, NULL);

ZTEST(relay, test_default_off)
{
	uint8_t state_mask = 0xffU;

	zassert_equal(relay_count(), RP2350_RELAY_6CH_CHANNEL_COUNT);
	zassert_equal(relay_get_all(&state_mask), 0);
	zassert_equal(state_mask, 0U);
}

ZTEST(relay, test_set_one)
{
	bool on = false;
	uint8_t state_mask;

	zassert_equal(relay_set(0U, true), 0);
	zassert_equal(relay_get(0U, &on), 0);
	zassert_true(on);

	zassert_equal(relay_get_all(&state_mask), 0);
	zassert_equal(state_mask, BIT(0));

	zassert_equal(relay_set(0U, false), 0);
	zassert_equal(relay_get(0U, &on), 0);
	zassert_false(on);
}

ZTEST(relay, test_set_all)
{
	uint8_t state_mask;

	zassert_equal(relay_set_all(BIT_MASK(RP2350_RELAY_6CH_CHANNEL_COUNT)), 0);
	zassert_equal(relay_get_all(&state_mask), 0);
	zassert_equal(state_mask, BIT_MASK(RP2350_RELAY_6CH_CHANNEL_COUNT));
}

ZTEST(relay, test_off_all)
{
	uint8_t state_mask;

	zassert_equal(relay_set_all(BIT_MASK(RP2350_RELAY_6CH_CHANNEL_COUNT)), 0);
	zassert_equal(relay_off_all(), 0);
	zassert_equal(relay_get_all(&state_mask), 0);
	zassert_equal(state_mask, 0U);
}

ZTEST(relay, test_invalid_channel_rejected)
{
	bool on = false;

	zassert_equal(relay_set(RP2350_RELAY_6CH_CHANNEL_COUNT, true), -EINVAL);
	zassert_equal(relay_get(RP2350_RELAY_6CH_CHANNEL_COUNT, &on), -EINVAL);
}

ZTEST(relay, test_invalid_state_mask_rejected)
{
	zassert_equal(relay_set_all(BIT(RP2350_RELAY_6CH_CHANNEL_COUNT)), -EINVAL);
}

ZTEST(relay, test_null_outputs_rejected)
{
	zassert_equal(relay_get(0U, NULL), -EINVAL);
	zassert_equal(relay_get_all(NULL), -EINVAL);
}

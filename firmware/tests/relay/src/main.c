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

static void relay_after(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_equal(relay_off_all(), 0, "relay_off_all failed");
}

ZTEST_SUITE(relay, NULL, relay_suite_setup, relay_before, relay_after, NULL);

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
	zassert_equal(relay_is_pulsing(0U, NULL), -EINVAL);
}

ZTEST(relay, test_pulse_turns_on_then_off)
{
	bool on = false;
	bool pulsing = false;

	zassert_equal(relay_pulse(0U, RP2350_RELAY_6CH_PULSE_MIN_MS), 0);
	zassert_equal(relay_get(0U, &on), 0);
	zassert_true(on);
	zassert_equal(relay_is_pulsing(0U, &pulsing), 0);
	zassert_true(pulsing);

	k_msleep(RP2350_RELAY_6CH_PULSE_MIN_MS + 10U);

	zassert_equal(relay_get(0U, &on), 0);
	zassert_false(on);
	zassert_equal(relay_is_pulsing(0U, &pulsing), 0);
	zassert_false(pulsing);
}

ZTEST(relay, test_pulse_duration_bounds)
{
	zassert_equal(relay_pulse(0U, RP2350_RELAY_6CH_PULSE_MIN_MS - 1U), -EINVAL);
	zassert_equal(relay_pulse(0U, RP2350_RELAY_6CH_PULSE_MAX_MS + 1U), -EINVAL);
	zassert_equal(relay_pulse(0U, RP2350_RELAY_6CH_PULSE_MIN_MS), 0);
}

ZTEST(relay, test_pulse_invalid_channel_rejected)
{
	bool pulsing = false;

	zassert_equal(relay_pulse(RP2350_RELAY_6CH_CHANNEL_COUNT,
				  RP2350_RELAY_6CH_PULSE_MIN_MS),
		      -EINVAL);
	zassert_equal(relay_is_pulsing(RP2350_RELAY_6CH_CHANNEL_COUNT, &pulsing),
		      -EINVAL);
}

ZTEST(relay, test_pulse_rejects_busy_relay)
{
	zassert_equal(relay_pulse(0U, 100U), 0);
	zassert_equal(relay_pulse(0U, 100U), -EBUSY);
}

ZTEST(relay, test_off_all_cancels_pulse)
{
	bool on = false;
	bool pulsing = false;

	zassert_equal(relay_pulse(0U, 100U), 0);
	zassert_equal(relay_off_all(), 0);

	zassert_equal(relay_get(0U, &on), 0);
	zassert_false(on);
	zassert_equal(relay_is_pulsing(0U, &pulsing), 0);
	zassert_false(pulsing);

	k_msleep(110U);

	zassert_equal(relay_get(0U, &on), 0);
	zassert_false(on);
}

ZTEST(relay, test_direct_set_cancels_pulse_timeout)
{
	bool on = false;
	bool pulsing = false;

	zassert_equal(relay_pulse(0U, 100U), 0);
	zassert_equal(relay_set(0U, true), 0);
	zassert_equal(relay_is_pulsing(0U, &pulsing), 0);
	zassert_false(pulsing);

	k_msleep(110U);

	zassert_equal(relay_get(0U, &on), 0);
	zassert_true(on);
}

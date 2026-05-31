/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include <rp2350_relay_6ch/indicator.h>
#include <rp2350_relay_6ch/relay.h>

LOG_MODULE_REGISTER(rp2350_relay, LOG_LEVEL_INF);

#define RELAYS_NODE DT_PATH(relays)
#ifdef CONFIG_ZTEST
#define COMM_LOSS_REBOOT_PENDING_INDICATION_MS 40U
#else
#define COMM_LOSS_REBOOT_PENDING_INDICATION_MS 10000U
#endif

BUILD_ASSERT(DT_NODE_EXISTS(RELAYS_NODE), "Missing /relays devicetree node");
BUILD_ASSERT(DT_CHILD_NUM(RELAYS_NODE) == RP2350_RELAY_6CH_CHANNEL_COUNT,
	     "/relays must define exactly six relay channel child nodes");

static const struct gpio_dt_spec relays[] = {
	GPIO_DT_SPEC_GET(DT_CHILD(RELAYS_NODE, ch1), gpios),
	GPIO_DT_SPEC_GET(DT_CHILD(RELAYS_NODE, ch2), gpios),
	GPIO_DT_SPEC_GET(DT_CHILD(RELAYS_NODE, ch3), gpios),
	GPIO_DT_SPEC_GET(DT_CHILD(RELAYS_NODE, ch4), gpios),
	GPIO_DT_SPEC_GET(DT_CHILD(RELAYS_NODE, ch5), gpios),
	GPIO_DT_SPEC_GET(DT_CHILD(RELAYS_NODE, ch6), gpios),
};

static struct k_mutex relay_lock;
static uint8_t relay_state_mask;
static uint8_t relay_pulse_mask;
static bool comm_loss_armed;
static bool comm_loss_reboot_scheduled;
static bool comm_loss_reboot_pending_indication_scheduled;

struct relay_pulse_work {
	struct k_work_delayable work;
	uint8_t channel;
	uint32_t duration_ms;
};

static void pulse_expired(struct k_work *work);
static void comm_loss_expired(struct k_work *work);
static void comm_loss_reboot_pending_indication_expired(struct k_work *work);
static void comm_loss_reboot_expired(struct k_work *work);

static struct relay_pulse_work relay_pulses[] = {
	{ .channel = 0U },
	{ .channel = 1U },
	{ .channel = 2U },
	{ .channel = 3U },
	{ .channel = 4U },
	{ .channel = 5U },
};

static K_WORK_DELAYABLE_DEFINE(comm_loss_work, comm_loss_expired);
static K_WORK_DELAYABLE_DEFINE(comm_loss_reboot_pending_indication_work,
			       comm_loss_reboot_pending_indication_expired);
static K_WORK_DELAYABLE_DEFINE(comm_loss_reboot_work, comm_loss_reboot_expired);

static bool channel_valid(uint8_t channel)
{
	return channel < ARRAY_SIZE(relays);
}

static void publish_indicator_state(
	uint8_t state_mask, uint8_t pulse_mask,
	const struct indicator_pulse_timing pulse_timing[RP2350_RELAY_6CH_CHANNEL_COUNT])
{
	if (pulse_timing == NULL) {
		indicator_set_relay_state(state_mask, pulse_mask);
		return;
	}

	indicator_set_relay_timed_state(state_mask, pulse_mask, pulse_timing);
}

static void snapshot_pulse_timing_locked(
	uint8_t pulse_mask,
	struct indicator_pulse_timing pulse_timing[RP2350_RELAY_6CH_CHANNEL_COUNT])
{
	for (size_t i = 0; i < ARRAY_SIZE(relay_pulses); i++) {
		if ((pulse_mask & BIT(i)) == 0U) {
			pulse_timing[i].duration_ms = 0U;
			pulse_timing[i].remaining_ms = 0U;
			continue;
		}

		pulse_timing[i].duration_ms = relay_pulses[i].duration_ms;
		pulse_timing[i].remaining_ms =
			k_ticks_to_ms_ceil32(
				k_work_delayable_remaining_get(&relay_pulses[i].work));
	}
}

static int apply_state_locked(uint8_t state_mask)
{
	for (size_t i = 0; i < ARRAY_SIZE(relays); i++) {
		const bool on = (state_mask & BIT(i)) != 0U;
		int ret = gpio_pin_set_dt(&relays[i], on ? 1 : 0);

		if (ret < 0) {
			LOG_ERR("Failed to set relay %u: %d", (unsigned int)i + 1U, ret);
			return ret;
		}
	}

	relay_state_mask = state_mask & BIT_MASK(ARRAY_SIZE(relays));
	return 0;
}

static bool comm_loss_timeout_enabled(void)
{
	return !IS_ENABLED(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_NO_TIMEOUT);
}

static bool comm_loss_should_arm_locked(void)
{
	if (!comm_loss_timeout_enabled()) {
		return false;
	}

	if (IS_ENABLED(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_ALWAYS_ON_OWNER)) {
		return true;
	}

	return (relay_state_mask != 0U) || (relay_pulse_mask != 0U);
}

static void comm_loss_schedule_locked(void)
{
	if (!comm_loss_should_arm_locked()) {
		comm_loss_armed = false;
		(void)k_work_cancel_delayable(&comm_loss_work);
		return;
	}

	comm_loss_armed = true;
	(void)k_work_reschedule(
		&comm_loss_work, K_MSEC(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS));
}

static void comm_loss_disarm_if_idle_locked(void)
{
	if (IS_ENABLED(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_ENERGIZED_ONLY) &&
	    relay_state_mask == 0U && relay_pulse_mask == 0U) {
		comm_loss_armed = false;
		(void)k_work_cancel_delayable(&comm_loss_work);
	}
}

static bool comm_loss_cancel_reboot_locked(void)
{
	bool was_scheduled = comm_loss_reboot_scheduled;

	comm_loss_reboot_scheduled = false;
	comm_loss_reboot_pending_indication_scheduled = false;
	(void)k_work_cancel_delayable(&comm_loss_reboot_pending_indication_work);
	(void)k_work_cancel_delayable(&comm_loss_reboot_work);

	return was_scheduled;
}

static void comm_loss_schedule_reboot_pending_indication(void)
{
	uint32_t delay_ms = relay_comm_loss_reboot_delay_ms();

	if (delay_ms <= COMM_LOSS_REBOOT_PENDING_INDICATION_MS) {
		indicator_set_comm_loss_reboot_pending(true);
		return;
	}

	(void)k_work_schedule(&comm_loss_reboot_pending_indication_work,
			      K_MSEC(delay_ms -
				     COMM_LOSS_REBOOT_PENDING_INDICATION_MS));
}

static void cancel_pulses_locked(uint8_t pulse_mask)
{
	for (size_t i = 0; i < ARRAY_SIZE(relay_pulses); i++) {
		if ((pulse_mask & BIT(i)) != 0U) {
			(void)k_work_cancel_delayable(&relay_pulses[i].work);
			relay_pulses[i].duration_ms = 0U;
		}
	}

	relay_pulse_mask &= (uint8_t)~pulse_mask;
}

static int set_channel_locked(uint8_t channel, bool on)
{
	int ret;
	uint8_t next_state = relay_state_mask;

	if (on) {
		next_state |= BIT(channel);
	} else {
		next_state &= (uint8_t)~BIT(channel);
	}

	ret = gpio_pin_set_dt(&relays[channel], on ? 1 : 0);
	if (ret == 0) {
		relay_state_mask = next_state;
	} else {
		LOG_ERR("Failed to set relay %u: %d", (unsigned int)channel + 1U, ret);
	}

	return ret;
}

static void pulse_expired(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct relay_pulse_work *pulse =
		CONTAINER_OF(dwork, struct relay_pulse_work, work);
	uint8_t state_mask = 0U;
	uint8_t pulse_mask = 0U;
	struct indicator_pulse_timing pulse_timing[RP2350_RELAY_6CH_CHANNEL_COUNT];
	bool publish = false;
	int ret;

	k_mutex_lock(&relay_lock, K_FOREVER);
	if ((relay_pulse_mask & BIT(pulse->channel)) == 0U) {
		k_mutex_unlock(&relay_lock);
		return;
	}

	relay_pulse_mask &= (uint8_t)~BIT(pulse->channel);
	pulse->duration_ms = 0U;
	ret = set_channel_locked(pulse->channel, false);
	if (ret == 0) {
		state_mask = relay_state_mask;
		pulse_mask = relay_pulse_mask;
		snapshot_pulse_timing_locked(pulse_mask, pulse_timing);
		comm_loss_disarm_if_idle_locked();
		publish = true;
	}
	k_mutex_unlock(&relay_lock);

	if (publish) {
		publish_indicator_state(state_mask, pulse_mask, pulse_timing);
	}

	if (ret < 0) {
		LOG_ERR("Failed to end relay %u pulse: %d",
			(unsigned int)pulse->channel + 1U, ret);
	}
}

static void comm_loss_expired(struct k_work *work)
{
	uint8_t state_mask = 0U;
	uint8_t pulse_mask = 0U;
	struct indicator_pulse_timing pulse_timing[RP2350_RELAY_6CH_CHANNEL_COUNT];
	bool publish = false;
	bool owner_lost = false;
	bool reboot_pending = false;
	int ret = 0;

	ARG_UNUSED(work);

	k_mutex_lock(&relay_lock, K_FOREVER);
	if (!comm_loss_armed) {
		k_mutex_unlock(&relay_lock);
		return;
	}

	cancel_pulses_locked(BIT_MASK(ARRAY_SIZE(relay_pulses)));
	ret = apply_state_locked(0U);
	if (ret == 0) {
		state_mask = relay_state_mask;
		pulse_mask = relay_pulse_mask;
		snapshot_pulse_timing_locked(pulse_mask, pulse_timing);
		if (IS_ENABLED(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_ALWAYS_ON_OWNER)) {
			owner_lost = true;
			if (relay_comm_loss_reboot_on_timeout()) {
				comm_loss_armed = false;
				comm_loss_reboot_scheduled = true;
				comm_loss_reboot_pending_indication_scheduled = true;
				reboot_pending = true;
			} else {
				comm_loss_schedule_locked();
			}
		} else {
			comm_loss_armed = false;
		}
		publish = true;
	}
	k_mutex_unlock(&relay_lock);

	if (publish) {
		publish_indicator_state(state_mask, pulse_mask, pulse_timing);
	}

	if (owner_lost) {
		indicator_set_owner_lost(true);
	}

	if (reboot_pending) {
		comm_loss_schedule_reboot_pending_indication();
		(void)k_work_schedule(&comm_loss_reboot_work,
				      K_MSEC(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_REBOOT_DELAY_MS));
	}

	if (ret < 0) {
		LOG_ERR("Communication-loss off-all failed: %d", ret);
	}
}

static void comm_loss_reboot_pending_indication_expired(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&relay_lock, K_FOREVER);
	if (!comm_loss_reboot_pending_indication_scheduled ||
	    !comm_loss_reboot_scheduled) {
		k_mutex_unlock(&relay_lock);
		return;
	}

	comm_loss_reboot_pending_indication_scheduled = false;
	k_mutex_unlock(&relay_lock);
	indicator_set_comm_loss_reboot_pending(true);
}

static void comm_loss_reboot_expired(struct k_work *work)
{
	ARG_UNUSED(work);

#ifdef CONFIG_ZTEST
	return;
#elif defined(CONFIG_REBOOT)
	sys_reboot(SYS_REBOOT_COLD);
#else
	return;
#endif
}

int relay_init(void)
{
	k_mutex_init(&relay_lock);

	for (size_t i = 0; i < ARRAY_SIZE(relay_pulses); i++) {
		k_work_init_delayable(&relay_pulses[i].work, pulse_expired);
	}

	for (size_t i = 0; i < ARRAY_SIZE(relays); i++) {
		int ret;

		if (!gpio_is_ready_dt(&relays[i])) {
			LOG_ERR("Relay %u GPIO device is not ready", (unsigned int)i + 1U);
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&relays[i], GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure relay %u GPIO: %d",
				(unsigned int)i + 1U, ret);
			return ret;
		}
	}

	relay_state_mask = 0U;
	relay_pulse_mask = 0U;
	comm_loss_armed = false;
	comm_loss_reboot_scheduled = false;
	comm_loss_reboot_pending_indication_scheduled = false;
	(void)k_work_cancel_delayable(&comm_loss_work);
	(void)k_work_cancel_delayable(&comm_loss_reboot_pending_indication_work);
	(void)k_work_cancel_delayable(&comm_loss_reboot_work);
	indicator_set_owner_lost(false);
	indicator_set_reboot_pending(false);
	indicator_set_comm_loss_reboot_pending(false);
	if (IS_ENABLED(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_ALWAYS_ON_OWNER)) {
		k_mutex_lock(&relay_lock, K_FOREVER);
		comm_loss_schedule_locked();
		k_mutex_unlock(&relay_lock);
	}
	publish_indicator_state(relay_state_mask, relay_pulse_mask, NULL);
	LOG_INF("Initialized %u relays off", (unsigned int)ARRAY_SIZE(relays));

	return 0;
}

int relay_count(void)
{
	return ARRAY_SIZE(relays);
}

int relay_get(uint8_t channel, bool *on)
{
	if (on == NULL) {
		return -EINVAL;
	}

	if (!channel_valid(channel)) {
		return -EINVAL;
	}

	k_mutex_lock(&relay_lock, K_FOREVER);
	*on = (relay_state_mask & BIT(channel)) != 0U;
	k_mutex_unlock(&relay_lock);

	return 0;
}

int relay_get_all(uint8_t *state_mask)
{
	if (state_mask == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&relay_lock, K_FOREVER);
	*state_mask = relay_state_mask;
	k_mutex_unlock(&relay_lock);

	return 0;
}

int relay_is_pulsing(uint8_t channel, bool *pulsing)
{
	if (pulsing == NULL) {
		return -EINVAL;
	}

	if (!channel_valid(channel)) {
		return -EINVAL;
	}

	k_mutex_lock(&relay_lock, K_FOREVER);
	*pulsing = (relay_pulse_mask & BIT(channel)) != 0U;
	k_mutex_unlock(&relay_lock);

	return 0;
}

int relay_set(uint8_t channel, bool on)
{
	uint8_t state_mask = 0U;
	uint8_t pulse_mask = 0U;
	struct indicator_pulse_timing pulse_timing[RP2350_RELAY_6CH_CHANNEL_COUNT];
	bool reboot_canceled = false;
	int ret;

	if (!channel_valid(channel)) {
		return -EINVAL;
	}

	k_mutex_lock(&relay_lock, K_FOREVER);
	cancel_pulses_locked(BIT(channel));
	ret = set_channel_locked(channel, on);
	if (ret == 0) {
		state_mask = relay_state_mask;
		pulse_mask = relay_pulse_mask;
		snapshot_pulse_timing_locked(pulse_mask, pulse_timing);
		reboot_canceled = comm_loss_cancel_reboot_locked();
		comm_loss_schedule_locked();
	}
	k_mutex_unlock(&relay_lock);

	if (ret == 0) {
		publish_indicator_state(state_mask, pulse_mask, pulse_timing);
		indicator_set_owner_lost(false);
		if (reboot_canceled) {
			indicator_set_comm_loss_reboot_pending(false);
		}
	}

	return ret;
}

int relay_set_all(uint8_t state_mask)
{
	uint8_t next_state_mask = 0U;
	uint8_t pulse_mask = 0U;
	struct indicator_pulse_timing pulse_timing[RP2350_RELAY_6CH_CHANNEL_COUNT];
	bool reboot_canceled = false;
	int ret;

	if ((state_mask & ~BIT_MASK(ARRAY_SIZE(relays))) != 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&relay_lock, K_FOREVER);
	cancel_pulses_locked(BIT_MASK(ARRAY_SIZE(relays)));
	ret = apply_state_locked(state_mask);
	if (ret == 0) {
		next_state_mask = relay_state_mask;
		pulse_mask = relay_pulse_mask;
		snapshot_pulse_timing_locked(pulse_mask, pulse_timing);
		reboot_canceled = comm_loss_cancel_reboot_locked();
		comm_loss_schedule_locked();
	}
	k_mutex_unlock(&relay_lock);

	if (ret == 0) {
		publish_indicator_state(next_state_mask, pulse_mask, pulse_timing);
		indicator_set_owner_lost(false);
		if (reboot_canceled) {
			indicator_set_comm_loss_reboot_pending(false);
		}
	}

	return ret;
}

int relay_off_all(void)
{
	return relay_set_all(0U);
}

int relay_pulse(uint8_t channel, uint32_t duration_ms)
{
	uint8_t state_mask = 0U;
	uint8_t pulse_mask = 0U;
	struct indicator_pulse_timing pulse_timing[RP2350_RELAY_6CH_CHANNEL_COUNT];
	bool reboot_canceled = false;
	int ret;

	if (!channel_valid(channel)) {
		return -EINVAL;
	}

	if (duration_ms < RP2350_RELAY_6CH_PULSE_MIN_MS ||
	    duration_ms > RP2350_RELAY_6CH_PULSE_MAX_MS) {
		return -EINVAL;
	}

	k_mutex_lock(&relay_lock, K_FOREVER);
	if ((relay_pulse_mask & BIT(channel)) != 0U) {
		k_mutex_unlock(&relay_lock);
		return -EBUSY;
	}

	ret = set_channel_locked(channel, true);
	if (ret == 0) {
		relay_pulse_mask |= BIT(channel);
		relay_pulses[channel].duration_ms = duration_ms;
		ret = k_work_schedule(&relay_pulses[channel].work, K_MSEC(duration_ms));
		if (ret < 0) {
			relay_pulse_mask &= (uint8_t)~BIT(channel);
			relay_pulses[channel].duration_ms = 0U;
			(void)set_channel_locked(channel, false);
		} else {
			state_mask = relay_state_mask;
			pulse_mask = relay_pulse_mask;
			snapshot_pulse_timing_locked(pulse_mask, pulse_timing);
			reboot_canceled = comm_loss_cancel_reboot_locked();
			comm_loss_schedule_locked();
		}
	}
	k_mutex_unlock(&relay_lock);

	if (ret >= 0) {
		publish_indicator_state(state_mask, pulse_mask, pulse_timing);
		indicator_set_owner_lost(false);
		if (reboot_canceled) {
			indicator_set_comm_loss_reboot_pending(false);
		}
	}

	return ret < 0 ? ret : 0;
}

void relay_comm_loss_renew(void)
{
	bool reboot_canceled;

	k_mutex_lock(&relay_lock, K_FOREVER);
	reboot_canceled = comm_loss_cancel_reboot_locked();
	comm_loss_schedule_locked();
	k_mutex_unlock(&relay_lock);
	indicator_set_owner_lost(false);
	if (reboot_canceled) {
		indicator_set_comm_loss_reboot_pending(false);
	}
}

const char *relay_comm_loss_policy(void)
{
	if (IS_ENABLED(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_NO_TIMEOUT)) {
		return "no-comm-timeout";
	}

	if (IS_ENABLED(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_ALWAYS_ON_OWNER)) {
		return "always-on-owner";
	}

	return "energized-only";
}

uint32_t relay_comm_loss_timeout_ms(void)
{
	if (!comm_loss_timeout_enabled()) {
		return 0U;
	}

	return CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS;
}

bool relay_comm_loss_reboot_on_timeout(void)
{
	return IS_ENABLED(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_ALWAYS_ON_OWNER_REBOOT_ON_TIMEOUT);
}

uint32_t relay_comm_loss_reboot_delay_ms(void)
{
	if (!relay_comm_loss_reboot_on_timeout()) {
		return 0U;
	}

	return CONFIG_RP2350_RELAY_6CH_COMM_LOSS_REBOOT_DELAY_MS;
}

#ifdef CONFIG_ZTEST
bool relay_comm_loss_test_reboot_scheduled(void)
{
	return comm_loss_reboot_scheduled;
}

uint32_t relay_comm_loss_test_reboot_remaining_ms(void)
{
	if (!comm_loss_reboot_scheduled) {
		return 0U;
	}

	return k_ticks_to_ms_ceil32(k_work_delayable_remaining_get(&comm_loss_reboot_work));
}

bool relay_comm_loss_test_reboot_pending_indication_scheduled(void)
{
	return comm_loss_reboot_pending_indication_scheduled;
}
#endif

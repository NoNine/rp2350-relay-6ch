/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <rp2350_relay_6ch/relay.h>

LOG_MODULE_REGISTER(rp2350_relay, LOG_LEVEL_INF);

#define RELAYS_NODE DT_PATH(relays)

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

static bool channel_valid(uint8_t channel)
{
	return channel < ARRAY_SIZE(relays);
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

int relay_init(void)
{
	k_mutex_init(&relay_lock);

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

int relay_set(uint8_t channel, bool on)
{
	int ret;
	uint8_t next_state;

	if (!channel_valid(channel)) {
		return -EINVAL;
	}

	k_mutex_lock(&relay_lock, K_FOREVER);
	next_state = relay_state_mask;
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
	k_mutex_unlock(&relay_lock);

	return ret;
}

int relay_set_all(uint8_t state_mask)
{
	int ret;

	if ((state_mask & ~BIT_MASK(ARRAY_SIZE(relays))) != 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&relay_lock, K_FOREVER);
	ret = apply_state_locked(state_mask);
	k_mutex_unlock(&relay_lock);

	return ret;
}

int relay_off_all(void)
{
	return relay_set_all(0U);
}

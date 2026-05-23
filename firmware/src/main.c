/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <rp2350_relay_6ch/indicator.h>
#include <rp2350_relay_6ch/relay.h>

LOG_MODULE_REGISTER(rp2350_relay_6ch, LOG_LEVEL_INF);

int main(void)
{
	int ret;

	indicator_init();

	ret = relay_init();

	if (ret < 0) {
		LOG_ERR("Relay initialization failed: %d", ret);
		indicator_set_fault(true);
		return ret;
	}

	indicator_set_relay_state(0U, 0U);
	indicator_set_ready(true);
	LOG_INF("RP2350 relay controller baseline started on %s", CONFIG_BOARD_TARGET);

	return 0;
}

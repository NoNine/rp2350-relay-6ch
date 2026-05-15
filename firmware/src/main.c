/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <rp2350_relay_6ch/relay.h>

LOG_MODULE_REGISTER(rp2350_relay_6ch, LOG_LEVEL_INF);

int main(void)
{
	int ret = relay_init();

	if (ret < 0) {
		LOG_ERR("Relay initialization failed: %d", ret);
		return ret;
	}

	LOG_INF("RP2350 relay controller baseline started on %s", CONFIG_BOARD_TARGET);

	return 0;
}

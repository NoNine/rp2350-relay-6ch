/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <rp2350_relay_6ch/health.h>
#include <rp2350_relay_6ch/indicator.h>
#include <rp2350_relay_6ch/relay.h>
#include <rp2350_relay_6ch/relay_mgmt.h>

LOG_MODULE_REGISTER(rp2350_relay_6ch, LOG_LEVEL_INF);

int main(void)
{
	int ret;
	struct health_snapshot snapshot;

	health_init();
	indicator_init();

	ret = relay_init();

	if (ret < 0) {
		LOG_ERR("Relay initialization failed: %d", ret);
		health_record_relay_gpio_init_failed();
		health_snapshot(&snapshot);
		indicator_set_health_snapshot(&snapshot);
		return ret;
	}

	health_set_relay_gpio_ready(true);
	relay_mgmt_publish_health();
	health_snapshot(&snapshot);
	indicator_set_health_snapshot(&snapshot);
	LOG_INF("RP2350 relay controller baseline started on %s", CONFIG_BOARD_TARGET);

	return 0;
}

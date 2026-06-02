/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <rp2350_relay_6ch/health.h>
#include <rp2350_relay_6ch/indicator.h>
#include <rp2350_relay_6ch/relay.h>
#include <rp2350_relay_6ch/relay_mgmt.h>
#ifdef CONFIG_RP2350_RELAY_6CH_WATCHDOG
#include <rp2350_relay_6ch/watchdog_supervisor.h>
#endif

LOG_MODULE_REGISTER(rp2350_relay_6ch, LOG_LEVEL_INF);

int main(void)
{
	int ret;

	health_init();
	indicator_init();

	ret = relay_init();

	if (ret < 0) {
		LOG_ERR("Relay initialization failed: %d", ret);
		health_record_relay_gpio_init_failed();
		indicator_publish_health_snapshot();
		return ret;
	}

	health_set_relay_gpio_ready(true);
	relay_mgmt_publish_health();
#ifdef CONFIG_RP2350_RELAY_6CH_WATCHDOG
	watchdog_supervisor_start();
#endif
	indicator_publish_health_snapshot();
	LOG_INF("RP2350 relay controller baseline started on %s", CONFIG_BOARD_TARGET);

	return 0;
}

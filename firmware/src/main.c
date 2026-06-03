/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include <rp2350_relay_6ch/health.h>
#include <rp2350_relay_6ch/indicator.h>
#include <rp2350_relay_6ch/relay.h>
#include <rp2350_relay_6ch/relay_mgmt.h>
#ifdef CONFIG_RP2350_RELAY_6CH_WATCHDOG
#include <rp2350_relay_6ch/watchdog_supervisor.h>
#endif

LOG_MODULE_REGISTER(rp2350_relay_6ch, LOG_LEVEL_INF);

#define STARTUP_REBOOT_DELAY_MS 1000U

static void handle_relay_init_failure(int ret)
{
	LOG_ERR("Relay initialization failed: %d", ret);
	health_record_relay_gpio_init_failed();
	indicator_init();
	indicator_publish_health_snapshot();
	k_sleep(K_MSEC(STARTUP_REBOOT_DELAY_MS));

#ifdef CONFIG_REBOOT
	sys_reboot(SYS_REBOOT_COLD);
#endif

	for (;;) {
		k_sleep(K_FOREVER);
	}
}

int main(void)
{
	int ret;

	health_init();

	ret = relay_init();

	if (ret < 0) {
		handle_relay_init_failure(ret);
	}

	health_set_relay_gpio_ready(true);
	indicator_init();
	relay_mgmt_publish_health();
#ifdef CONFIG_RP2350_RELAY_6CH_WATCHDOG
	watchdog_supervisor_start();
#endif
	indicator_publish_health_snapshot();
	LOG_INF("RP2350 relay controller baseline started on %s", CONFIG_BOARD_TARGET);

	return 0;
}

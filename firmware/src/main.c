/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rp2350_relay_6ch, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("RP2350 relay controller baseline started on %s", CONFIG_BOARD_TARGET);

	return 0;
}

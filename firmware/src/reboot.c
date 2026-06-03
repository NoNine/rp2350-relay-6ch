/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
#include <zephyr/drivers/usb/udc.h>
#endif

#include <rp2350_relay_6ch/reboot.h>
#ifdef CONFIG_ZTEST
#include <rp2350_relay_6ch_test/reboot.h>
#endif

LOG_MODULE_REGISTER(rp2350_reboot, LOG_LEVEL_INF);

#define REBOOT_USB_DISCONNECT_SETTLE_MS 100

#if defined(CONFIG_USB_DEVICE_STACK_NEXT) && \
	DT_NODE_HAS_STATUS(DT_NODELABEL(zephyr_udc0), okay)
#define REBOOT_USB_DISCONNECT_AVAILABLE 1
#else
#define REBOOT_USB_DISCONNECT_AVAILABLE 0
#endif

#ifdef CONFIG_ZTEST
static int test_usb_disconnect_result;
static unsigned int test_reboot_sequence;
static unsigned int test_usb_disconnect_attempts;
static unsigned int test_usb_disconnect_settles;
static unsigned int test_usb_disconnect_order;
static unsigned int test_reboot_order;
#endif

static int reboot_disconnect_usb(void)
{
#ifdef CONFIG_ZTEST
	test_usb_disconnect_attempts++;
	test_usb_disconnect_order = ++test_reboot_sequence;
	return test_usb_disconnect_result;
#elif REBOOT_USB_DISCONNECT_AVAILABLE
	const struct device *udc = DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0));
	int ret;

	if (!device_is_ready(udc)) {
		LOG_WRN("USB device controller is not ready before reboot");
		return -ENODEV;
	}

	ret = udc_disable(udc);
	if (ret == 0 || ret == -EALREADY) {
		LOG_INF("USB device controller detached before reboot");
		return 0;
	}

	LOG_WRN("USB device controller detach failed before reboot: %d", ret);
	return ret;
#else
	LOG_DBG("USB device controller detach is not available before reboot");
	return -ENOTSUP;
#endif
}

void reboot_usb_disconnect_and_settle(void)
{
	(void)reboot_disconnect_usb();
#ifdef CONFIG_ZTEST
	test_usb_disconnect_settles++;
#else
	k_msleep(REBOOT_USB_DISCONNECT_SETTLE_MS);
#endif
}

#ifdef CONFIG_ZTEST
void reboot_test_reset(void)
{
	test_usb_disconnect_result = 0;
	test_reboot_sequence = 0U;
	test_usb_disconnect_attempts = 0U;
	test_usb_disconnect_settles = 0U;
	test_usb_disconnect_order = 0U;
	test_reboot_order = 0U;
}

void reboot_test_force_usb_disconnect_result(int result)
{
	test_usb_disconnect_result = result;
}

void reboot_test_record_reboot(void)
{
	test_reboot_order = ++test_reboot_sequence;
}

unsigned int reboot_test_usb_disconnect_attempts(void)
{
	return test_usb_disconnect_attempts;
}

unsigned int reboot_test_usb_disconnect_settles(void)
{
	return test_usb_disconnect_settles;
}

unsigned int reboot_test_usb_disconnect_order(void)
{
	return test_usb_disconnect_order;
}

unsigned int reboot_test_reboot_order(void)
{
	return test_reboot_order;
}
#endif

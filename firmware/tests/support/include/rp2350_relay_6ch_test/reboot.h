/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RP2350_RELAY_6CH_TEST_REBOOT_H_
#define RP2350_RELAY_6CH_TEST_REBOOT_H_

#ifdef __cplusplus
extern "C" {
#endif

void reboot_test_reset(void);
void reboot_test_force_usb_disconnect_result(int result);
void reboot_test_record_reboot(void);
unsigned int reboot_test_usb_disconnect_attempts(void);
unsigned int reboot_test_usb_disconnect_settles(void);
unsigned int reboot_test_usb_disconnect_order(void);
unsigned int reboot_test_reboot_order(void);

#ifdef __cplusplus
}
#endif

#endif /* RP2350_RELAY_6CH_TEST_REBOOT_H_ */

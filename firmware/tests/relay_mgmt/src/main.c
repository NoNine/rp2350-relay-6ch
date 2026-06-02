/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <errno.h>

#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/ztest.h>

#include <mgmt/mcumgr/util/zcbor_bulk.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include <rp2350_relay_6ch/health.h>
#include <rp2350_relay_6ch/indicator.h>
#include <rp2350_relay_6ch/relay.h>
#include <rp2350_relay_6ch/relay_mgmt.h>
#include <rp2350_relay_6ch/watchdog_supervisor.h>

#define TEST_BUF_SIZE 512U

static uint8_t request_buf[TEST_BUF_SIZE];
static uint8_t response_buf[TEST_BUF_SIZE];

static size_t encode_empty_request(void)
{
	zcbor_state_t zse[2];

	zcbor_new_encode_state(zse, ARRAY_SIZE(zse), request_buf, sizeof(request_buf), 0);
	zassert_true(zcbor_map_start_encode(zse, 1));
	zassert_true(zcbor_map_end_encode(zse, 1));

	return (size_t)(zse[0].payload - request_buf);
}

static size_t encode_channel_request(uint32_t channel)
{
	zcbor_state_t zse[2];

	zcbor_new_encode_state(zse, ARRAY_SIZE(zse), request_buf, sizeof(request_buf), 0);
	zassert_true(zcbor_map_start_encode(zse, 1));
	zassert_true(zcbor_tstr_put_lit(zse, "channel"));
	zassert_true(zcbor_uint32_put(zse, channel));
	zassert_true(zcbor_map_end_encode(zse, 1));

	return (size_t)(zse[0].payload - request_buf);
}

static size_t encode_set_request(uint32_t channel, bool on)
{
	zcbor_state_t zse[2];

	zcbor_new_encode_state(zse, ARRAY_SIZE(zse), request_buf, sizeof(request_buf), 0);
	zassert_true(zcbor_map_start_encode(zse, 2));
	zassert_true(zcbor_tstr_put_lit(zse, "channel"));
	zassert_true(zcbor_uint32_put(zse, channel));
	zassert_true(zcbor_tstr_put_lit(zse, "on"));
	zassert_true(zcbor_bool_put(zse, on));
	zassert_true(zcbor_map_end_encode(zse, 2));

	return (size_t)(zse[0].payload - request_buf);
}

static size_t encode_set_all_request(uint32_t state)
{
	zcbor_state_t zse[2];

	zcbor_new_encode_state(zse, ARRAY_SIZE(zse), request_buf, sizeof(request_buf), 0);
	zassert_true(zcbor_map_start_encode(zse, 1));
	zassert_true(zcbor_tstr_put_lit(zse, "state"));
	zassert_true(zcbor_uint32_put(zse, state));
	zassert_true(zcbor_map_end_encode(zse, 1));

	return (size_t)(zse[0].payload - request_buf);
}

static size_t encode_pulse_request(uint32_t channel, uint32_t duration_ms)
{
	zcbor_state_t zse[2];

	zcbor_new_encode_state(zse, ARRAY_SIZE(zse), request_buf, sizeof(request_buf), 0);
	zassert_true(zcbor_map_start_encode(zse, 2));
	zassert_true(zcbor_tstr_put_lit(zse, "channel"));
	zassert_true(zcbor_uint32_put(zse, channel));
	zassert_true(zcbor_tstr_put_lit(zse, "duration_ms"));
	zassert_true(zcbor_uint32_put(zse, duration_ms));
	zassert_true(zcbor_map_end_encode(zse, 2));

	return (size_t)(zse[0].payload - request_buf);
}

static size_t call_handler(enum rp2350_relay_6ch_mgmt_cmd command, bool write,
			   size_t request_len)
{
	size_t response_len = 0U;

	memset(response_buf, 0, sizeof(response_buf));
	zassert_equal(relay_mgmt_test_handle(command, write, request_buf, request_len,
					     response_buf, sizeof(response_buf),
					     &response_len),
		      0);
	zassert_not_equal(response_len, 0U);
	return response_len;
}

static bool decode_u32(size_t response_len, const char *key, uint32_t *value)
{
	zcbor_state_t zsd[4];
	struct zcbor_string actual_key;
	uint64_t scratch = 0U;

	zcbor_new_decode_state(zsd, ARRAY_SIZE(zsd), response_buf, response_len, 1, NULL, 0);
	if (!zcbor_map_start_decode(zsd)) {
		return false;
	}

	while (zcbor_tstr_decode(zsd, &actual_key)) {
		if (actual_key.len == strlen(key) &&
		    memcmp(actual_key.value, key, actual_key.len) == 0) {
			if (!zcbor_uint64_decode(zsd, &scratch) || scratch > UINT32_MAX) {
				return false;
			}

			*value = (uint32_t)scratch;
			return true;
		}

		if (!zcbor_any_skip(zsd, NULL)) {
			return false;
		}
	}

	return false;
}

static bool decode_bool(size_t response_len, const char *key, bool *value)
{
	zcbor_state_t zsd[4];
	struct zcbor_string actual_key;

	zcbor_new_decode_state(zsd, ARRAY_SIZE(zsd), response_buf, response_len, 1, NULL, 0);
	if (!zcbor_map_start_decode(zsd)) {
		return false;
	}

	while (zcbor_tstr_decode(zsd, &actual_key)) {
		if (actual_key.len == strlen(key) &&
		    memcmp(actual_key.value, key, actual_key.len) == 0) {
			return zcbor_bool_decode(zsd, value);
		}

		if (!zcbor_any_skip(zsd, NULL)) {
			return false;
		}
	}

	return false;
}

static bool decode_tstr(size_t response_len, const char *key, struct zcbor_string *value)
{
	zcbor_state_t zsd[4];
	struct zcbor_string actual_key;

	zcbor_new_decode_state(zsd, ARRAY_SIZE(zsd), response_buf, response_len, 1, NULL, 0);
	if (!zcbor_map_start_decode(zsd)) {
		return false;
	}

	while (zcbor_tstr_decode(zsd, &actual_key)) {
		if (actual_key.len == strlen(key) &&
		    memcmp(actual_key.value, key, actual_key.len) == 0) {
			return zcbor_tstr_decode(zsd, value);
		}

		if (!zcbor_any_skip(zsd, NULL)) {
			return false;
		}
	}

	return false;
}

static bool decode_err(size_t response_len, uint32_t *group, uint32_t *rc)
{
	zcbor_state_t zsd[5];
	struct zcbor_map_decode_key_val err_fields[] = {
		ZCBOR_MAP_DECODE_KEY_DECODER("group", zcbor_uint32_decode, group),
		ZCBOR_MAP_DECODE_KEY_DECODER("rc", zcbor_uint32_decode, rc),
	};
	size_t decoded;

	zcbor_new_decode_state(zsd, ARRAY_SIZE(zsd), response_buf, response_len, 1, NULL, 0);
	if (!zcbor_map_start_decode(zsd) ||
	    !zcbor_tstr_expect_lit(zsd, "err")) {
		return false;
	}

	return zcbor_map_decode_bulk(zsd, err_fields, ARRAY_SIZE(err_fields), &decoded) == 0;
}

static void assert_state(size_t response_len, uint32_t state, uint32_t pulsing)
{
	uint32_t actual_state = 0xffU;
	uint32_t actual_pulsing = 0xffU;

	zassert_true(decode_u32(response_len, "state", &actual_state));
	zassert_true(decode_u32(response_len, "pulsing", &actual_pulsing));
	zassert_equal(actual_state, state);
	zassert_equal(actual_pulsing, pulsing);
}

static void assert_tstr_equal(size_t response_len, const char *key, const char *expected)
{
	struct zcbor_string actual;

	zassert_true(decode_tstr(response_len, key, &actual));
	zassert_equal(actual.len, strlen(expected));
	zassert_equal(memcmp(actual.value, expected, actual.len), 0);
}

static void assert_health(size_t response_len, const char *health,
			  uint32_t reasons, const char *primary_reason)
{
	uint32_t actual_reasons = UINT32_MAX;

	assert_tstr_equal(response_len, "health", health);
	assert_tstr_equal(response_len, "health_primary_reason", primary_reason);
	zassert_true(decode_u32(response_len, "health_reasons", &actual_reasons));
	zassert_equal(actual_reasons, reasons);
}

static void assert_error(size_t response_len,
			 enum rp2350_relay_6ch_mgmt_err expected_error)
{
	uint32_t group = 0U;
	uint32_t rc = 0U;

	zassert_true(decode_err(response_len, &group, &rc));
	zassert_equal(group, RP2350_RELAY_6CH_MGMT_GROUP_ID);
	zassert_equal(rc, expected_error);
}

static void *relay_mgmt_suite_setup(void)
{
	health_test_reset();
	zassert_equal(relay_init(), 0, "relay_init failed");
	health_set_relay_gpio_ready(true);
	health_set_rpc_ready(true);
	return NULL;
}

static void relay_mgmt_before(void *fixture)
{
	ARG_UNUSED(fixture);

	health_test_reset();
	indicator_test_reset();
	watchdog_supervisor_test_reset();
	relay_mgmt_test_cancel_reboot();
	zassert_equal(relay_off_all(), 0);
	health_set_relay_gpio_ready(true);
	health_set_rpc_ready(true);
	relay_mgmt_reset_counters();
}

static void relay_mgmt_after(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_equal(relay_off_all(), 0);
	relay_mgmt_test_cancel_reboot();
}

ZTEST_SUITE(relay_mgmt, NULL, relay_mgmt_suite_setup, relay_mgmt_before,
	    relay_mgmt_after, NULL);

ZTEST(relay_mgmt, test_group_registered)
{
	const struct mgmt_group *group =
		mgmt_find_group(RP2350_RELAY_6CH_MGMT_GROUP_ID);

	zassert_not_null(group);
	zassert_equal(group->mg_handlers_count, RP2350_RELAY_6CH_MGMT_CMD_COUNT);
	zassert_not_null(mgmt_get_handler(group, RP2350_RELAY_6CH_MGMT_CMD_INFO));
	zassert_not_null(mgmt_get_handler(group, RP2350_RELAY_6CH_MGMT_CMD_BUILD_INFO));
	zassert_not_null(mgmt_get_handler(group, RP2350_RELAY_6CH_MGMT_CMD_HEARTBEAT));
}

ZTEST(relay_mgmt, test_registered_management_service_publishes_rpc_ready)
{
	struct health_snapshot snap;

	health_test_reset();
	relay_mgmt_publish_health();
	health_set_relay_gpio_ready(true);
	health_snapshot(&snap);

	zassert_equal(snap.state, HEALTH_NORMAL);
	zassert_equal(snap.reasons, HEALTH_REASON_NONE);
}

ZTEST(relay_mgmt, test_info_reports_capabilities)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_INFO, false,
					   encode_empty_request());
	uint32_t protocol_version = 0U;
	uint32_t relay_count = 0U;
	uint32_t pulse_min = 0U;
	uint32_t pulse_max = 0U;
	uint32_t comm_loss_timeout_ms = 0U;
	uint32_t comm_loss_reboot_delay_ms = UINT32_MAX;
	bool comm_loss_reboot_on_timeout = true;

	zassert_true(decode_u32(response_len, "protocol_version", &protocol_version));
	zassert_true(decode_u32(response_len, "relay_count", &relay_count));
	zassert_true(decode_u32(response_len, "pulse_min_ms", &pulse_min));
	zassert_true(decode_u32(response_len, "pulse_max_ms", &pulse_max));
	zassert_true(decode_u32(response_len, "comm_loss_timeout_ms",
				&comm_loss_timeout_ms));
	zassert_true(decode_bool(response_len, "comm_loss_reboot_on_timeout",
				 &comm_loss_reboot_on_timeout));
	zassert_true(decode_u32(response_len, "comm_loss_reboot_delay_ms",
				&comm_loss_reboot_delay_ms));
	zassert_equal(protocol_version, 6U);
	zassert_equal(protocol_version, RP2350_RELAY_6CH_MGMT_PROTOCOL_VERSION);
	zassert_equal(relay_count, RP2350_RELAY_6CH_CHANNEL_COUNT);
	zassert_equal(pulse_min, RP2350_RELAY_6CH_PULSE_MIN_MS);
	zassert_equal(pulse_max, RP2350_RELAY_6CH_PULSE_MAX_MS);
	zassert_equal(comm_loss_timeout_ms, relay_comm_loss_timeout_ms());
	zassert_equal(comm_loss_reboot_on_timeout, relay_comm_loss_reboot_on_timeout());
	zassert_equal(comm_loss_reboot_delay_ms, relay_comm_loss_reboot_delay_ms());
	assert_tstr_equal(response_len, "comm_loss_policy", relay_comm_loss_policy());
}

ZTEST(relay_mgmt, test_build_info_reports_build_metadata)
{
	static const char shanghai_offset[] = "+08:00";
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_BUILD_INFO, false,
					   encode_empty_request());
	struct zcbor_string text;
	bool git_dirty = true;

	zassert_true(decode_tstr(response_len, "app_version", &text));
	zassert_not_equal(text.len, 0U);
	zassert_true(decode_tstr(response_len, "zephyr_version", &text));
	zassert_not_equal(text.len, 0U);
	zassert_true(decode_tstr(response_len, "board", &text));
	zassert_not_equal(text.len, 0U);
	zassert_true(decode_tstr(response_len, "git_commit", &text));
	zassert_not_equal(text.len, 0U);
	zassert_true(decode_bool(response_len, "git_dirty", &git_dirty));
	zassert_true(decode_tstr(response_len, "build_timestamp", &text));
	zassert_not_equal(text.len, 0U);
	zassert_true(text.len > strlen(shanghai_offset));
	zassert_equal(memcmp(text.value + text.len - strlen(shanghai_offset), shanghai_offset,
			     strlen(shanghai_offset)),
		      0);
	zassert_true(decode_tstr(response_len, "compiler", &text));
	zassert_not_equal(text.len, 0U);
}

ZTEST(relay_mgmt, test_heartbeat_returns_ok_and_does_not_change_state)
{
	size_t response_len;
	bool ok = false;
	uint32_t received = 0U;
	uint32_t succeeded = 0U;
	uint32_t comm_loss_timeout_ms = 0U;
	uint32_t comm_loss_reboot_delay_ms = UINT32_MAX;
	bool comm_loss_reboot_on_timeout = true;

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
				    encode_set_request(0U, true));
	assert_state(response_len, BIT(0), 0U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_HEARTBEAT, true,
				    encode_empty_request());
	zassert_true(decode_bool(response_len, "ok", &ok));
	zassert_true(ok);
	zassert_true(decode_u32(response_len, "comm_loss_timeout_ms",
				&comm_loss_timeout_ms));
	zassert_true(decode_bool(response_len, "comm_loss_reboot_on_timeout",
				 &comm_loss_reboot_on_timeout));
	zassert_true(decode_u32(response_len, "comm_loss_reboot_delay_ms",
				&comm_loss_reboot_delay_ms));
	zassert_equal(comm_loss_timeout_ms, relay_comm_loss_timeout_ms());
	zassert_equal(comm_loss_reboot_on_timeout, relay_comm_loss_reboot_on_timeout());
	zassert_equal(comm_loss_reboot_delay_ms, relay_comm_loss_reboot_delay_ms());
	assert_tstr_equal(response_len, "comm_loss_policy", relay_comm_loss_policy());

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_GET, false,
				    encode_empty_request());
	assert_state(response_len, BIT(0), 0U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_STATUS, false,
				    encode_empty_request());
	zassert_true(decode_u32(response_len, "received", &received));
	zassert_true(decode_u32(response_len, "succeeded", &succeeded));
	zassert_equal(received, 4U);
	zassert_equal(succeeded, 3U);
}

ZTEST(relay_mgmt, test_get_all_default_off)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_GET, false,
					   encode_empty_request());

	assert_state(response_len, 0U, 0U);
}

ZTEST(relay_mgmt, test_set_and_get_one)
{
	size_t response_len;
	uint32_t channel = 0xffU;
	bool on = false;
	bool pulsing = true;

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
				    encode_set_request(1U, true));
	assert_state(response_len, BIT(1), 0U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_GET, false,
				    encode_channel_request(1U));
	zassert_true(decode_u32(response_len, "channel", &channel));
	zassert_true(decode_bool(response_len, "on", &on));
	zassert_true(decode_bool(response_len, "pulsing", &pulsing));
	zassert_equal(channel, 1U);
	zassert_true(on);
	zassert_false(pulsing);
}

ZTEST(relay_mgmt, test_set_all_and_off_all)
{
	size_t response_len;

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET_ALL, true,
				    encode_set_all_request(BIT(0) | BIT(5)));
	assert_state(response_len, BIT(0) | BIT(5), 0U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_OFF_ALL, true,
				    encode_empty_request());
	assert_state(response_len, 0U, 0U);
}

ZTEST(relay_mgmt, test_pulse_busy_and_final_off)
{
	size_t response_len;

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_PULSE, true,
				    encode_pulse_request(0U, 100U));
	assert_state(response_len, BIT(0), BIT(0));

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_PULSE, true,
				    encode_pulse_request(0U, 100U));
	assert_error(response_len, RP2350_RELAY_6CH_MGMT_ERR_BUSY);

	k_msleep(110U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_GET, false,
				    encode_empty_request());
	assert_state(response_len, 0U, 0U);
}

ZTEST(relay_mgmt, test_invalid_channel_returns_group_error)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
					   encode_set_request(RP2350_RELAY_6CH_CHANNEL_COUNT,
							      true));

	assert_error(response_len, RP2350_RELAY_6CH_MGMT_ERR_INVALID_ARGUMENT);
}

ZTEST(relay_mgmt, test_invalid_pulse_duration_returns_group_error)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_PULSE, true,
					   encode_pulse_request(0U,
								RP2350_RELAY_6CH_PULSE_MIN_MS -
									1U));

	assert_error(response_len, RP2350_RELAY_6CH_MGMT_ERR_INVALID_ARGUMENT);
}

ZTEST(relay_mgmt, test_missing_required_field_returns_group_error)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
					   encode_channel_request(0U));

	assert_error(response_len, RP2350_RELAY_6CH_MGMT_ERR_INVALID_ARGUMENT);
}

ZTEST(relay_mgmt, test_malformed_cbor_returns_decode_error)
{
	size_t response_len;

	request_buf[0] = 0xffU;
	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true, 1U);
	assert_error(response_len, RP2350_RELAY_6CH_MGMT_ERR_DECODE);
}

ZTEST(relay_mgmt, test_malformed_empty_request_command_returns_decode_error)
{
	size_t response_len;

	request_buf[0] = 0xffU;
	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_INFO, false, 1U);
	assert_error(response_len, RP2350_RELAY_6CH_MGMT_ERR_DECODE);

	request_buf[0] = 0xffU;
	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_BUILD_INFO, false, 1U);
	assert_error(response_len, RP2350_RELAY_6CH_MGMT_ERR_DECODE);

	request_buf[0] = 0xffU;
	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_HEARTBEAT, true, 1U);
	assert_error(response_len, RP2350_RELAY_6CH_MGMT_ERR_DECODE);
}

ZTEST(relay_mgmt, test_status_reports_counters)
{
	size_t response_len;
	uint32_t received = 0U;
	uint32_t succeeded = 0U;
	uint32_t invalid_args = 0U;
	bool usb_cdc_acm = true;
	bool smp_uart = true;
	uint32_t comm_loss_timeout_ms = 0U;
	uint32_t comm_loss_reboot_delay_ms = UINT32_MAX;
	bool comm_loss_reboot_on_timeout = true;

	(void)call_handler(RP2350_RELAY_6CH_MGMT_CMD_GET, false, encode_empty_request());
	(void)call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
			   encode_set_request(RP2350_RELAY_6CH_CHANNEL_COUNT, true));

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_STATUS, false,
				    encode_empty_request());

	zassert_true(decode_u32(response_len, "received", &received));
	zassert_true(decode_u32(response_len, "succeeded", &succeeded));
	zassert_true(decode_u32(response_len, "invalid_args", &invalid_args));
	zassert_true(decode_bool(response_len, "usb_cdc_acm", &usb_cdc_acm));
	zassert_true(decode_bool(response_len, "smp_uart", &smp_uart));
	zassert_true(decode_u32(response_len, "comm_loss_timeout_ms",
				&comm_loss_timeout_ms));
	zassert_true(decode_bool(response_len, "comm_loss_reboot_on_timeout",
				 &comm_loss_reboot_on_timeout));
	zassert_true(decode_u32(response_len, "comm_loss_reboot_delay_ms",
				&comm_loss_reboot_delay_ms));
	zassert_equal(comm_loss_timeout_ms, relay_comm_loss_timeout_ms());
	zassert_equal(comm_loss_reboot_on_timeout, relay_comm_loss_reboot_on_timeout());
	zassert_equal(comm_loss_reboot_delay_ms, relay_comm_loss_reboot_delay_ms());
	assert_tstr_equal(response_len, "comm_loss_policy", relay_comm_loss_policy());
	zassert_equal(received, 3U);
	zassert_equal(succeeded, 1U);
	zassert_equal(invalid_args, 1U);
	zassert_false(usb_cdc_acm);
	zassert_false(smp_uart);
	assert_health(response_len, "normal", HEALTH_REASON_NONE, "none");
}

ZTEST(relay_mgmt, test_status_reports_health_fields_from_one_snapshot)
{
	size_t response_len;
	uint32_t transitions = 0U;

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
				    encode_set_request(2U, true));
	assert_state(response_len, BIT(2), 0U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_STATUS, false,
				    encode_empty_request());

	assert_state(response_len, BIT(2), 0U);
	assert_health(response_len, "relay_active", HEALTH_REASON_NONE, "none");
	zassert_true(decode_u32(response_len, "health_transitions", &transitions));
	zassert_true(transitions > 0U);
}

ZTEST(relay_mgmt, test_status_reports_watchdog_fields)
{
	size_t response_len;
	bool watchdog_enabled = false;
	bool watchdog_healthy = false;
	bool last_reset_watchdog = false;
	uint32_t watchdog_timeout_ms = 0U;
	uint32_t watchdog_feed_interval_ms = 0U;
	uint32_t watchdog_feeds = UINT32_MAX;
	uint32_t watchdog_feed_errors = UINT32_MAX;

	watchdog_supervisor_test_set_device_ready(true);
	watchdog_supervisor_test_set_last_reset_watchdog(true);
	watchdog_supervisor_start();
	watchdog_supervisor_test_run_once();

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_STATUS, false,
				    encode_empty_request());

	zassert_true(decode_bool(response_len, "watchdog_enabled", &watchdog_enabled));
	zassert_true(decode_bool(response_len, "watchdog_healthy", &watchdog_healthy));
	zassert_true(decode_u32(response_len, "watchdog_timeout_ms",
				&watchdog_timeout_ms));
	zassert_true(decode_u32(response_len, "watchdog_feed_interval_ms",
				&watchdog_feed_interval_ms));
	zassert_true(decode_u32(response_len, "watchdog_feeds", &watchdog_feeds));
	zassert_true(decode_u32(response_len, "watchdog_feed_errors",
				&watchdog_feed_errors));
	zassert_true(decode_bool(response_len, "last_reset_watchdog",
				 &last_reset_watchdog));
	zassert_true(watchdog_enabled);
	zassert_true(watchdog_healthy);
	zassert_equal(watchdog_timeout_ms, CONFIG_RP2350_RELAY_6CH_WATCHDOG_TIMEOUT_MS);
	zassert_equal(watchdog_feed_interval_ms,
		      CONFIG_RP2350_RELAY_6CH_WATCHDOG_FEED_INTERVAL_MS);
	zassert_equal(watchdog_feeds, 1U);
	zassert_equal(watchdog_feed_errors, 0U);
	zassert_true(last_reset_watchdog);
}

#if IS_ENABLED(CONFIG_REBOOT)
ZTEST(relay_mgmt, test_host_reboot_success_sets_pending_health)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_REBOOT, true,
					   encode_empty_request());
	bool ok = false;
	struct health_snapshot snap;

	zassert_true(decode_bool(response_len, "ok", &ok));
	zassert_true(ok);
	health_snapshot(&snap);
	zassert_equal(snap.state, HEALTH_RECOVERY_PENDING);
	zassert_equal(snap.primary_reason, HEALTH_REASON_HOST_REBOOT_PENDING);
}

ZTEST(relay_mgmt, test_host_reboot_schedule_failure_returns_reboot_failed)
{
	size_t response_len;
	struct health_snapshot snap;

	relay_mgmt_test_force_reboot_schedule_result(-EINVAL);
	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_REBOOT, true,
				    encode_empty_request());

	assert_error(response_len, RP2350_RELAY_6CH_MGMT_ERR_REBOOT_FAILED);
	health_snapshot(&snap);
	zassert_equal(snap.state, HEALTH_FAULT);
	zassert_equal(snap.primary_reason, HEALTH_REASON_REBOOT_FAILED);
}

ZTEST(relay_mgmt, test_host_reboot_return_records_reboot_failed)
{
	struct health_snapshot snap;

	relay_mgmt_test_force_reboot_return(true);
	relay_mgmt_test_run_reboot_work();

	health_snapshot(&snap);
	zassert_equal(snap.state, HEALTH_FAULT);
	zassert_equal(snap.primary_reason, HEALTH_REASON_REBOOT_FAILED);
}

ZTEST(relay_mgmt, test_host_reboot_off_all_failure_records_reboot_failed)
{
	struct health_snapshot snap;

	relay_test_force_next_off_all_result(-EIO);
	relay_mgmt_test_run_reboot_work();

	health_snapshot(&snap);
	zassert_equal(snap.state, HEALTH_FAULT);
	zassert_true((snap.reasons & HEALTH_REASON_REBOOT_FAILED) != 0U);
	zassert_equal(snap.primary_reason, HEALTH_REASON_REBOOT_FAILED);
}
#endif

#if IS_ENABLED(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_ENERGIZED_ONLY) && \
	CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS <= 100
ZTEST(relay_mgmt, test_comm_loss_energized_only_expires_outputs)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
					   encode_set_request(0U, true));

	assert_state(response_len, BIT(0), 0U);
	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS + 20U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_GET, false,
				    encode_empty_request());
	assert_state(response_len, 0U, 0U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_STATUS, false,
				    encode_empty_request());
	assert_state(response_len, 0U, 0U);
	assert_health(response_len, "degraded", HEALTH_REASON_COMM_OWNER_TIMEOUT,
		      "comm_owner_timeout");
}

ZTEST(relay_mgmt, test_comm_loss_heartbeat_renews_lease)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
					   encode_set_request(0U, true));
	bool ok = false;

	assert_state(response_len, BIT(0), 0U);
	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS / 3U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_HEARTBEAT, true,
				    encode_empty_request());
	zassert_true(decode_bool(response_len, "ok", &ok));
	zassert_true(ok);
	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS / 3U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_GET, false,
				    encode_empty_request());
	assert_state(response_len, BIT(0), 0U);

	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS + 20U);
	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_GET, false,
				    encode_empty_request());
	assert_state(response_len, 0U, 0U);
}

ZTEST(relay_mgmt, test_comm_loss_control_command_renews_lease)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
					   encode_set_request(0U, true));

	assert_state(response_len, BIT(0), 0U);
	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS / 3U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
				    encode_set_request(1U, true));
	assert_state(response_len, BIT(0) | BIT(1), 0U);
	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS / 3U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_GET, false,
				    encode_empty_request());
	assert_state(response_len, BIT(0) | BIT(1), 0U);
}

ZTEST(relay_mgmt, test_comm_loss_off_all_disarms_energized_only)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
					   encode_set_request(0U, true));

	assert_state(response_len, BIT(0), 0U);
	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_OFF_ALL, true,
				    encode_empty_request());
	assert_state(response_len, 0U, 0U);

	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS + 20U);
	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_GET, false,
				    encode_empty_request());
	assert_state(response_len, 0U, 0U);
}

ZTEST(relay_mgmt, test_comm_loss_cuts_long_pulse)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_PULSE, true,
					   encode_pulse_request(0U, 100U));

	assert_state(response_len, BIT(0), BIT(0));
	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS + 20U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_GET, false,
				    encode_empty_request());
	assert_state(response_len, 0U, 0U);
}

ZTEST(relay_mgmt, test_comm_loss_short_pulse_finishes_before_timeout)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_PULSE, true,
					   encode_pulse_request(0U,
								RP2350_RELAY_6CH_PULSE_MIN_MS));

	assert_state(response_len, BIT(0), BIT(0));
	k_msleep(RP2350_RELAY_6CH_PULSE_MIN_MS + 20U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_GET, false,
				    encode_empty_request());
	assert_state(response_len, 0U, 0U);
}
#endif

#if IS_ENABLED(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_NO_TIMEOUT)
ZTEST(relay_mgmt, test_comm_loss_no_timeout_leaves_energized_outputs)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
					   encode_set_request(0U, true));

	assert_state(response_len, BIT(0), 0U);
	zassert_equal(relay_comm_loss_timeout_ms(), 0U);
	k_msleep(70U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_GET, false,
				    encode_empty_request());
	assert_state(response_len, BIT(0), 0U);
}
#endif

#if IS_ENABLED(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_ALWAYS_ON_OWNER) && \
	CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS <= 100
ZTEST(relay_mgmt, test_comm_loss_always_on_owner_expires_outputs)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
					   encode_set_request(0U, true));
	struct indicator_test_snapshot snap;

	assert_state(response_len, BIT(0), 0U);
	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS + 20U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_GET, false,
				    encode_empty_request());
	assert_state(response_len, 0U, 0U);
	indicator_test_force_render();
	indicator_test_get_snapshot(&snap);
	zassert_true(snap.owner_lost);
#if IS_ENABLED(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_ALWAYS_ON_OWNER_REBOOT_ON_TIMEOUT)
	zassert_equal(snap.rgb, INDICATOR_RGB_ATTENTION);
	zassert_false(snap.reboot_pending);
#else
	zassert_equal(snap.rgb, INDICATOR_RGB_ATTENTION);
#endif
}

ZTEST(relay_mgmt, test_comm_loss_always_on_owner_heartbeat_renews_while_off)
{
	size_t response_len;
	bool ok = false;

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_OFF_ALL, true,
				    encode_empty_request());
	assert_state(response_len, 0U, 0U);
	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS / 2U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_HEARTBEAT, true,
				    encode_empty_request());
	zassert_true(decode_bool(response_len, "ok", &ok));
	zassert_true(ok);
}

ZTEST(relay_mgmt, test_comm_loss_always_on_owner_heartbeat_clears_owner_lost)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
					   encode_set_request(0U, true));
	struct indicator_test_snapshot snap;

	assert_state(response_len, BIT(0), 0U);
	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS + 20U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_HEARTBEAT, true,
				    encode_empty_request());
	indicator_test_force_render();
	indicator_test_get_snapshot(&snap);
	zassert_false(snap.owner_lost);
}

ZTEST(relay_mgmt, test_comm_loss_always_on_owner_control_clears_owner_lost)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
					   encode_set_request(0U, true));
	struct indicator_test_snapshot snap;

	assert_state(response_len, BIT(0), 0U);
	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS + 20U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
				    encode_set_request(0U, false));
	assert_state(response_len, 0U, 0U);
	indicator_test_force_render();
	indicator_test_get_snapshot(&snap);
	zassert_false(snap.owner_lost);
}
#endif

#if IS_ENABLED(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_ALWAYS_ON_OWNER_REBOOT_ON_TIMEOUT) && \
	CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS <= 100
ZTEST(relay_mgmt, test_comm_loss_always_on_owner_reboot_on_timeout_reports_and_schedules)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_INFO, false,
					   encode_empty_request());
	bool comm_loss_reboot_on_timeout = false;
	uint32_t comm_loss_reboot_delay_ms = 0U;
	uint32_t reboot_remaining_ms = 0U;

	zassert_true(decode_bool(response_len, "comm_loss_reboot_on_timeout",
				 &comm_loss_reboot_on_timeout));
	zassert_true(decode_u32(response_len, "comm_loss_reboot_delay_ms",
				&comm_loss_reboot_delay_ms));
	zassert_true(comm_loss_reboot_on_timeout);
	zassert_equal(comm_loss_reboot_delay_ms, relay_comm_loss_reboot_delay_ms());

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
				    encode_set_request(0U, true));
	assert_state(response_len, BIT(0), 0U);
	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS + 20U);

	zassert_true(relay_comm_loss_test_reboot_scheduled());
	zassert_true(relay_comm_loss_test_reboot_pending_indication_scheduled());
	reboot_remaining_ms = relay_comm_loss_test_reboot_remaining_ms();
	zassert_true(reboot_remaining_ms <= relay_comm_loss_reboot_delay_ms());
	zassert_true(reboot_remaining_ms > relay_comm_loss_reboot_delay_ms() / 2U);
	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_GET, false,
				    encode_empty_request());
	assert_state(response_len, 0U, 0U);
}

ZTEST(relay_mgmt, test_comm_loss_reboot_pending_starts_in_final_grace_phase)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
					   encode_set_request(0U, true));
	struct indicator_test_snapshot snap;

	assert_state(response_len, BIT(0), 0U);
	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS + 20U);
	zassert_true(relay_comm_loss_test_reboot_scheduled());
	zassert_true(relay_comm_loss_test_reboot_pending_indication_scheduled());
	indicator_test_force_render();
	indicator_test_get_snapshot(&snap);
	zassert_true(snap.owner_lost);
	zassert_false(snap.reboot_pending);
	zassert_equal(snap.rgb, INDICATOR_RGB_ATTENTION);

	k_msleep(relay_comm_loss_test_reboot_remaining_ms() / 2U);
	indicator_test_force_render();
	indicator_test_get_snapshot(&snap);
	zassert_true(snap.owner_lost);
	zassert_true(snap.reboot_pending);
	zassert_equal(snap.rgb, INDICATOR_RGB_REBOOT_PENDING);
}

ZTEST(relay_mgmt, test_comm_loss_reboot_pending_heartbeat_cancels_reboot)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
					   encode_set_request(0U, true));
	struct indicator_test_snapshot snap;

	assert_state(response_len, BIT(0), 0U);
	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS + 20U);
	zassert_true(relay_comm_loss_test_reboot_scheduled());
	zassert_true(relay_comm_loss_test_reboot_pending_indication_scheduled());

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_HEARTBEAT, true,
				    encode_empty_request());
	zassert_false(relay_comm_loss_test_reboot_scheduled());
	zassert_false(relay_comm_loss_test_reboot_pending_indication_scheduled());
	indicator_test_force_render();
	indicator_test_get_snapshot(&snap);
	zassert_false(snap.owner_lost);
	zassert_false(snap.reboot_pending);
}

ZTEST(relay_mgmt, test_host_reboot_keeps_indicator_after_comm_loss_reboot_canceled)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
					   encode_set_request(0U, true));
	struct indicator_test_snapshot snap;
	bool ok = false;

	assert_state(response_len, BIT(0), 0U);
	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS + 20U);
	zassert_true(relay_comm_loss_test_reboot_scheduled());
	indicator_test_force_render();
	indicator_test_get_snapshot(&snap);
	zassert_false(snap.reboot_pending);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_REBOOT, true,
				    encode_empty_request());
	zassert_true(decode_bool(response_len, "ok", &ok));
	zassert_true(ok);
	zassert_equal(relay_mgmt_test_reboot_delay_ms(), 1000U);

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_HEARTBEAT, true,
				    encode_empty_request());
	zassert_true(decode_bool(response_len, "ok", &ok));
	zassert_true(ok);
	zassert_false(relay_comm_loss_test_reboot_scheduled());
	indicator_test_force_render();
	indicator_test_get_snapshot(&snap);
	zassert_false(snap.owner_lost);
	zassert_true(snap.reboot_pending);
}

ZTEST(relay_mgmt, test_comm_loss_reboot_pending_control_cancels_reboot)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
					   encode_set_request(0U, true));
	struct indicator_test_snapshot snap;

	assert_state(response_len, BIT(0), 0U);
	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS + 20U);
	zassert_true(relay_comm_loss_test_reboot_scheduled());
	zassert_true(relay_comm_loss_test_reboot_pending_indication_scheduled());

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
				    encode_set_request(0U, false));
	assert_state(response_len, 0U, 0U);
	zassert_false(relay_comm_loss_test_reboot_scheduled());
	zassert_false(relay_comm_loss_test_reboot_pending_indication_scheduled());
	indicator_test_force_render();
	indicator_test_get_snapshot(&snap);
	zassert_false(snap.owner_lost);
	zassert_false(snap.reboot_pending);
}

ZTEST(relay_mgmt, test_comm_loss_reboot_schedule_failure_records_reboot_failed)
{
	size_t response_len;

	relay_comm_loss_test_force_reboot_schedule_result(-EINVAL);
	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
				    encode_set_request(0U, true));
	assert_state(response_len, BIT(0), 0U);
	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS + 20U);

	zassert_false(relay_comm_loss_test_reboot_scheduled());
	zassert_false(relay_comm_loss_test_reboot_pending_indication_scheduled());
	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_STATUS, false,
				    encode_empty_request());
	assert_health(response_len, "fault",
		      HEALTH_REASON_COMM_OWNER_TIMEOUT | HEALTH_REASON_REBOOT_FAILED,
		      "reboot_failed");
}

ZTEST(relay_mgmt, test_comm_loss_reboot_return_records_reboot_failed)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
					   encode_set_request(0U, true));

	assert_state(response_len, BIT(0), 0U);
	k_msleep(CONFIG_RP2350_RELAY_6CH_COMM_LOSS_TIMEOUT_MS + 20U);
	zassert_true(relay_comm_loss_test_reboot_scheduled());

	relay_comm_loss_test_force_reboot_return(true);
	relay_comm_loss_test_run_reboot_work();

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_STATUS, false,
				    encode_empty_request());
	assert_health(response_len, "fault",
		      HEALTH_REASON_COMM_OWNER_TIMEOUT | HEALTH_REASON_REBOOT_FAILED,
		      "reboot_failed");
}
#endif

/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/ztest.h>

#include <mgmt/mcumgr/util/zcbor_bulk.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include <rp2350_relay_6ch/relay.h>
#include <rp2350_relay_6ch/relay_mgmt.h>

#define TEST_BUF_SIZE 256U

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
	zassert_equal(relay_init(), 0, "relay_init failed");
	return NULL;
}

static void relay_mgmt_before(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_equal(relay_off_all(), 0);
	relay_mgmt_reset_counters();
}

static void relay_mgmt_after(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_equal(relay_off_all(), 0);
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
}

ZTEST(relay_mgmt, test_info_reports_capabilities)
{
	size_t response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_INFO, false,
					   encode_empty_request());
	uint32_t protocol_version = 0U;
	uint32_t relay_count = 0U;
	uint32_t pulse_min = 0U;
	uint32_t pulse_max = 0U;

	zassert_true(decode_u32(response_len, "protocol_version", &protocol_version));
	zassert_true(decode_u32(response_len, "relay_count", &relay_count));
	zassert_true(decode_u32(response_len, "pulse_min_ms", &pulse_min));
	zassert_true(decode_u32(response_len, "pulse_max_ms", &pulse_max));
	zassert_equal(protocol_version, RP2350_RELAY_6CH_MGMT_PROTOCOL_VERSION);
	zassert_equal(relay_count, RP2350_RELAY_6CH_CHANNEL_COUNT);
	zassert_equal(pulse_min, RP2350_RELAY_6CH_PULSE_MIN_MS);
	zassert_equal(pulse_max, RP2350_RELAY_6CH_PULSE_MAX_MS);
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
}

ZTEST(relay_mgmt, test_status_reports_counters)
{
	size_t response_len;
	uint32_t received = 0U;
	uint32_t succeeded = 0U;
	uint32_t invalid_args = 0U;

	(void)call_handler(RP2350_RELAY_6CH_MGMT_CMD_GET, false, encode_empty_request());
	(void)call_handler(RP2350_RELAY_6CH_MGMT_CMD_SET, true,
			   encode_set_request(RP2350_RELAY_6CH_CHANNEL_COUNT, true));

	response_len = call_handler(RP2350_RELAY_6CH_MGMT_CMD_STATUS, false,
				    encode_empty_request());

	zassert_true(decode_u32(response_len, "received", &received));
	zassert_true(decode_u32(response_len, "succeeded", &succeeded));
	zassert_true(decode_u32(response_len, "invalid_args", &invalid_args));
	zassert_equal(received, 3U);
	zassert_equal(succeeded, 1U);
	zassert_equal(invalid_args, 1U);
}

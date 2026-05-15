/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/shell/shell.h>

#include <rp2350_relay_6ch/relay.h>

static int parse_channel(const struct shell *sh, const char *arg, uint8_t *channel)
{
	char *end;
	unsigned long value = strtoul(arg, &end, 10);

	if (*arg == '\0' || *end != '\0' || value < 1UL ||
	    value > RP2350_RELAY_6CH_CHANNEL_COUNT) {
		shell_error(sh, "channel must be 1-%u", RP2350_RELAY_6CH_CHANNEL_COUNT);
		return -EINVAL;
	}

	*channel = (uint8_t)(value - 1UL);
	return 0;
}

static int parse_on_off(const struct shell *sh, const char *arg, bool *on)
{
	if (strcmp(arg, "on") == 0) {
		*on = true;
		return 0;
	}

	if (strcmp(arg, "off") == 0) {
		*on = false;
		return 0;
	}

	shell_error(sh, "state must be 'on' or 'off'");
	return -EINVAL;
}

static int print_all(const struct shell *sh)
{
	uint8_t state_mask;
	int ret = relay_get_all(&state_mask);

	if (ret < 0) {
		shell_error(sh, "relay get failed: %d", ret);
		return ret;
	}

	for (uint8_t i = 0U; i < RP2350_RELAY_6CH_CHANNEL_COUNT; i++) {
		shell_print(sh, "CH%u %s", (unsigned int)i + 1U,
			    (state_mask & BIT(i)) != 0U ? "on" : "off");
	}

	return 0;
}

static int cmd_relay_get(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t channel;
	bool on;
	int ret;

	if (argc == 1U) {
		return print_all(sh);
	}

	ret = parse_channel(sh, argv[1], &channel);
	if (ret < 0) {
		return ret;
	}

	ret = relay_get(channel, &on);
	if (ret < 0) {
		shell_error(sh, "relay get failed: %d", ret);
		return ret;
	}

	shell_print(sh, "CH%u %s", (unsigned int)channel + 1U, on ? "on" : "off");
	return 0;
}

static int cmd_relay_set(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t channel;
	bool on;
	int ret;

	ret = parse_channel(sh, argv[1], &channel);
	if (ret < 0) {
		return ret;
	}

	ret = parse_on_off(sh, argv[2], &on);
	if (ret < 0) {
		return ret;
	}

	ret = relay_set(channel, on);
	if (ret < 0) {
		shell_error(sh, "relay set failed: %d", ret);
		return ret;
	}

	shell_print(sh, "CH%u %s", (unsigned int)channel + 1U, on ? "on" : "off");
	return 0;
}

static int cmd_relay_all(const struct shell *sh, size_t argc, char **argv)
{
	bool on;
	int ret = parse_on_off(sh, argv[1], &on);

	ARG_UNUSED(argc);

	if (ret < 0) {
		return ret;
	}

	ret = relay_set_all(on ? BIT_MASK(RP2350_RELAY_6CH_CHANNEL_COUNT) : 0U);
	if (ret < 0) {
		shell_error(sh, "relay all failed: %d", ret);
		return ret;
	}

	shell_print(sh, "all relays %s", on ? "on" : "off");
	return 0;
}

static int cmd_relay_off(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = relay_off_all();
	if (ret < 0) {
		shell_error(sh, "relay off failed: %d", ret);
		return ret;
	}

	shell_print(sh, "all relays off");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	relay_cmds,
	SHELL_CMD_ARG(get, NULL, "Get all relays or one channel: get [channel]",
		      cmd_relay_get, 1, 1),
	SHELL_CMD_ARG(set, NULL, "Set one relay: set <channel> <on|off>",
		      cmd_relay_set, 3, 0),
	SHELL_CMD_ARG(all, NULL, "Set all relays: all <on|off>",
		      cmd_relay_all, 2, 0),
	SHELL_CMD_ARG(off, NULL, "Turn all relays off", cmd_relay_off, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(relay, &relay_cmds, "Relay bring-up commands", NULL);

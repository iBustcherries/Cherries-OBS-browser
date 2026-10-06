/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <string.h>
static inline bool cherries_positive_game_id(const char *value)
{
	if (!value || !*value)
		return false;
	bool positive = false;
	for (; *value; value++) {
		if (*value < '0' || *value > '9')
			return false;
		positive |= *value != '0';
	}
	return positive;
}
static inline bool cherries_automatic_vulkan_allowed(const char *explicit_capture, const char *steam_app,
						     const char *steam_game)
{
	return (explicit_capture && !strcmp(explicit_capture, "1")) || cherries_positive_game_id(steam_app) ||
	       cherries_positive_game_id(steam_game);
}

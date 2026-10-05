#pragma once
#include <string.h>

/* Do not select launcher UI renderers in the automatic game mode. Explicit
 * executable selection remains available for capturing those applications. */
static inline int cherries_is_game_candidate(const char *exe)
{
	const char *launchers[] = {"steam",
				   "steamwebhelper",
				   "steamwebhelper.exe",
				   "steam.exe",
				   "obs",
				   "obs-studio",
				   "pressure-vessel-wrap",
				   "lutris",
				   "heroic",
				   "EpicGamesLauncher.exe",
				   "EpicWebHelper.exe",
				   "Battle.net.exe",
				   "Battle.net Helper.exe",
				   "UbisoftConnect.exe",
				   "upc.exe"};
	if (!exe || !*exe)
		return 0;
	for (unsigned i = 0; i < sizeof(launchers) / sizeof(launchers[0]); ++i)
		if (!strcmp(exe, launchers[i]))
			return 0;
	return 1;
}

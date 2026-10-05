#pragma once
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef pid_t (*cherries_audio_parent_fn)(pid_t);
static inline bool cherries_audio_matches_process(pid_t game, pid_t audio, cherries_audio_parent_fn parent)
{
	if (game <= 1 || audio <= 1)
		return false;
	for (unsigned depth = 0; depth < 64 && audio > 1; ++depth) {
		if (audio == game)
			return true;
		pid_t next = parent(audio);
		if (next <= 1 || next == audio)
			return false;
		audio = next;
	}
	return false;
}

static inline pid_t cherries_audio_parse_parent(const char *stat)
{
	const char *end = stat ? strrchr(stat, ')') : NULL;
	char state;
	long parent = 0;
	if (!end || sscanf(end + 1, " %c %ld", &state, &parent) != 2 || parent <= 0)
		return 0;
	return (pid_t)parent;
}

static inline pid_t cherries_audio_process_parent(pid_t process)
{
	char path[64], stat[4096];
	snprintf(path, sizeof(path), "/proc/%ld/stat", (long)process);
	FILE *file = fopen(path, "r");
	if (!file)
		return 0;
	char *read = fgets(stat, sizeof(stat), file);
	fclose(file);
	return cherries_audio_parse_parent(read);
}

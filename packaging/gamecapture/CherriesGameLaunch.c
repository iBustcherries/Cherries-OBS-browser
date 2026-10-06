/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <spawn.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "dlsym.h"

#ifndef CHERRIES_CAPTURE_LIBRARY
#define CHERRIES_CAPTURE_LIBRARY "/opt/cherries-obs/$LIB/obs_glcapture/libobs_glcapture.so"
#endif

extern char **environ;

struct launch_environment {
	char **values;
	void *mapping;
	size_t size;
};

static const char *environment_value(char *const values[], const char *key)
{
	if (!values)
		return NULL;
	size_t length = strlen(key);
	for (size_t i = 0; values[i]; i++) {
		if (strncmp(values[i], key, length) == 0 && values[i][length] == '=')
			return values[i] + length + 1;
	}
	return NULL;
}

static bool value_is(char *const values[], const char *key, const char *expected)
{
	const char *value = environment_value(values, key);
	return value && strcmp(value, expected) == 0;
}

static bool dead_cells_game(const char *file, char *const argv[], char *const values[])
{
	if (!file || !value_is(values, "CHERRIES_GAMECAPTURE_ACTIVE", "1"))
		return false;
	if (!value_is(values, "SteamAppId", "588650") && !value_is(values, "SteamGameId", "588650"))
		return false;
	const char *name = strrchr(file, '/');
	name = name ? name + 1 : file;
	if (strcmp(name, "deadcells") != 0)
		return false;
	/* The launcher's renderer detection stays exactly as supplied by the game. */
	for (size_t i = 1; argv && argv[0] && argv[i]; i++) {
		const char *argument = strrchr(argv[i], '/');
		argument = argument ? argument + 1 : argv[i];
		if (strcmp(argument, "detect.hl") == 0)
			return false;
	}
	return true;
}

static bool preload_contains(const char *preload)
{
	if (!preload)
		return false;
	const size_t length = sizeof(CHERRIES_CAPTURE_LIBRARY) - 1;
	while (*preload) {
		preload += strspn(preload, " :\t");
		size_t token_length = strcspn(preload, " :\t");
		if (token_length == length && strncmp(preload, CHERRIES_CAPTURE_LIBRARY, length) == 0)
			return true;
		preload += token_length;
	}
	return false;
}

static struct launch_environment prepare_environment(const char *file, char *const argv[], char *const values[])
{
	struct launch_environment result = {.values = (char **)values};
	if (!dead_cells_game(file, argv, values))
		return result;
	const char *old_preload = environment_value(values, "LD_PRELOAD");
	if (preload_contains(old_preload))
		return result;
	if (!old_preload)
		old_preload = "";
	size_t count = 0;
	while (values[count])
		count++;
	const size_t prefix_length = sizeof("LD_PRELOAD=" CHERRIES_CAPTURE_LIBRARY) - 1;
	size_t old_length = strlen(old_preload);
	if (count > SIZE_MAX / sizeof(char *) - 2 || old_length > SIZE_MAX - prefix_length - 2)
		return result;
	size_t pointers_size = (count + 2) * sizeof(char *);
	size_t string_size = prefix_length + old_length + 2;
	if (pointers_size > SIZE_MAX - string_size)
		return result;
	result.size = pointers_size + string_size;
	/* Avoid allocator locks in the interval between a shell's fork and exec. */
	result.mapping = mmap(NULL, result.size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (result.mapping == MAP_FAILED) {
		result.mapping = NULL;
		return result;
	}
	result.values = result.mapping;
	char *preload = (char *)result.mapping + pointers_size;
	memcpy(preload, "LD_PRELOAD=" CHERRIES_CAPTURE_LIBRARY, prefix_length);
	size_t position = prefix_length;
	if (old_length) {
		preload[position++] = ':';
		memcpy(preload + position, old_preload, old_length);
		position += old_length;
	}
	preload[position] = '\0';
	size_t copied = 0;
	for (size_t i = 0; i < count; i++) {
		if (strncmp(values[i], "LD_PRELOAD=", sizeof("LD_PRELOAD=") - 1) != 0)
			result.values[copied++] = values[i];
	}
	result.values[copied++] = preload;
	result.values[copied] = NULL;
	static const char message[] = "[cherries-gamecapture] Restoring capture hook for Dead Cells' game process\n";
	ssize_t written = write(STDERR_FILENO, message, sizeof(message) - 1);
	(void)written;
	return result;
}

static void release_environment(struct launch_environment *environment)
{
	int saved_errno = errno;
	if (environment->mapping)
		munmap(environment->mapping, environment->size);
	errno = saved_errno;
}

int execve(const char *file, char *const argv[], char *const values[])
{
	int (*next)(const char *, char *const[], char *const[]) = real_dlsym(RTLD_NEXT, "execve");
	if (!next) {
		errno = ENOSYS;
		return -1;
	}
	struct launch_environment environment = prepare_environment(file, argv, values);
	int result = next(file, argv, environment.values);
	release_environment(&environment);
	return result;
}

int execv(const char *file, char *const argv[])
{
	return execve(file, argv, environ);
}

int execvpe(const char *file, char *const argv[], char *const values[])
{
	int (*next)(const char *, char *const[], char *const[]) = real_dlsym(RTLD_NEXT, "execvpe");
	if (!next) {
		errno = ENOSYS;
		return -1;
	}
	struct launch_environment environment = prepare_environment(file, argv, values);
	int result = next(file, argv, environment.values);
	release_environment(&environment);
	return result;
}

int execvp(const char *file, char *const argv[])
{
	return execvpe(file, argv, environ);
}

#define CHERRIES_SPAWN_WRAPPER(function) \
	int function(pid_t *pid, const char *file, const posix_spawn_file_actions_t *actions, \
	             const posix_spawnattr_t *attributes, char *const argv[], char *const values[]) \
	{ \
		int (*next)(pid_t *, const char *, const posix_spawn_file_actions_t *, \
		            const posix_spawnattr_t *, char *const[], char *const[]) = real_dlsym(RTLD_NEXT, #function); \
		if (!next) \
			return ENOSYS; \
		struct launch_environment environment = prepare_environment(file, argv, values); \
		int result = next(pid, file, actions, attributes, argv, environment.values); \
		release_environment(&environment); \
		return result; \
	}

CHERRIES_SPAWN_WRAPPER(posix_spawn)
CHERRIES_SPAWN_WRAPPER(posix_spawnp)

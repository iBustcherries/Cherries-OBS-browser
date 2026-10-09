/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#ifdef CHERRIES_TEST_RESOLVER
void *real_dlsym(void *handle, const char *symbol)
{
	return dlsym(handle, symbol);
}
#elif defined(CHERRIES_TEST_GAME)
int main(int argc, char **argv)
{
	const char *library = getenv("CHERRIES_TEST_LIBRARY");
	assert(library);
	void *loaded = dlopen(library, RTLD_LAZY | RTLD_NOLOAD);
	const char *expected = getenv("CHERRIES_TEST_EXPECT_HOOK");
	assert(expected && (loaded != NULL) == (strcmp(expected, "1") == 0));
	if (loaded)
		dlclose(loaded);
	assert(strcmp(getenv("LD_LIBRARY_PATH"), ".") == 0);
	const char *keep = getenv("CHERRIES_TEST_KEEP_PRELOAD");
	if (keep)
		assert(strstr(getenv("LD_PRELOAD"), keep));
	if (argc > 1 && strcmp(argv[1], "detect.hl") == 0)
		assert(argc == 2);
	else {
		assert(argc == 3 && strcmp(argv[1], "argument with spaces") == 0);
		assert(strcmp(argv[2], "literal-$value") == 0);
	}
	puts("launch environment and arguments verified");
	return 23;
}
#else
int main(int argc, char **argv)
{
	assert(argc == 3);
	const char *method = argv[1];
	char *arguments[] = {argv[2], "argument with spaces", "literal-$value", NULL};
	if (strcmp(method, "missing") == 0) {
		setenv("LD_PRELOAD", "", 1);
		errno = 0;
		assert(execve("./missing/deadcells", arguments, environ) == -1);
		assert(errno == ENOENT);
		assert(strcmp(getenv("LD_PRELOAD"), "") == 0);
		return 0;
	}
	const char *keep = getenv("CHERRIES_TEST_KEEP_PRELOAD");
	setenv("LD_PRELOAD", keep ? keep : "", 1);
	if (strcmp(method, "execve") == 0)
		execve(argv[2], arguments, environ);
	else if (strcmp(method, "execv") == 0)
		execv(argv[2], arguments);
	else if (strcmp(method, "execvp") == 0)
		execvp(argv[2], arguments);
	else if (strcmp(method, "execvpe") == 0)
		execvpe(argv[2], arguments, environ);
	else {
		pid_t pid;
		int result = strcmp(method, "posix_spawn") == 0
				     ? posix_spawn(&pid, argv[2], NULL, NULL, arguments, environ)
				     : posix_spawnp(&pid, argv[2], NULL, NULL, arguments, environ);
		assert(result == 0);
		int status;
		assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status));
		return WEXITSTATUS(status);
	}
	perror(method);
	return 1;
}
#endif

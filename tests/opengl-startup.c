/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>

#ifdef CHERRIES_TEST_GL_MODULE
/* HashLink's SDL module exports OpenGL function-pointer variables like this.
 * An early global libOpenGL dependency preempts the variable with executable
 * code, so assigning the result of SDL_GL_GetProcAddress faults. */
void (*glActiveTexture)(unsigned);

void initialize_graphics(void *function)
{
	glActiveTexture = function;
}

void *graphics_function(void)
{
	return (void *)glActiveTexture;
}
#else
int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (!module) {
		fprintf(stderr, "Module load failed: %s\n", dlerror());
		return 3;
	}
	void (*initialize)(void *) = dlsym(module, "initialize_graphics");
	void *(*read_function)(void) = dlsym(module, "graphics_function");
	void *graphics = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
	if (!initialize || !read_function || !graphics)
		return 4;
	void *(*get_proc)(const char *) = dlsym(graphics, "glXGetProcAddress");
	if (!get_proc)
		return 5;
	void *function = get_proc("glActiveTexture");
	if (!function)
		return 6;
	initialize(function);
	if (read_function() != function)
		return 7;
	puts("OpenGL function-pointer initialization passed with capture preloaded");
	dlclose(module);
	dlclose(graphics);
	return 0;
}
#endif

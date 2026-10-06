#include <obs.h>
#include <obs-nix-platform.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
	Display *display = NULL;
	for (int attempt = 0; attempt < 30 && !display; ++attempt) {
		display = XOpenDisplay(NULL);
		if (!display)
			usleep(100000);
	}
	if (!display)
		return 2;
	obs_set_nix_platform(OBS_NIX_PLATFORM_X11_EGL);
	obs_set_nix_platform_display(display);
	if (!obs_startup("en-US", NULL, NULL))
		return 3;
	obs_module_t *module = NULL;
	int result = obs_open_module(&module, "/opt/cherries-obs/lib64/obs-modules/core/linux-vkcapture.so",
				     "/opt/cherries-obs/share/obs/obs-modules/core/linux-vkcapture");
	if (result != MODULE_SUCCESS || !obs_init_module(module)) {
		fprintf(stderr, "Game Capture module failed to initialize (%d)\n", result);
		obs_shutdown();
		return 4;
	}
	const char *name = obs_source_get_display_name("vkcapture-source");
	uint32_t flags = obs_get_source_output_flags("vkcapture-source");
	bool registered = name && strcmp(name, "Game Capture") == 0 &&
			  (flags & (OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO)) == (OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO);
	printf("Game Capture registered: name=%s; video=%d; audio=%d\n", name ? name : "missing",
	       !!(flags & OBS_SOURCE_VIDEO), !!(flags & OBS_SOURCE_AUDIO));
	obs_shutdown();
	return registered ? 0 : 5;
}

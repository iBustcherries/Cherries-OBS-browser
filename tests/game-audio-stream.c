#define _GNU_SOURCE
#include "packaging/gamecapture/CherriesGameAudio.h"
#include <obs-module.h>
#include <pipewire/pipewire.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("cherries-test", "en-US")

static void *capture;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static double square_sum;
static unsigned long sample_count;

static const char *name(void *unused)
{
	(void)unused;
	return "Game Audio Test";
}
static void *create(obs_data_t *settings, obs_source_t *source)
{
	(void)settings;
	capture = cherries_game_audio_create(source);
	return capture;
}
static void destroy(void *data)
{
	cherries_game_audio_destroy(data);
}
static void received(void *unused, obs_source_t *source, const struct audio_data *audio, bool muted)
{
	(void)unused;
	(void)source;
	if (muted || !audio->data[0])
		return;
	const float *samples = (const float *)audio->data[0];
	pthread_mutex_lock(&lock);
	for (uint32_t i = 0; i < audio->frames; ++i)
		square_sum += (double)samples[i] * samples[i];
	sample_count += audio->frames;
	pthread_mutex_unlock(&lock);
}
static double measure(void)
{
	/* Let route changes and already buffered samples settle. */
	usleep(500000);
	pthread_mutex_lock(&lock);
	square_sum = 0;
	sample_count = 0;
	pthread_mutex_unlock(&lock);
	usleep(1500000);
	pthread_mutex_lock(&lock);
	double result = sample_count ? sqrt(square_sum / sample_count) : 0;
	pthread_mutex_unlock(&lock);
	return result;
}
int main(int argc, char **argv)
{
	if (argc != 3 && argc != 4)
		return 2;
	bool pulse = argc == 4 && strcmp(argv[3], "pulse") == 0;
	pid_t unrelated = (pid_t)strtol(argv[2], NULL, 10);
	if (!obs_startup("en-US", NULL, NULL))
		return 3;
	struct obs_audio_info audio = {.samples_per_sec = 48000, .speakers = SPEAKERS_STEREO};
	if (!obs_reset_audio(&audio))
		return 4;
	pw_init(NULL, NULL);
	const struct obs_source_info info = {
		.id = "cherries_audio_test",
		.type = OBS_SOURCE_TYPE_INPUT,
		.output_flags = OBS_SOURCE_AUDIO,
		.get_name = name,
		.create = create,
		.destroy = destroy,
	};
	obs_register_source(&info);
	obs_source_t *source = obs_source_create_private(info.id, "Game Audio Test", NULL);
	if (!source || !capture)
		return 5;
	obs_set_output_source(1, source);
	obs_source_add_audio_capture_callback(source, received, NULL);
	pid_t game = fork();
	if (game == 0) {
		if (pulse)
			execlp("paplay", "paplay", "--device=cherries-test-sink", argv[1], (char *)NULL);
		else
			execlp("pw-cat", "pw-cat", "--playback", "--target=cherries-test-sink", argv[1], (char *)NULL);
		_exit(127);
	}
	if (game < 0)
		return 6;
	sleep(2);
	cherries_game_audio_set_pid(capture, getpid());
	double selected = measure();
	cherries_game_audio_set_pid(capture, unrelated);
	double switched = measure();
	cherries_game_audio_set_pid(capture, 0);
	double stopped = measure();
	double bridge = 0;
	if (pulse) {
		const char *bridge_text = getenv("CHERRIES_TEST_PULSE_BRIDGE_PID");
		pid_t bridge_pid = bridge_text ? (pid_t)strtol(bridge_text, NULL, 10) : 0;
		if (bridge_pid <= 1)
			return 8;
		cherries_game_audio_set_pid(capture, bridge_pid);
		bridge = measure();
	}
	printf("Audio RMS (%s): selected game %.3f; switched target %.3f; stopped %.3f; bridge %.3f\n",
		pulse ? "pulse" : "native", selected, switched, stopped, bridge);
	kill(game, SIGTERM);
	waitpid(game, NULL, 0);
	obs_source_remove_audio_capture_callback(source, received, NULL);
	obs_set_output_source(1, NULL);
	obs_source_release(source);
	obs_shutdown();
	pw_deinit();
	/* Selected tone amplitude .2, unrelated tone amplitude .6. Capturing the
     * full desktop mix would produce RMS around .447 instead of .141. */
	return selected > .08 && selected < .24 && switched > .32 && switched < .52 && stopped < .005 && bridge < .005 ? 0 : 7;
}

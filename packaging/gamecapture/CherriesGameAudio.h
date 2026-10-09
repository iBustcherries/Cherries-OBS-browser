#pragma once
#include <obs.h>
#include <sys/types.h>

void *cherries_game_audio_create(obs_source_t *source);
void cherries_game_audio_set_pid(void *capture, pid_t pid);
void cherries_game_audio_destroy(void *capture);

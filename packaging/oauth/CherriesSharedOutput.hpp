#pragma once

#include <obs.h>
#include <cstring>

inline bool CherriesShareEncoders(obs_output_t *destination, obs_output_t *source)
{
	if (!source || !destination)
		return false;
	obs_encoder_t *video = obs_output_get_video_encoder(source);
	obs_encoder_t *audio = obs_output_get_audio_encoder(source, 0);
	if (!video || !audio || strcmp(obs_encoder_get_codec(video), "h264") != 0 ||
	    strcmp(obs_encoder_get_codec(audio), "aac") != 0)
		return false;
	obs_output_set_video_encoder(destination, video);
	obs_output_set_audio_encoder(destination, audio, 0);
	return true;
}

#pragma once

#include <obs.h>
#include <cstring>

inline bool CherriesCanShareOutput(obs_output_t *source)
{
	if (!source)
		return false;
	obs_encoder_t *video = obs_output_get_video_encoder(source);
	obs_encoder_t *audio = obs_output_get_audio_encoder(source, 0);
	if (!video || !audio || strcmp(obs_encoder_get_codec(video), "h264") != 0 ||
	    strcmp(obs_encoder_get_codec(audio), "aac") != 0)
		return false;
	return true;
}

inline bool CherriesShareEncoders(obs_output_t *destination, obs_output_t *source)
{
	if (!destination || !CherriesCanShareOutput(source))
		return false;
	obs_output_set_video_encoder(destination, obs_output_get_video_encoder(source));
	obs_output_set_audio_encoder(destination, obs_output_get_audio_encoder(source, 0), 0);
	return true;
}

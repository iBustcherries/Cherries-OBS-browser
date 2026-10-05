#pragma once

#include <obs.h>
#include <cstring>
#include <array>
#include <string>

// One encoder per distinct OBS mix; destinations selecting the same track reuse it.
class CherriesAudioEncoders {
	std::array<obs_encoder_t *, MAX_AUDIO_MIXES> encoders{};

public:
	CherriesAudioEncoders() = default;
	CherriesAudioEncoders(const CherriesAudioEncoders &) = delete;
	CherriesAudioEncoders &operator=(const CherriesAudioEncoders &) = delete;
	~CherriesAudioEncoders() { Clear(); }
	void Clear()
	{
		for (auto &encoder : encoders) {
			if (encoder)
				obs_encoder_release(encoder);
			encoder = nullptr;
		}
	}
	obs_encoder_t *Get(obs_encoder_t *source, int track)
	{
		if (!source || track < 0 || track > MAX_AUDIO_MIXES ||
		    strcmp(obs_encoder_get_codec(source), "aac") != 0)
			return nullptr;
		const size_t sourceMix = obs_encoder_get_mixer_index(source);
		const size_t mix = track == 0 ? sourceMix : size_t(track - 1);
		if (mix >= encoders.size())
			return nullptr;
		auto &encoder = encoders[mix];
		if (encoder)
			return encoder;
		if (mix == sourceMix)
			encoder = obs_encoder_get_ref(source);
		else {
			obs_data_t *settings = obs_encoder_get_settings(source);
			const std::string name = "cherries_audio_track_" + std::to_string(mix + 1);
			encoder = obs_audio_encoder_create(obs_encoder_get_id(source), name.c_str(), settings, mix,
							   nullptr);
			obs_data_release(settings);
			if (encoder)
				obs_encoder_set_audio(encoder, obs_get_audio());
		}
		return encoder;
	}
};

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

inline bool CherriesShareEncoders(obs_output_t *destination, obs_output_t *source, obs_encoder_t *audio = nullptr)
{
	if (!destination || !CherriesCanShareOutput(source))
		return false;
	if (!audio)
		audio = obs_output_get_audio_encoder(source, 0);
	if (strcmp(obs_encoder_get_codec(audio), "aac") != 0)
		return false;
	obs_output_set_video_encoder(destination, obs_output_get_video_encoder(source));
	obs_output_set_audio_encoder(destination, audio, 0);
	return true;
}

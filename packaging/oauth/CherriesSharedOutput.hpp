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
	void Seed(obs_encoder_t *encoder)
	{
		if (!encoder || strcmp(obs_encoder_get_codec(encoder), "aac") != 0)
			return;
		const auto mix = obs_encoder_get_mixer_index(encoder);
		if (mix < encoders.size() && !encoders[mix])
			encoders[mix] = obs_encoder_get_ref(encoder);
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

inline obs_encoder_t *CherriesSharedVideoEncoder(obs_output_t *source)
{
	if (!source)
		return nullptr;
	for (size_t i = 0; i < MAX_OUTPUT_VIDEO_ENCODERS; ++i) {
		auto encoder = obs_output_get_video_encoder2(source, i);
		if (encoder && strcmp(obs_encoder_get_codec(encoder), "h264") == 0)
			return encoder;
	}
	return nullptr;
}

inline bool CherriesCanShareOutput(obs_output_t *source)
{
	if (!source)
		return false;
	obs_encoder_t *video = CherriesSharedVideoEncoder(source);
	obs_encoder_t *audio = obs_output_get_audio_encoder(source, 0);
	if (!video || !audio || strcmp(obs_encoder_get_codec(video), "h264") != 0 ||
	    strcmp(obs_encoder_get_codec(audio), "aac") != 0)
		return false;
	return true;
}

// Twitch uses output audio slot 1 for its VOD mix; other platforms get only slot 0.
inline bool CherriesSetAudioTracks(obs_output_t *output, obs_encoder_t *live, obs_encoder_t *vod = nullptr)
{
	if (!output || !live || strcmp(obs_encoder_get_codec(live), "aac") != 0 ||
	    (vod && strcmp(obs_encoder_get_codec(vod), "aac") != 0))
		return false;
	obs_output_set_audio_encoder(output, live, 0);
	// With identical live/VOD mixes, Twitch's normal archive already has the desired audio.
	obs_output_set_audio_encoder(output, vod == live ? nullptr : vod, 1);
	for (size_t i = 2; i < MAX_OUTPUT_AUDIO_ENCODERS; ++i)
		obs_output_set_audio_encoder(output, nullptr, i);
	return true;
}

inline bool CherriesShareEncoders(obs_output_t *destination, obs_output_t *source, obs_encoder_t *audio = nullptr,
				  obs_encoder_t *vod = nullptr)
{
	if (!destination || !CherriesCanShareOutput(source))
		return false;
	if (!audio)
		audio = obs_output_get_audio_encoder(source, 0);
	if (!CherriesSetAudioTracks(destination, audio, vod))
		return false;
	obs_output_set_video_encoder(destination, CherriesSharedVideoEncoder(source));
	return true;
}

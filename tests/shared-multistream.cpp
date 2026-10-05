#include "packaging/oauth/CherriesSharedOutput.hpp"
#include <cassert>

struct obs_encoder {
	const char *codec;
	size_t mixer = 0;
	int references = 0;
	bool allocated = false;
	bool audioBound = false;
};
struct obs_output {
	obs_encoder_t *video = nullptr;
	obs_encoder_t *audio = nullptr;
};

extern "C" obs_encoder_t *obs_output_get_video_encoder(const obs_output_t *output)
{
	return output->video;
}
extern "C" obs_encoder_t *obs_output_get_audio_encoder(const obs_output_t *output, size_t)
{
	return output->audio;
}
extern "C" const char *obs_encoder_get_codec(const obs_encoder_t *encoder)
{
	return encoder->codec;
}
extern "C" void obs_output_set_video_encoder(obs_output_t *output, obs_encoder_t *encoder)
{
	output->video = encoder;
}
extern "C" void obs_output_set_audio_encoder(obs_output_t *output, obs_encoder_t *encoder, size_t)
{
	output->audio = encoder;
}

static int created = 0;
static int live = 0;
static bool failCreation = false;
struct obs_data {
	int bitrate = 160;
};
static obs_data encoderSettings;
extern "C" size_t obs_encoder_get_mixer_index(const obs_encoder_t *encoder)
{
	return encoder->mixer;
}
extern "C" const char *obs_encoder_get_id(const obs_encoder_t *)
{
	return "ffmpeg_aac";
}
extern "C" obs_encoder_t *obs_encoder_get_ref(obs_encoder_t *encoder)
{
	if (encoder)
		++encoder->references;
	return encoder;
}
extern "C" void obs_encoder_release(obs_encoder_t *encoder)
{
	assert(encoder && encoder->references > 0);
	if (--encoder->references == 0 && encoder->allocated) {
		--live;
		delete encoder;
	}
}
extern "C" obs_data_t *obs_encoder_get_settings(const obs_encoder_t *)
{
	return &encoderSettings;
}
extern "C" void obs_data_release(obs_data_t *) {}
extern "C" audio_t *obs_get_audio()
{
	return reinterpret_cast<audio_t *>(1);
}
extern "C" void obs_encoder_set_audio(obs_encoder_t *encoder, audio_t *audio)
{
	assert(audio == obs_get_audio());
	encoder->audioBound = true;
}
extern "C" obs_encoder_t *obs_audio_encoder_create(const char *id, const char *, obs_data_t *settings, size_t mixer,
						   obs_data_t *)
{
	assert(strcmp(id, "ffmpeg_aac") == 0 && settings->bitrate == 160);
	if (failCreation)
		return nullptr;
	++created;
	++live;
	return new obs_encoder{"aac", mixer, 1, true};
}

int main()
{
	obs_encoder video{"h264"}, audio{"aac"}, av1{"av1"};
	obs_output source{&video, &audio}, twitch, youtube, third;
	assert(CherriesShareEncoders(&twitch, &source));
	assert(CherriesShareEncoders(&youtube, &source));
	assert(CherriesShareEncoders(&third, &source));
	assert(twitch.video == source.video && youtube.video == source.video && third.video == source.video);
	assert(twitch.audio == source.audio && youtube.audio == source.audio && third.audio == source.audio);
	assert(!CherriesShareEncoders(nullptr, &source));
	assert(!CherriesShareEncoders(&third, nullptr));
	source.video = &av1;
	assert(!CherriesShareEncoders(&third, &source));
	assert(third.video == &video && third.audio == &audio);
	source.video = &video;
	source.audio = nullptr;
	assert(!CherriesShareEncoders(&third, &source));
	// Preserve the native streaming track as the default, even when primary selects another track.
	audio.mixer = 1;
	source.audio = &audio;
	{
		CherriesAudioEncoders tracks;
		assert(tracks.Get(&audio, 0) == &audio && tracks.Get(&audio, 2) == &audio);
		auto primaryAudio = tracks.Get(&audio, 3);
		assert(primaryAudio && primaryAudio->mixer == 2 && primaryAudio->audioBound);
		assert(CherriesShareEncoders(&source, &source, primaryAudio));
		assert(CherriesShareEncoders(&twitch, &source, tracks.Get(&audio, 3)));
		assert(CherriesShareEncoders(&youtube, &source, tracks.Get(&audio, 0)));
		assert(twitch.audio == primaryAudio && youtube.audio == &audio);
		assert(twitch.video == &video && youtube.video == &video);
		assert(created == 1); // Every account on track 3 shares the same encoder.
		auto sixth = tracks.Get(&audio, 6);
		assert(sixth && sixth->mixer == 5 && tracks.Get(&audio, 6) == sixth && created == 2);
		assert(!tracks.Get(&audio, -1) && !tracks.Get(&audio, 7) && !tracks.Get(nullptr, 1));
		assert(!tracks.Get(&av1, 1));
		failCreation = true;
		assert(!tracks.Get(&audio, 1));
		failCreation = false;
		assert(tracks.Get(&audio, 1)->mixer == 0); // A failed creation can be retried.
		tracks.Clear();
		assert(live == 0 && audio.references == 0);
		assert(tracks.Get(&audio, 2) == &audio); // Clean cache can be reused for a later stream.
	}
	assert(live == 0 && audio.references == 0);
}

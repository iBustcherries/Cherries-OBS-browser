#include "packaging/oauth/CherriesSharedOutput.hpp"
#include <cassert>

struct obs_encoder {
	const char *codec;
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
}

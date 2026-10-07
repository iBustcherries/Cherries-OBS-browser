#include "packaging/gamecapture/CherriesAudioTarget.h"
#include <assert.h>
#include <unistd.h>
static pid_t parent(pid_t process)
{
	if (process == 103)
		return 102;
	if (process == 102)
		return 100;
	if (process == 200)
		return 200;
	if (process == 300)
		return 301;
	if (process == 301)
		return 300;
	return 1;
}
int main(void)
{
	assert(cherries_audio_parse_pid("100") == 100);
	assert(cherries_audio_parse_pid("2147483647") == INT_MAX);
	assert(cherries_audio_parse_pid("2147483648") == 0);
	assert(cherries_audio_parse_pid("100junk") == 0);
	assert(cherries_audio_parse_pid("-100") == 0);
	assert(cherries_audio_parse_pid("0") == 0);
	assert(cherries_audio_parse_pid("1") == 0);
	assert(cherries_audio_parse_pid("") == 0);
	assert(cherries_audio_parse_pid(NULL) == 0);
	/* The Pulse bridge is process 200, but the game/app is process 103. */
	pid_t pulse = cherries_audio_client_process("pipewire-pulse", "200", "103");
	assert(pulse == 103);
	assert(cherries_audio_matches_process(100, pulse, parent));
	assert(!cherries_audio_matches_process(200, pulse, parent));
	assert(cherries_audio_client_process("pipewire-pulse", "200", NULL) == 0);
	assert(cherries_audio_client_process("pipewire-pulse", "200", "invalid") == 0);
	assert(cherries_audio_client_process("pipewire-pulse", "200", "104") == 104);
	assert(!cherries_audio_matches_process(100,
		cherries_audio_client_process("pipewire-pulse", "200", "104"), parent));
	/* Native clients retain the trusted host PID even if their app PID differs. */
	assert(cherries_audio_client_process("pipewire", "103", "200") == 103);
	assert(cherries_audio_client_process(NULL, "103", "200") == 103);
	assert(cherries_audio_client_process("pipewire", "invalid", "103") == 103);
	assert(cherries_audio_client_process(NULL, NULL, NULL) == 0);
	assert(cherries_audio_matches_process(100, 100, parent));
	assert(cherries_audio_matches_process(100, 103, parent));
	assert(!cherries_audio_matches_process(100, 104, parent));
	assert(!cherries_audio_matches_process(0, 100, parent));
	assert(!cherries_audio_matches_process(100, 0, parent));
	assert(!cherries_audio_matches_process(100, 200, parent));
	assert(!cherries_audio_matches_process(100, 300, parent));
	assert(cherries_audio_parse_parent("103 (game helper) S 102 1 2 3") == 102);
	assert(cherries_audio_parse_parent("103 (game ) helper)) S 102 1 2") == 102);
	assert(cherries_audio_parse_parent("invalid") == 0);
	assert(cherries_audio_parse_parent("103 (game) S -1") == 0);
	assert(cherries_audio_parse_parent(NULL) == 0);
}

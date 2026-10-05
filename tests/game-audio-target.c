#include "packaging/gamecapture/CherriesAudioTarget.h"
#include <assert.h>
#include <unistd.h>
static pid_t parent(pid_t process)
{
    if (process == 103) return 102;
    if (process == 102) return 100;
    if (process == 200) return 200;
    if (process == 300) return 301;
    if (process == 301) return 300;
    return 1;
}
int main(void)
{
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

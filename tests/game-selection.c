#include "packaging/gamecapture/CherriesGameSelection.h"
#include <assert.h>
int main(void)
{
    assert(!cherries_is_game_candidate("steamwebhelper"));
    assert(!cherries_is_game_candidate("steamwebhelper.exe"));
    assert(!cherries_is_game_candidate("EpicGamesLauncher.exe"));
    assert(!cherries_is_game_candidate("obs"));
    assert(!cherries_is_game_candidate(""));
    assert(!cherries_is_game_candidate(NULL));
    assert(cherries_is_game_candidate("eldenring.exe"));
    assert(cherries_is_game_candidate("dota2"));
}

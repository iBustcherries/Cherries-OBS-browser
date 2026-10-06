#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
runtime=$(mktemp -d)
trap 'rm -rf "$runtime"' EXIT
if [ "$#" -gt 0 ]; then
  hook=$1
  test -r "$hook"
else
  hook="$runtime/launch-hook.so"
  mkdir -p "$runtime/include"
  printf '#include <dlfcn.h>\nvoid *real_dlsym(void *, const char *);\n' > "$runtime/include/dlsym.h"
  gcc -std=gnu11 -Wall -Wextra -Werror -fPIC -shared -I"$runtime/include" \
    -DCHERRIES_TEST_RESOLVER -DCHERRIES_CAPTURE_LIBRARY=\""$hook"\" \
    "$repo/packaging/gamecapture/CherriesGameLaunch.c" "$repo/tests/game-launch.c" -ldl -o "$hook"
fi
gcc -std=gnu11 -Wall -Wextra -Werror -DCHERRIES_TEST_GAME "$repo/tests/game-launch.c" -ldl -o "$runtime/deadcells"
gcc -std=gnu11 -Wall -Wextra -Werror "$repo/tests/game-launch.c" -o "$runtime/launch-driver"
cp "$runtime/deadcells" "$runtime/othergame"
printf 'int unused_preload_library;\n' | gcc -x c -fPIC -shared -o "$runtime/keep.so" -
cat > "$runtime/deadcells.sh" <<'EOF'
#!/bin/bash
LD_PRELOAD= LD_LIBRARY_PATH=. CHERRIES_TEST_EXPECT_HOOK=0 ./deadcells detect.hl
test "$?" -eq 23 || exit 1
LD_PRELOAD= LD_LIBRARY_PATH=. CHERRIES_TEST_EXPECT_HOOK=1 ./deadcells 'argument with spaces' 'literal-$value'
EOF
chmod +x "$runtime/deadcells.sh"
original_hash=$(sha256sum "$runtime/deadcells.sh")
export LD_LIBRARY_PATH=.
export CHERRIES_GAMECAPTURE_ACTIVE=1 SteamAppId=588650
export CHERRIES_TEST_LIBRARY="$hook" CHERRIES_TEST_EXPECT_HOOK=1
export PATH="$runtime:$PATH"
cd "$runtime"
expect_exit() {
  local expected=$1
  shift
  local result=0
  "$@" || result=$?
  test "$result" -eq "$expected"
}
# Real shell startup reproduces the game's two LD_PRELOAD-clearing launches.
expect_exit 23 env LD_PRELOAD="$hook" bash ./deadcells.sh
test "$original_hash" = "$(sha256sum "$runtime/deadcells.sh")"
for method in execve execv execvp execvpe posix_spawn posix_spawnp; do
  expect_exit 23 env LD_PRELOAD="$hook" ./launch-driver "$method" deadcells
done
# Preserve unrelated preloads, argument boundaries, parent environment and errno.
expect_exit 23 env CHERRIES_TEST_KEEP_PRELOAD="$runtime/keep.so" LD_PRELOAD="$hook" ./launch-driver execve deadcells
expect_exit 0 env LD_PRELOAD="$hook" ./launch-driver missing deadcells
# Keep ordinary games and unrequested launches completely unchanged.
expect_exit 23 env CHERRIES_TEST_EXPECT_HOOK=0 LD_PRELOAD="$hook" ./launch-driver execve othergame
expect_exit 23 env CHERRIES_TEST_EXPECT_HOOK=0 SteamAppId=12345 LD_PRELOAD="$hook" ./launch-driver execve deadcells
expect_exit 23 env CHERRIES_TEST_EXPECT_HOOK=0 CHERRIES_GAMECAPTURE_ACTIVE=0 LD_PRELOAD="$hook" ./launch-driver execve deadcells
expect_exit 23 env -u SteamAppId SteamGameId=588650 LD_PRELOAD="$hook" ./launch-driver execve deadcells
printf 'Dead Cells hook restoration: shell, exec, spawn, isolation and unchanged game script passed\n'

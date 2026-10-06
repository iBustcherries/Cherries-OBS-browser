#!/bin/bash
set -euo pipefail
export XDG_RUNTIME_DIR
XDG_RUNTIME_DIR=$(mktemp -d)
export XDG_CONFIG_HOME="$XDG_RUNTIME_DIR/config"
mkdir -p "$XDG_CONFIG_HOME/pipewire/pipewire.conf.d"
cat > "$XDG_CONFIG_HOME/pipewire/pipewire.conf.d/99-cherries-test.conf" <<'EOF'
context.objects = [
  { factory = adapter
    args = {
      factory.name = support.null-audio-sink
      node.name = cherries-test-sink
      media.class = Audio/Sink
      audio.position = [ FL FR ]
    }
  }
]
EOF
pipewire > "$XDG_RUNTIME_DIR/pipewire.log" 2>&1 &
server=$!
for attempt in {1..30}; do
    if [ -S "$XDG_RUNTIME_DIR/pipewire-0" ]; then break; fi
    sleep 0.2
done
wireplumber > "$XDG_RUNTIME_DIR/wireplumber.log" 2>&1 &
manager=$!
player=''
display_server=''
cleanup() {
    if [ -n "$player" ]; then kill "$player" 2>/dev/null || true; fi
    kill "$manager" "$server" 2>/dev/null || true
    if [ -n "$display_server" ]; then kill "$display_server" 2>/dev/null || true; fi
    rm -rf "$XDG_RUNTIME_DIR"
}
trap cleanup EXIT
# libobs initializes its Linux display backend even for this audio-only test.
export DISPLAY=:99
Xvfb "$DISPLAY" -screen 0 1280x720x24 -nolisten tcp > "$XDG_RUNTIME_DIR/display.log" 2>&1 &
display_server=$!
for attempt in {1..30}; do
    if [ -S /tmp/.X11-unix/X99 ]; then break; fi
    sleep 0.2
done
ready=false
for attempt in {1..30}; do
    if pw-metadata -n default 0 default.audio.sink '{"name":"cherries-test-sink"}' >/dev/null 2>&1; then
        ready=true
        break
    fi
    sleep 1
done
if [ "$ready" != true ]; then
    cat "$XDG_RUNTIME_DIR/pipewire.log" "$XDG_RUNTIME_DIR/wireplumber.log"
    exit 1
fi
python3 - "$XDG_RUNTIME_DIR" <<'PY'
import array, math, pathlib, sys, wave
root = pathlib.Path(sys.argv[1])
for name, amplitude, frequency in [('game', .2, 440), ('unrelated', .6, 880)]:
    samples = array.array('h')
    for i in range(48000 * 30):
        sample = amplitude * math.sin(2 * math.pi * frequency * i / 48000)
        samples.extend([round(sample * 32767), round(sample * 32767)])
    with wave.open(str(root / (name + '.wav')), 'wb') as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(48000)
        output.writeframes(samples.tobytes())
PY
pw-cat --playback --target=cherries-test-sink "$XDG_RUNTIME_DIR/unrelated.wav" > "$XDG_RUNTIME_DIR/player.log" 2>&1 &
player=$!
if ! ./game-audio-stream-test "$XDG_RUNTIME_DIR/game.wav" "$player"; then
    cat "$XDG_RUNTIME_DIR/pipewire.log" "$XDG_RUNTIME_DIR/wireplumber.log" "$XDG_RUNTIME_DIR/player.log" "$XDG_RUNTIME_DIR/display.log"
    exit 1
fi

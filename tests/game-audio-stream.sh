#!/bin/bash
set -euo pipefail
mode=${1:-native}
test_binary=$(realpath "${2:-./game-audio-stream-test}")
expected=${3:-pass}
if [[ "$mode" != native && "$mode" != pulse ]]; then exit 2; fi
if [[ "$expected" != pass && "$expected" != legacy-failure ]]; then exit 2; fi
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
pulse_server=''
cleanup() {
    if [ -n "$player" ]; then kill "$player" 2>/dev/null || true; fi
    if [ -n "$pulse_server" ]; then kill "$pulse_server" 2>/dev/null || true; fi
    kill "$manager" "$server" 2>/dev/null || true
    if [ -n "$display_server" ]; then
        kill "$display_server" 2>/dev/null || true
        wait "$display_server" 2>/dev/null || true
    fi
    wait 2>/dev/null || true
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
if [[ "$mode" == pulse ]]; then
    export PULSE_SERVER="unix:$XDG_RUNTIME_DIR/pulse/native"
    pipewire-pulse > "$XDG_RUNTIME_DIR/pulse.log" 2>&1 &
    pulse_server=$!
    export CHERRIES_TEST_PULSE_BRIDGE_PID="$pulse_server"
    ready=false
    for attempt in {1..30}; do
        if pactl info >/dev/null 2>&1; then ready=true; break; fi
        sleep 0.2
    done
    if [[ "$ready" != true ]]; then cat "$XDG_RUNTIME_DIR/pulse.log"; exit 1; fi
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
if [[ "$mode" == pulse ]]; then
    paplay --device=cherries-test-sink "$XDG_RUNTIME_DIR/unrelated.wav" > "$XDG_RUNTIME_DIR/player.log" 2>&1 &
else
    pw-cat --playback --target=cherries-test-sink "$XDG_RUNTIME_DIR/unrelated.wav" > "$XDG_RUNTIME_DIR/player.log" 2>&1 &
fi
player=$!
result=0
"$test_binary" "$XDG_RUNTIME_DIR/game.wav" "$player" "$mode" > "$XDG_RUNTIME_DIR/result.log" 2>&1 || result=$?
cat "$XDG_RUNTIME_DIR/result.log"
if [[ "$expected" == legacy-failure ]]; then
    if [[ "$result" != 7 ]] || ! grep -Eq 'Audio RMS \(pulse\): selected game 0\.000; switched target 0\.000' "$XDG_RUNTIME_DIR/result.log"; then
        echo 'Expected legacy Pulse bridge PID mismatch was not reproduced' >&2
        exit 1
    fi
    echo 'Legacy Pulse bridge PID mismatch reproduced; selected game and switched app were silent'
elif [[ "$result" != 0 ]]; then
    cat "$XDG_RUNTIME_DIR/pipewire.log" "$XDG_RUNTIME_DIR/wireplumber.log" "$XDG_RUNTIME_DIR/player.log" "$XDG_RUNTIME_DIR/display.log"
    if [[ -f "$XDG_RUNTIME_DIR/pulse.log" ]]; then cat "$XDG_RUNTIME_DIR/pulse.log"; fi
    exit 1
fi

#!/bin/bash
# Reads a REAL compositor's screen-sharing stream with the server's PipeWire capture: a headless GNOME compositor
# (mutter) with one 800x600 virtual monitor, a fullscreen terminal painted solid red (foot), and mutter's own
# ScreenCast interface giving the stream. The test binary must find the middle, a corner and the last pixel red.
# It does NOT go through the desktop portal (see the "live" test in WaylandTests.cpp for that).
#
#   Tools/wayland/RunMutterCaptureCheck.sh [path to MarconiServerTest]
#
# Needs: mutter, foot, pipewire, wireplumber, dbus-daemon, python3-dbus and python3-gi (Debian and Ubuntu: apt install
# mutter foot pipewire wireplumber dbus python3-dbus python3-gi). Set PYTHON if the python with dbus isn't python3.
set -u
BIN=${1:-Build/MarconiServerTest/Debug/MarconiServerTest}
HERE=$(cd "$(dirname "$0")" && pwd)
PYTHON=${PYTHON:-python3}
DIR=$(mktemp -d /tmp/marconi-mutter-XXXXXX)
chmod 700 "$DIR"
export XDG_RUNTIME_DIR=$DIR
export DBUS_SESSION_BUS_ADDRESS=unix:path=$DIR/bus
export XDG_SESSION_TYPE=wayland
PIDS=()
cleanup() { kill "${PIDS[@]}" 2>/dev/null; rm -rf "$DIR"; }
trap cleanup EXIT
dbus-daemon --session --address="$DBUS_SESSION_BUS_ADDRESS" --nofork --nopidfile >"$DIR/dbus.log" 2>&1 &
PIDS+=($!)
sleep 1
pipewire >"$DIR/pipewire.log" 2>&1 &
PIDS+=($!)
sleep 1
wireplumber >"$DIR/wireplumber.log" 2>&1 &
PIDS+=($!)
sleep 1
mutter --headless --wayland --no-x11 --virtual-monitor 800x600 --wayland-display=wayland-check >"$DIR/mutter.log" 2>&1 &
PIDS+=($!)
sleep 4
WAYLAND_DISPLAY=wayland-check foot -F -o colors.background=ff0000 -o colors.alpha=1.0 -e sleep 600 >"$DIR/foot.log" 2>&1 &
PIDS+=($!)
sleep 3
"$PYTHON" "$HERE/mutter_cast.py" 2 60 >"$DIR/cast.out" 2>"$DIR/cast.err" &
PIDS+=($!)
sleep 3
NODE=$(grep NODE "$DIR/cast.out" | head -1 | awk '{print $2}')
if [ -z "$NODE" ]; then
    echo "mutter gave no screen-cast stream; its messages:"
    cat "$DIR/cast.err" "$DIR/mutter.log"
    exit 2
fi
echo "mutter's stream is PipeWire node $NODE"
MARCONI_PIPEWIRE_NODE=$NODE MARCONI_PIPEWIRE_COLOUR=ff0000 "$BIN"

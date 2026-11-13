#!/bin/bash
# Runs MarconiServerTest with a private PipeWire daemon and session manager (and a private D-Bus session), so the
# Wayland capture and input tests that read a real PipeWire stream run instead of printing SKIP.
#
#   Tools/wayland/RunPipeWireTests.sh [path to MarconiServerTest]
#
# Needs the pipewire, wireplumber and dbus-daemon programs (Debian and Ubuntu: apt install pipewire wireplumber
# dbus). Nothing here touches the real desktop's PipeWire or bus: everything lives in a temporary folder.
set -u
BIN=${1:-Build/MarconiServerTest/Debug/MarconiServerTest}
DIR=$(mktemp -d /tmp/marconi-pw-XXXXXX)
chmod 700 "$DIR"
export XDG_RUNTIME_DIR=$DIR
export DBUS_SESSION_BUS_ADDRESS=unix:path=$DIR/bus
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
sleep 2
"$BIN"

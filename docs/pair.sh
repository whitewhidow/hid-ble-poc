#!/usr/bin/env bash
# PoC-KBD pairing helper. Usage: curl -sL .../pair.sh | bash -s <MAC>
mac="${1:?usage: pair.sh <MAC>}"
rfkill unblock bluetooth 2>/dev/null
bluetoothctl power on >/dev/null 2>&1
bluetoothctl remove "$mac" >/dev/null 2>&1
echo "scanning for $mac (waits until found)..."
bluetoothctl --timeout 600 scan on >/dev/null 2>&1 &
sp=$!
until bluetoothctl devices | grep -qi "$mac"; do sleep 1; done
echo "found -> trust + pair + connect"
{ sleep 1; echo "agent NoInputNoOutput"; echo default-agent; echo "trust $mac"; echo "pair $mac"; sleep 5; echo "connect $mac"; sleep 3; echo quit; } | bluetoothctl
kill "$sp" >/dev/null 2>&1
echo "done."

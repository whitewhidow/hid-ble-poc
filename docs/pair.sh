#!/usr/bin/env bash
# PoC-KBD pairing helper. Usage: curl -sL .../pair.sh | bash -s <MAC>
# If already connected, do nothing (avoids the connect/reconnect churn when this
# runs twice). Otherwise do a fresh remove + pair + connect, and stop scanning as
# soon as the board is found (a lingering scan destabilises the link).
mac="${1:?usage: pair.sh <MAC>}"
mac="${mac^^}"

rfkill unblock bluetooth 2>/dev/null
bluetoothctl power on >/dev/null 2>&1

if bluetoothctl info "$mac" 2>/dev/null | grep -q "Connected: yes"; then
    echo "already connected -> nothing to do."
    exit 0
fi

# Not connected: clear any stale/half bond on both sides and pair fresh.
bluetoothctl remove "$mac" >/dev/null 2>&1
echo "scanning for $mac (waits until found)..."
bluetoothctl scan on >/dev/null 2>&1 &
sp=$!
until bluetoothctl devices | grep -qi "$mac"; do sleep 1; done
bluetoothctl scan off >/dev/null 2>&1
kill "$sp" >/dev/null 2>&1

echo "found -> trust + pair + connect"
{ sleep 1; echo "agent NoInputNoOutput"; echo default-agent; echo "trust $mac"; echo "pair $mac"; sleep 5; echo "connect $mac"; sleep 3; echo quit; } | bluetoothctl
echo "done."

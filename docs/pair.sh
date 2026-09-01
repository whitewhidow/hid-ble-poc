#!/usr/bin/env bash
# PoC-KBD pairing helper. Usage: curl -sL .../pair.sh | bash -s <MAC>
# Skip entirely if already connected (stops the connect/reconnect churn when this
# runs twice). Otherwise pair fresh, keeping the scan running THROUGH pair+connect
# (BlueZ needs it to stay connectable) and stopping it only afterwards.
mac="${1:?usage: pair.sh <MAC>}"
mac="${mac^^}"

rfkill unblock bluetooth 2>/dev/null
bluetoothctl power on >/dev/null 2>&1

if bluetoothctl info "$mac" 2>/dev/null | grep -q "Connected: yes"; then
    echo "already connected -> nothing to do."
    exit 0
fi

bluetoothctl remove "$mac" >/dev/null 2>&1
echo "scanning for $mac (waits until found)..."
bluetoothctl scan on >/dev/null 2>&1 &
sp=$!
until bluetoothctl devices | grep -qi "$mac"; do sleep 1; done

echo "found -> trust + pair + connect"
{ sleep 1; echo "agent NoInputNoOutput"; echo default-agent; echo "trust $mac"; echo "pair $mac"; sleep 5; echo "connect $mac"; sleep 3; echo quit; } | bluetoothctl

kill "$sp" >/dev/null 2>&1        # stop scanning only AFTER pairing (a lingering scan churns the link)
bluetoothctl scan off >/dev/null 2>&1
echo "done."

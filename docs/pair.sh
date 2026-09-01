#!/usr/bin/env bash
# PoC-KBD pairing helper. Usage: curl -sL .../pair.sh | bash -s <MAC>
# Idempotent: if already connected it does nothing; if already paired it just
# reconnects; only a truly unpaired device triggers a scan + pair. It also stops
# scanning as soon as the board is found (a lingering scan destabilises the link
# and causes a connect/reconnect loop, especially if this is run twice).
mac="${1:?usage: pair.sh <MAC>}"
mac="${mac^^}"

rfkill unblock bluetooth 2>/dev/null
bluetoothctl power on >/dev/null 2>&1

info() { bluetoothctl info "$mac" 2>/dev/null; }

if info | grep -q "Connected: yes"; then
    echo "already connected -> nothing to do."
    exit 0
fi

if info | grep -q "Paired: yes"; then
    echo "already paired -> reconnecting..."
    bluetoothctl connect "$mac" >/dev/null 2>&1
    echo "done."
    exit 0
fi

# Fresh pair.
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

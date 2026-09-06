# Relay

A tiny mailbox that lets you drive a board over the internet, from beyond BLE range.
The board joins your Wi-Fi (STA) and long-polls the relay for commands; the portal
posts commands and long-polls for replies. It's the same command set as the BLE
control service — only the transport changes — so the board keeps working as a normal
BLE device at the same time (STA + BLE coexist; SoftAP is deliberately avoided).

No database, all in memory, zero dependencies.

## Run it locally (test on your LAN first)

```
node relay.js                 # listens on :8080, no auth
```

Point the board and the portal at your machine's LAN IP, e.g. `http://192.168.1.50:8080`.
The board must be on the same Wi-Fi. This proves the whole handoff before you deploy.

## Deploy it (reach the board from anywhere)

Any Node host works. On [Render](https://render.com): New → Blueprint → pick this repo
(`relay/render.yaml`), set `RELAY_TOKEN` to a long random string. You get a URL like
`https://yourboard-relay.onrender.com`; use that as the relay URL and send the same
token from the portal.

> Set `RELAY_TOKEN` for anything internet-facing — a board that types/controls on
> command is a real capability. With a token set, every request must send
> `x-relay-token: <token>`.

## Endpoints

| Method / path | Who | What |
|---|---|---|
| `POST /cmd/<id>` | portal | queue a command for the board |
| `GET /pull/<id>` | board | long-poll (~25s) for the next command |
| `POST /reply/<id>` | board | queue a reply for the portal |
| `GET /poll/<id>` | portal | long-poll (~25s) for the next reply |
| `GET /health` | — | liveness check |

`<id>` is the board's relay id (shown by the board when it goes remote; derived from its
BLE MAC). One id = one board's mailbox.

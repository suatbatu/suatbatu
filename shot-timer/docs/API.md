# HTTP and WebSocket API

Base URL is the device: `http://192.168.4.1` in access-point mode, or whatever
address the OLED footer shows when it has joined your network.

## Authentication

Everything — the UI, every endpoint, the WebSocket — is behind **HTTP basic
auth**. Credentials are `webUser` / `webPass` from settings.

**The web interface does not start at all until a password is set.** That is
deliberate: the device's default network is an open access point, and an
unauthenticated timer on an open AP lets anyone within range fire the buzzer
into the middle of someone's string.

To set the first password, build once with a `firmware/include/secrets.h`:

```c
#pragma once
#define BOOTSTRAP_WEB_USER "shooter"
#define BOOTSTRAP_WEB_PASS "something-long"
```

…and set them from `setup()`, or set them over the serial monitor. After that,
change them from the Settings tab like anything else. `secrets.h` is
gitignored.

### Why the access point is open

An open AP with a password-protected application is a deliberate trade. The AP
carries nothing worth protecting on its own, the application behind it is
authenticated, and a shooter with cold hands between stages should not be typing
a WPA key into a phone. If you would rather have WPA2, it is one call in
`Net::startAp()`.

### WebSocket authentication

Browsers cannot attach an `Authorization` header to a WebSocket handshake, and
basic-auth replay on the handshake is inconsistent between browsers. So the
socket authenticates itself:

1. `GET /api/wstoken` (basic auth) → `{"token": "..."}`
2. Open `ws://<device>/ws`
3. Send the token as the **first frame**

Until a valid token arrives, the client receives nothing. An invalid one is
closed with code 1008. The token is regenerated on every boot.

## Endpoints

### `GET /api/status`

```json
{
  "state": "running",
  "elapsedMs": 4312,
  "armed": true,
  "levelPerMille": 412,
  "floorPerMille": 96,
  "string": {
    "id": 17, "count": 6,
    "firstMs": 1180, "totalMs": 4045,
    "bestSplitMs": 190, "worstSplitMs": 410,
    "shots": [1180, 1450, 1640, 2100, 3635, 4045]
  },
  "stored": 23,
  "ip": "192.168.4.1",
  "ap": true,
  "heap": 214512
}
```

`state` is one of `ready`, `countdown`, `running`, `review`, `menu`.

**The time remaining in a countdown is never published.** Anyone who can read it
has the start cue that the random delay exists to take away.

`levelPerMille` and `floorPerMille` are 0–1000 over an 80 dB window, for the
level meter — not calibrated SPL.

### `GET /api/settings` · `POST /api/settings`

`GET` returns every setting. Passwords are never returned; `wifiPassSet` and
`webPassSet` report whether one is configured.

`POST` takes a JSON object and applies **only the keys present**, so a client can
send a single field. Values are clamped to their valid range rather than
rejected. The response is the full resulting settings plus `"accepted"` — `false`
means at least one value was refused outright (currently only a web password
shorter than 8 characters). Settings are saved to NVS on every accepted POST.

Omit `webPass` / `wifiPass` to leave them unchanged.

### `POST /api/start` · `POST /api/stop`

Queue a start or stop. These run on the web server's task, so they do not
execute the transition themselves — they set a flag the main loop consumes on
its next pass. **The response body therefore reflects the state *before* the
request took effect**; watch the WebSocket for the transition, which follows
within a few milliseconds.

`start` during a countdown or an open string means *abort*, matching the button.

### `GET /api/strings`

Summaries of every stored string, newest first, **without** the shot arrays:

```json
{"strings": [{"id": 17, "count": 6, "firstMs": 1180, "totalMs": 4045,
              "bestSplitMs": 190, "worstSplitMs": 410}]}
```

### `GET /api/string?id=N`

One stored string, in full, including `shots`. `404` if it has aged out of the
50-string history.

### `DELETE /api/strings`

Clears the history. Ids are not reused afterwards.

### `GET /api/export.csv`

The whole history, streamed:

```csv
string_id,shot,time_s,split_s
17,1,1.18,1.18
17,2,1.45,0.27
```

Shot 1's `split_s` is its time from the beep — the draw — matching how the
device and every commercial timer report it.

## WebSocket events

All frames are JSON objects with a `type`.

| `type` | Payload | When |
|---|---|---|
| `state` | full `/api/status` body | every state transition, and on connect |
| `beep` | — | the start signal fired; **t = 0 for the string** |
| `shot` | `index`, `atMs`, `splitMs` | a shot was detected |
| `par` | `index`, `atMs` | a par beep sounded |
| `tick` | `elapsedMs` | 5 Hz while a string is open |
| `end` | `reason` (`manual`/`timeout`), `saveFailed`, `string` | string closed |

`tick` exists so a browser can sweep a smooth clock without inventing numbers:
interpolate between ticks, re-anchor on every one. Every value the UI displays
originates on the device.

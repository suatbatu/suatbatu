# Security model

This device controls medication and (optionally) a camera inside someone's
home. Treat its security seriously. This document is deliberately blunt about
what is and isn't protected.

## What the firmware gives you

- **Password-protected web server.** The dispenser's web UI and API require a
  login. Credentials are stored **salted + SHA-256 hashed** in NVS (flash), not
  in plaintext. The plaintext in `secrets.h` is only the *initial seed* used on
  first boot; change it from the UI afterwards.
- **Session tokens, not persisted passwords.** A successful login issues a
  128-bit random token (`esp_random()`), stored server-side with an expiry and
  sent as an `HttpOnly; SameSite=Strict` cookie. No password is kept in the
  browser.
- **Login rate-limiting.** After `AUTH_MAX_ATTEMPTS` failures the device locks
  out new attempts for `AUTH_LOCKOUT_MS`, throttling brute force.
- **Camera node auth.** The ESP32-CAM's `/snapshot` and `/stream` endpoints
  require HTTP Basic auth (its own credentials), and it only ever *pushes*
  images out to Telegram — it does not accept inbound control.

## What the firmware does NOT give you (and how to cover it)

### 1. Transport encryption (TLS) for the web UI

The ESP32 async web server serves **plain HTTP on the LAN**. Passwords and
session cookies are therefore in the clear *on your local network*. That's an
acceptable risk on a trusted home LAN, but **must not be exposed directly to the
internet.**

**Do NOT port-forward the ESP32.** Bare ESP32/ESP32-CAM web servers are a
well-known mass-hijack target. To reach the device from outside the home, pick
one of these instead — in order of preference:

1. **VPN into the home network** (recommended). Run **WireGuard** or
   **Tailscale** on an always-on box already in the house — a Raspberry Pi, an
   old Android phone, or a router with WireGuard support. Family devices join
   the VPN and reach `http://pillpilot.local` as if they were home. Nothing is
   exposed publicly.
2. **TLS reverse proxy.** Run **Caddy** or **nginx** on that same small box with
   a real certificate (Let's Encrypt) and HTTP Basic/OAuth in front, proxying to
   the ESP32 on the LAN. Now the public endpoint is HTTPS and hardened, and the
   ESP32 is never directly reachable.
3. **Cloudflare Tunnel** with Access policies, pointed at the LAN address.

Telegram (alerts + camera snapshots) already runs over TLS to Telegram's API,
so **the alerting path is encrypted end-to-end regardless** — even before you
set up remote web access.

### 2. Credential hygiene

- Use a **long, unique** web-admin password and a **different** one for the
  camera node.
- Change both after first boot.
- Rotate the **Telegram bot token** if it ever leaks; restrict the bot to a
  private group and set a numeric `TELEGRAM_ALLOWED_CHAT_ID` allow-list so
  strangers can't command it.
- Put IoT devices on a **separate Wi-Fi VLAN/guest network** if your router
  supports it.

### 3. MQTT

Use an authenticated broker. For a cloud broker (e.g. HiveMQ Cloud) use its
TLS port (8883) with `WiFiClientSecure` — the firmware supports this via
`MQTT_USE_TLS`. For a self-hosted Mosquitto, require username/password and,
ideally, TLS. Never use an open/anonymous broker for this.

## Privacy (the camera)

- **Aim the camera at the pill tray, not the room or the people.** It is a
  device monitor, not a surveillance camera. This is both an ethics and a
  consent matter — the person being monitored should understand and, as far as
  they're able, agree.
- Prefer **event snapshots** over a continuous stream; the firmware defaults to
  capture-on-event.
- Images are sent to a **private** Telegram chat. Don't route them anywhere
  public or unauthenticated.
- An indicator LED lights when the camera is active — keep it visible.

## Threat model summary

| Threat | Mitigation |
|---|---|
| Someone on the LAN opens the UI | Password + session auth |
| Brute-force the login | Rate-limit + lockout |
| Password stored in flash is dumped | Salted SHA-256, not plaintext |
| Device exposed to the internet | **Don't.** VPN or TLS reverse proxy only |
| Telegram bot commanded by a stranger | Chat-ID allow-list |
| MQTT sniffed/spoofed | Authenticated broker + TLS |
| Camera stream leaked | Auth'd, LAN-only, push-out to private chat |

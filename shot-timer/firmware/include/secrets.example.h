// Copy to secrets.h and fill in. secrets.h is gitignored and never committed.
//
// These values seed an *unconfigured* device. Once a password has been set —
// here on first boot, or later from the Settings tab — the stored value wins,
// so leaving this file in place does not undo a password you changed.
//
// The web interface refuses to start without a password, so at minimum define
// BOOTSTRAP_WEB_PASS before the first flash.
#pragma once

#define BOOTSTRAP_WEB_USER "shooter"
#define BOOTSTRAP_WEB_PASS "change-this-please"

// Optional. Leave both undefined to always come up as an access point, which is
// what you want at a range. Define them and the device tries your network first
// and falls back to its own AP if it is not there.
// #define BOOTSTRAP_WIFI_SSID "my-network"
// #define BOOTSTRAP_WIFI_PASS "my-wifi-password"

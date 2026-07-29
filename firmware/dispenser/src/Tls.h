// Tls.h — one place to configure certificate verification on a secure client.
//
// Default (TLS_USE_BUNDLE=1): verify against the arduino-esp32 built-in Mozilla
// root bundle, so any publicly-trusted server cert validates and issuer rotation
// doesn't break us. Fallback (TLS_USE_BUNDLE=0): pin TELEGRAM_ROOT_CA from
// secrets.h. TLS_INSECURE=1 disables verification (debugging only).
#pragma once
#include <WiFiClientSecure.h>
#include "config.h"
#include "secrets.h"

#if !TLS_INSECURE && TLS_USE_BUNDLE
// The esp_crt_bundle component embeds the Mozilla roots between these symbols.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[]   asm("_binary_x509_crt_bundle_end");
#endif

inline void configureTls(WiFiClientSecure& client) {
#if TLS_INSECURE
  client.setInsecure();
#elif TLS_USE_BUNDLE
  client.setCACertBundle(rootca_crt_bundle_start,
                         (size_t)(rootca_crt_bundle_end - rootca_crt_bundle_start));
#else
  client.setCACert(TELEGRAM_ROOT_CA);   // define TELEGRAM_ROOT_CA in secrets.h
#endif
}

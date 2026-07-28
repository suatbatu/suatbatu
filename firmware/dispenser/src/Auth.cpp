#include "Auth.h"
#include "mbedtls/sha256.h"
#include <esp_system.h>
#include <time.h>

static String toHex(const uint8_t* buf, size_t len) {
  static const char* H = "0123456789abcdef";
  String s;
  s.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    s += H[(buf[i] >> 4) & 0xF];
    s += H[buf[i] & 0xF];
  }
  return s;
}

static size_t fromHex(const String& hex, uint8_t* out, size_t maxOut) {
  size_t n = 0;
  for (size_t i = 0; i + 1 < (size_t)hex.length() && n < maxOut; i += 2) {
    auto nib = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return 0;
    };
    out[n++] = (nib(hex[i]) << 4) | nib(hex[i + 1]);
  }
  return n;
}

String Auth::randomHex(size_t nBytes) {
  uint8_t buf[64];
  if (nBytes > sizeof(buf)) nBytes = sizeof(buf);
  for (size_t i = 0; i < nBytes; i++) buf[i] = (uint8_t)(esp_random() & 0xFF);
  return toHex(buf, nBytes);
}

String Auth::sha256Hex(const String& saltHex, const String& pass) {
  uint8_t salt[32];
  size_t saltLen = fromHex(saltHex, salt, sizeof(salt));

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);                       // 0 = SHA-256
  mbedtls_sha256_update(&ctx, salt, saltLen);
  mbedtls_sha256_update(&ctx, (const uint8_t*)pass.c_str(), pass.length());
  uint8_t out[32];
  mbedtls_sha256_finish(&ctx, out);
  mbedtls_sha256_free(&ctx);
  return toHex(out, sizeof(out));
}

void Auth::begin(Storage* storage, const char* seedUser, const char* seedPass) {
  storage_ = storage;
  for (auto& s : sessions_) { s.token[0] = 0; s.expiresAt = 0; }

  if (storage_->loadAuth(user_, saltHex_, hashHex_)) return;   // already provisioned

  // First boot: seed from secrets.h.
  user_    = seedUser;
  saltHex_ = randomHex(16);
  hashHex_ = sha256Hex(saltHex_, seedPass);
  storage_->saveAuth(user_, saltHex_, hashHex_);
}

bool Auth::isLockedOut() {
  return (int32_t)(lockoutUntil_ - millis()) > 0;
}

bool Auth::check(const String& user, const String& pass) {
  if (isLockedOut()) return false;

  bool ok = user.equals(user_) && sha256Hex(saltHex_, pass).equals(hashHex_);
  if (ok) {
    failCount_ = 0;
    return true;
  }
  if (++failCount_ >= AUTH_MAX_ATTEMPTS) {
    lockoutUntil_ = millis() + AUTH_LOCKOUT_MS;
    failCount_    = 0;
  }
  return false;
}

bool Auth::setPassword(const String& user, const String& newPass) {
  if (newPass.length() < 8) return false;   // basic strength floor
  user_    = user.length() ? user : user_;
  saltHex_ = randomHex(16);
  hashHex_ = sha256Hex(saltHex_, newPass);
  storage_->saveAuth(user_, saltHex_, hashHex_);
  return true;
}

String Auth::createSession() {
  time_t now = time(nullptr);
  uint32_t exp = (uint32_t)now + SESSION_TTL_SECONDS;

  // Find a free or expired slot.
  for (auto& s : sessions_) {
    if (s.token[0] == 0 || (uint32_t)now > s.expiresAt) {
      String tok = randomHex(16);   // 128-bit
      tok.toCharArray(s.token, sizeof(s.token));
      s.expiresAt = exp;
      return tok;
    }
  }
  return "";   // session table full
}

bool Auth::validateSession(const String& token) {
  if (token.length() != 32) return false;
  time_t now = time(nullptr);
  for (auto& s : sessions_) {
    if (s.token[0] && token.equals(s.token)) {
      if ((uint32_t)now > s.expiresAt) { s.token[0] = 0; return false; }
      return true;
    }
  }
  return false;
}

void Auth::destroySession(const String& token) {
  for (auto& s : sessions_) {
    if (s.token[0] && token.equals(s.token)) s.token[0] = 0;
  }
}

String Auth::cookieToken(const String& cookieHeader) {
  int i = cookieHeader.indexOf("SESSION=");
  if (i < 0) return "";
  i += 8;
  int end = cookieHeader.indexOf(';', i);
  if (end < 0) end = cookieHeader.length();
  String t = cookieHeader.substring(i, end);
  t.trim();
  return t;
}

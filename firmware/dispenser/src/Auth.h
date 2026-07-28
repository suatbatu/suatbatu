// Auth.h — password auth (salted SHA-256) + in-memory session tokens.
//
// Security notes:
//  - Passwords are never stored in plaintext; only salt + SHA-256(salt||pass).
//  - Sessions are random 128-bit tokens with a TTL, delivered as HttpOnly cookies.
//  - Failed logins are rate-limited with a lockout window.
//  - Transport is plain HTTP on the LAN — see docs/SECURITY.md for TLS/VPN.
#pragma once
#include <Arduino.h>
#include "Storage.h"
#include "config.h"

class Auth {
public:
  // Seeds credentials from secrets.h on first boot (if none stored yet).
  void begin(Storage* storage, const char* seedUser, const char* seedPass);

  // Verify username/password. Applies rate-limiting; returns false when locked.
  bool check(const String& user, const String& pass);

  // Change the stored password (after an authenticated request).
  bool setPassword(const String& user, const String& newPass);

  bool isLockedOut();
  String username() const { return user_; }

  // Sessions
  String createSession();                 // returns token (empty on failure)
  bool   validateSession(const String& token);
  void   destroySession(const String& token);

  // Parse the SESSION cookie value out of a Cookie header.
  static String cookieToken(const String& cookieHeader);

  // SHA-256(saltHex-bytes || utf8(pass)) as lowercase hex. Public for reuse.
  static String sha256Hex(const String& saltHex, const String& pass);
  static String randomHex(size_t nBytes);

private:
  Storage* storage_ = nullptr;
  String   user_, saltHex_, hashHex_;

  struct Session { char token[33]; uint32_t expiresAt; };
  Session  sessions_[MAX_SESSIONS];

  uint8_t  failCount_    = 0;
  uint32_t lockoutUntil_ = 0;   // millis()
};

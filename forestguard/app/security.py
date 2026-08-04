"""Auth helpers. Constant-time comparisons so a timing side-channel can't be
used to guess the admin token or an ingest key.
"""

from __future__ import annotations

import hmac

from .config import Config


def check_ingest_key(cfg: Config, presented: str | None) -> bool:
    """True if ``presented`` matches a configured ingest key.

    If no ingest keys are configured the endpoint is open — intended only for a
    trusted LAN / bench setup. Deployment docs push hard toward setting keys.
    """
    if not cfg.ingest_keys:
        return True
    if not presented:
        return False
    return any(hmac.compare_digest(presented, k) for k in cfg.ingest_keys)


def check_admin(cfg: Config, auth_header: str | None) -> bool:
    """Validate a ``Authorization: Bearer <token>`` header against the admin
    token. Also accepts a raw token (no ``Bearer`` prefix) for convenience."""
    if not cfg.admin_token:
        # No token set: allow only if the operator explicitly opened the API.
        return cfg.dashboard_public
    if not auth_header:
        return False
    token = auth_header.strip()
    if token.lower().startswith("bearer "):
        token = token[7:].strip()
    return hmac.compare_digest(token, cfg.admin_token)


def read_allowed(cfg: Config, auth_header: str | None) -> bool:
    """Read (GET) access: open when dashboard_public, else requires admin."""
    if cfg.dashboard_public:
        return True
    return check_admin(cfg, auth_header)

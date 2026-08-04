"""Notification channels for alerts: Telegram, a generic JSON webhook, and the
server log. All are best-effort with short timeouts — a notification failure
must never break ingestion or crash the monitor. Delivery runs on a background
worker thread so a slow endpoint never stalls a request.
"""

from __future__ import annotations

import json
import logging
import queue
import threading
import urllib.parse
import urllib.request

from .config import Config

log = logging.getLogger("forestguard.notify")

_EMOJI = {"critical": "🔥", "warning": "⚠️", "elevated": "🟡",
          "offline": "📴", "online": "✅", "ok": "🟢"}


class Notifier:
    """Fans an alert message out to every configured channel, off-thread."""

    def __init__(self, cfg: Config):
        self.cfg = cfg
        self._q: "queue.Queue[dict | None]" = queue.Queue(maxsize=1000)
        self._thread = threading.Thread(target=self._run, name="fg-notify", daemon=True)
        self._started = False

    def start(self) -> None:
        if not self._started:
            self._started = True
            self._thread.start()

    def stop(self) -> None:
        if self._started:
            self._q.put(None)

    def send(self, level: str, title: str, body: str = "", data: dict | None = None) -> None:
        """Queue an alert. Drops (with a log line) if the queue is saturated."""
        msg = {"level": level, "title": title, "body": body, "data": data or {}}
        if not self._started:
            # Synchronous fallback (e.g. in tests) — still best-effort.
            self._dispatch(msg)
            return
        try:
            self._q.put_nowait(msg)
        except queue.Full:
            log.warning("notify queue full, dropping alert: %s", title)

    # -- internals ------------------------------------------------------
    def _run(self) -> None:
        while True:
            msg = self._q.get()
            if msg is None:
                return
            try:
                self._dispatch(msg)
            except Exception:  # noqa: BLE001 - never let the worker die
                log.exception("notification dispatch failed")

    def _dispatch(self, msg: dict) -> None:
        emoji = _EMOJI.get(msg["level"], "•")
        text = f"{emoji} ForestGuard [{msg['level'].upper()}] {msg['title']}"
        if msg["body"]:
            text += f"\n{msg['body']}"
        log.info("ALERT %s", text.replace("\n", " | "))
        if self.cfg.telegram_token and self.cfg.telegram_chat_id:
            self._telegram(text)
        if self.cfg.webhook_url:
            self._webhook(msg, text)

    def _telegram(self, text: str) -> None:
        url = f"https://api.telegram.org/bot{self.cfg.telegram_token}/sendMessage"
        payload = urllib.parse.urlencode(
            {"chat_id": self.cfg.telegram_chat_id, "text": text}
        ).encode()
        try:
            with urllib.request.urlopen(url, data=payload, timeout=5) as resp:
                resp.read()
        except Exception as exc:  # noqa: BLE001
            log.warning("telegram send failed: %s", exc)

    def _webhook(self, msg: dict, text: str) -> None:
        payload = json.dumps({**msg, "text": text}).encode()
        req = urllib.request.Request(
            self.cfg.webhook_url, data=payload,
            headers={"Content-Type": "application/json"}, method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=5) as resp:
                resp.read()
        except Exception as exc:  # noqa: BLE001
            log.warning("webhook send failed: %s", exc)

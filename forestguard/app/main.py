"""Entrypoint: ``python -m app.main`` (or the console entry in run.py).

Loads config from the environment / .env, starts the background monitor and
notifier, and serves until interrupted.
"""

from __future__ import annotations

import logging
import os
import signal
import sys

from . import __version__
from .config import load_config
from .server import create_server


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(
        level=os.environ.get("FG_LOG_LEVEL", "INFO").upper(),
        format="%(asctime)s %(levelname)-7s %(name)s: %(message)s",
    )
    log = logging.getLogger("forestguard")

    cfg = load_config()
    httpd, app = create_server(cfg)
    app.start_background()

    log.info("ForestGuard %s listening on http://%s:%d (mode=%s, cloud=%s, db=%s)",
             __version__, cfg.host, cfg.port, cfg.mode,
             "on" if cfg.cloud_enabled else "off", cfg.db_path)
    if not cfg.admin_token and not cfg.dashboard_public:
        log.warning("No FG_ADMIN_TOKEN set and dashboard not public: the API/"
                    "dashboard will reject all reads. Set one in your .env.")
    if not cfg.ingest_keys:
        log.warning("No FG_INGEST_KEYS set: telemetry ingestion is OPEN. Fine for "
                    "a trusted LAN/bench, not for anything reachable.")

    def _shutdown(signum, frame):  # noqa: ARG001
        log.info("shutting down (signal %s)", signum)
        app.stop_background()
        httpd.shutdown()

    signal.signal(signal.SIGTERM, _shutdown)
    signal.signal(signal.SIGINT, _shutdown)

    try:
        httpd.serve_forever()
    finally:
        httpd.server_close()
        app.db.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

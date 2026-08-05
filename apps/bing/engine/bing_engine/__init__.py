"""Bing — a Fing-like network scanner engine.

Pure-Python (standard library only) toolkit for discovering devices on a local
network, scanning ports, resolving DNS, measuring latency and bandwidth, and
more.  It powers both the ``bing`` command-line tool and the web dashboard, and
exposes a small REST API consumed by the Bing mobile app.

Nothing here requires third-party packages, so ``python -m bing_engine`` runs
anywhere Python 3.8+ is installed.  Features that need elevated privileges
(raw-socket ICMP, ARP population) degrade gracefully instead of failing.
"""

__all__ = ["__version__"]

__version__ = "1.0.0"

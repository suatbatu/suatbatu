#!/usr/bin/env python3
"""Convenience launcher so you can run ``./run.py`` or ``python3 run.py`` from
the project root. Equivalent to ``python3 -m app.main``."""
from app.main import main

if __name__ == "__main__":
    raise SystemExit(main())

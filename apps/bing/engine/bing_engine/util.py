"""Small shared helpers: terminal styling, timing, formatting, concurrency."""

from __future__ import annotations

import concurrent.futures
import os
import sys
import time
from typing import Callable, Iterable, List, Optional, Sequence, TypeVar

T = TypeVar("T")
R = TypeVar("R")

# --------------------------------------------------------------------------- #
# Terminal colour                                                             #
# --------------------------------------------------------------------------- #

_NO_COLOR = bool(os.environ.get("NO_COLOR")) or not sys.stdout.isatty()

_CODES = {
    "reset": "\033[0m",
    "bold": "\033[1m",
    "dim": "\033[2m",
    "red": "\033[31m",
    "green": "\033[32m",
    "yellow": "\033[33m",
    "blue": "\033[34m",
    "magenta": "\033[35m",
    "cyan": "\033[36m",
    "white": "\033[37m",
    "grey": "\033[90m",
}


def color(text: str, *styles: str) -> str:
    """Wrap *text* in ANSI styles, unless colour is disabled."""
    if _NO_COLOR or not styles:
        return text
    prefix = "".join(_CODES.get(s, "") for s in styles)
    return f"{prefix}{text}{_CODES['reset']}"


def supports_color() -> bool:
    return not _NO_COLOR


# --------------------------------------------------------------------------- #
# Formatting                                                                  #
# --------------------------------------------------------------------------- #

def human_bytes(n: float) -> str:
    """Render a byte count as a human-readable string."""
    step = 1024.0
    for unit in ("B", "KB", "MB", "GB", "TB", "PB"):
        if abs(n) < step:
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.2f} {unit}"
        n /= step
    return f"{n:.2f} EB"


def human_bits_per_sec(bits: float) -> str:
    step = 1000.0
    for unit in ("bps", "Kbps", "Mbps", "Gbps", "Tbps"):
        if abs(bits) < step:
            return f"{bits:.0f} {unit}" if unit == "bps" else f"{bits:.2f} {unit}"
        bits /= step
    return f"{bits:.2f} Pbps"


def human_duration(seconds: float) -> str:
    if seconds < 1:
        return f"{seconds * 1000:.0f} ms"
    if seconds < 60:
        return f"{seconds:.1f} s"
    m, s = divmod(int(seconds), 60)
    if m < 60:
        return f"{m}m {s}s"
    h, m = divmod(m, 60)
    return f"{h}h {m}m"


def table(rows: Sequence[Sequence[str]], headers: Sequence[str]) -> str:
    """Render a simple, aligned monospace table with box-drawing borders."""
    cols = len(headers)
    widths = [len(str(h)) for h in headers]
    for row in rows:
        for i in range(cols):
            widths[i] = max(widths[i], len(str(row[i])) if i < len(row) else 0)

    def line(left: str, mid: str, right: str) -> str:
        return left + mid.join("─" * (w + 2) for w in widths) + right

    def fmt(cells: Sequence[str], styler: Optional[Callable[[str], str]] = None) -> str:
        parts = []
        for i in range(cols):
            cell = str(cells[i]) if i < len(cells) else ""
            padded = " " + cell.ljust(widths[i]) + " "
            parts.append(styler(padded) if styler else padded)
        return "│" + "│".join(parts) + "│"

    out = [line("┌", "┬", "┐"),
           fmt(headers, lambda c: color(c, "bold", "cyan")),
           line("├", "┼", "┤")]
    out.extend(fmt(r) for r in rows)
    out.append(line("└", "┴", "┘"))
    return "\n".join(out)


# --------------------------------------------------------------------------- #
# Concurrency                                                                 #
# --------------------------------------------------------------------------- #

def parallel_map(
    func: Callable[[T], R],
    items: Iterable[T],
    workers: int = 100,
    on_result: Optional[Callable[[R], None]] = None,
) -> List[R]:
    """Run *func* over *items* on a thread pool, preserving input order.

    ``on_result`` is invoked (in completion order) as each result lands, which
    lets callers stream progress while the ordered list is still assembled.
    """
    items = list(items)
    if not items:
        return []
    workers = max(1, min(workers, len(items)))
    results: List[Optional[R]] = [None] * len(items)
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        future_to_idx = {pool.submit(func, item): i for i, item in enumerate(items)}
        for future in concurrent.futures.as_completed(future_to_idx):
            idx = future_to_idx[future]
            try:
                res = future.result()
            except Exception as exc:  # keep one bad item from killing the sweep
                res = exc  # type: ignore[assignment]
            results[idx] = res
            if on_result is not None and not isinstance(res, Exception):
                on_result(res)
    return [r for r in results if not isinstance(r, Exception)]


class Stopwatch:
    """Context manager / helper for measuring wall-clock elapsed time."""

    def __init__(self) -> None:
        self.start = time.perf_counter()

    def elapsed(self) -> float:
        return time.perf_counter() - self.start

    def __enter__(self) -> "Stopwatch":
        self.start = time.perf_counter()
        return self

    def __exit__(self, *exc) -> None:  # noqa: D401
        self.duration = self.elapsed()


def now_iso() -> str:
    """UTC timestamp in ISO-8601, e.g. ``2026-08-05T12:34:56Z``."""
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())

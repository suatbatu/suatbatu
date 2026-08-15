#!/usr/bin/env bash
# Host-side tests. No PlatformIO, no board, no toolchain download — the point is
# that you can run these while the firmware is still on your desk.
set -euo pipefail
cd "$(dirname "$0")"
g++ -std=c++17 -O2 -Wall -Wextra -o /tmp/shot-timer-test-tdoa test_tdoa.cpp -lm
/tmp/shot-timer-test-tdoa

#!/usr/bin/env bash
# Host-side tests. No PlatformIO, no board, no toolchain download — the point is
# that you can run these while the firmware is still on your desk.
#
# Everything here covers logic that cannot be debugged at a range without
# burning ammunition to discover it was wrong: the direction gate's sign
# conventions and circular indexing, and the rule that decides whether two
# sensors saw two shots or one shot twice.
set -euo pipefail
cd "$(dirname "$0")"

CXXFLAGS="-std=c++17 -O2 -Wall -Wextra"
failed=0

for test in test_tdoa test_shotmerge; do
  echo "=== ${test}"
  g++ $CXXFLAGS -o "/tmp/shot-timer-${test}" "${test}.cpp" -lm
  if ! "/tmp/shot-timer-${test}"; then
    failed=1
  fi
  echo
done

if [ "$failed" -ne 0 ]; then
  echo "SOME TESTS FAILED"
  exit 1
fi
echo "all tests passed"

#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out=$(mktemp)
trap 'rm -f "$out"' EXIT HUP INT TERM
for test in socket_test hardware_test; do
  "${CC:-cc}" -std=gnu11 -DASIX_HOST_TEST -Iasix/tests/stubs -idirafter . -Iasix/port -Iasix/lwip/src/include "asix/tests/$test.c" asix/lwip/src/core/*.c asix/lwip/src/core/ipv4/*.c asix/lwip/src/netif/ethernet.c -o "$out"
  "$out"
  if [ "$test" = socket_test ]; then
    "$out" dhcp
    "$out" conflict
    "$out" timeout
    "$out" fallback
    "$out" fallback-dhcp
    "$out" fallback-usbfail
  fi
done

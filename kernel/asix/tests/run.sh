#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out=$(mktemp)
trap 'rm -f "$out"' EXIT HUP INT TERM
"${CC:-cc}" -std=gnu11 -DASIX_HOST_TEST -Iasix/tests/stubs -idirafter . -Iasix/port -Iasix/lwip/src/include asix/tests/socket_test.c asix/lwip/src/core/*.c asix/lwip/src/core/ipv4/*.c asix/lwip/src/netif/ethernet.c -o "$out"
"$out"

#!/usr/bin/env bash
# Test the password vault (app/src/vault.c) here on OMV: app/test/vault_test.c
# creates, opens, locks and attacks a vault in a temporary directory.
#
# Usage: pi/tools/vault-test.sh        (exit code 0 = all passed)
set -euo pipefail

PI=$(cd "$(dirname "$0")/.." && pwd)
WEST=${ZMK_WEST:-/srv/mergerfs/pool/docker/appdata/zmk-west}
CACHE=${DECK_CACHE:-$WEST/.cache/deck}
"$PI/tools/fetch-deps.sh"
M=$CACHE/deps/monocypher-4.0.2/src
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

gcc -O2 -Wall -Wextra -I"$PI/app/src" -I"$M" "$PI/app/test/vault_test.c" "$PI/app/src/vault.c" "$M/monocypher.c" \
    -o "$T/vault_test"
mkdir "$T/data"
"$T/vault_test" "$T/data"

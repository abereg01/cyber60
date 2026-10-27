#!/usr/bin/env bash
# Draw the keymap: draw/cyber60.svg and draw/cyber60.png, straight from
# config/prospector_dongle.keymap, with keymap-drawer (same tool as the KLOR
# drawing). Run it after any keymap change.
#
# Usage: tools/draw.sh
#
# Runs in a local Docker image (keymap-drawer:local, built on first use). The
# password file never goes into the container: only the placeholders are
# parsed, and the drawing shows just PW1/PW2.
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
IMAGE=keymap-drawer:local

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    # BuildKit's RUN steps have no DNS on this box unless built with the host network.
    export DOCKER_CONFIG=${DOCKER_CONFIG:-/srv/mergerfs/pool/docker/appdata/.docker-config}
    docker build --network=host -t "$IMAGE" - <<'EOF'
FROM python:3.12-slim
RUN apt-get update && apt-get install -y --no-install-recommends librsvg2-bin fonts-dejavu-core && rm -rf /var/lib/apt/lists/*
RUN pip install --no-cache-dir keymap-drawer==0.23.0
EOF
fi

# Copies the keymap without secrets/passwords.dtsi, and draws AS(x) as plain x
# (auto shift is explained in the footer instead of a "Sft+x" on every key).
docker run --rm --user "$(id -u):$(id -g)" \
    --mount type=bind,src="$REPO/config",dst=/config,readonly \
    -v "$REPO/draw":/draw -w /tmp "$IMAGE" sh -euc '
        mkdir cfg && cp /config/*.keymap /config/*.dtsi cfg/
        mkdir cfg/secrets && cp /config/secrets/passwords.example.dtsi cfg/secrets/
        sed -i "s/^#define AS(key).*/#define AS(key) \&kp key/" cfg/behaviors.dtsi
        keymap -c /draw/config.yaml parse -z cfg/prospector_dongle.keymap > /draw/cyber60.yaml
        keymap -c /draw/config.yaml draw /draw/cyber60.yaml -j /draw/layout.json \
            -s BASE FN SYSTEM GAMING -o /draw/cyber60.svg
        rsvg-convert -z 3 -b "#eceff4" -o /draw/cyber60.png /draw/cyber60.svg
    '
echo "draw/cyber60.svg, draw/cyber60.png"

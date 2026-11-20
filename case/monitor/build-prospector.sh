#!/usr/bin/env bash
# Original Prospector case + flat tape surface. Reads original/{body,mount,cap}.stl,
# writes prospector/prospector-{body,mount,cap}-tape.stl. Needs Docker (cadtools:local).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
export DOCKER_CONFIG=${DOCKER_CONFIG:-/srv/mergerfs/pool/docker/appdata/.docker-config}
docker run --rm --user "$(id -u):$(id -g)" -v "$HERE/original":/in:ro -v "$HERE/prospector":/out \
    -v "$HERE/prospector_tape.py":/p.py:ro cadtools:local python3 /p.py /in /out | tee "$HERE/prospector/report.json"

# Runtime for headless Blender (pi/tools/gen-dog.sh mounts Blender itself from
# the cache): the X/GL libraries its binary links against, nothing else.
FROM debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        libx11-6 libxi6 libxxf86vm1 libxfixes3 libxrender1 libxkbcommon0 libsm6 libice6 \
        libgl1 libegl1 libglx0 libgomp1 libwayland-client0 libdecor-0-0 \
    && rm -rf /var/lib/apt/lists/*

# Build and test image for MacPhoenix on another host architecture (arm64,
# riscv64), run through qemu-user on an x86-64 box. tools/xarch/xarch.sh
# builds it per --platform and runs cmake/ctest in it; see docs/cpu/PLAN.md
# (C2d, S2).
FROM debian:trixie

ARG DEBIAN_FRONTEND=noninteractive

# mac-phoenix's build deps (debian/control) plus the Executor's (boost, ruby,
# bison, perl) and what the test scripts call (curl, python3). Qt WebEngine
# is not built for every architecture (riscv64 has none in trixie); without
# it the build needs -DBUILD_BROWSER=OFF, which xarch.sh passes.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build pkg-config git ca-certificates \
      python3 curl file gdb \
      libssl-dev libopenh264-dev libvpx-dev libwebp-dev libopus-dev libyuv-dev \
      qt6-base-dev cargo rustc \
      ruby bison perl libboost-filesystem-dev libboost-program-options-dev \
 && (apt-get install -y --no-install-recommends qt6-webengine-dev \
      || echo "qt6-webengine-dev not available on $(dpkg --print-architecture)") \
 && rm -rf /var/lib/apt/lists/*

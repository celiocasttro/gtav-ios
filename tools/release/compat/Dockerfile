# Build container for the portable Linux release: Ubuntu 22.04 (glibc 2.35) with clang 20 and
# GCC 13's libstdc++, the same toolchain the ReXGlue SDK's Linux CI uses
# (rexglue-sdk/.github/workflows/_build-platform.yaml). Binaries built here need glibc 2.35 and
# libstdc++ GLIBCXX_3.4.32 (Ubuntu 24.04, Debian 13, Fedora 39, Arch or newer).
# Used by tools/release/compat/build.sh.
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates wget gnupg lsb-release software-properties-common git python3 xz-utils file \
 && wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh && bash /tmp/llvm.sh 20 \
 && apt-get install -y clang-20 lld-20 llvm-20 \
 && add-apt-repository -y ppa:ubuntu-toolchain-r/test \
 && wget -qO- https://apt.kitware.com/keys/kitware-archive-latest.asc | gpg --dearmor -o /usr/share/keyrings/kitware.gpg \
 && echo "deb [signed-by=/usr/share/keyrings/kitware.gpg] https://apt.kitware.com/ubuntu/ jammy main" > /etc/apt/sources.list.d/kitware.list \
 && apt-get update && apt-get install -y --no-install-recommends \
      cmake ninja-build build-essential g++-13 curl unzip zip autoconf pkg-config \
      libgtk-3-dev libx11-xcb-dev libxss-dev libvulkan-dev \
      libwayland-dev libwayland-bin wayland-protocols libxkbcommon-dev libdecor-0-dev \
      libasound2-dev libpulse-dev libpipewire-0.3-dev \
 && rm -rf /var/lib/apt/lists/* /tmp/llvm.sh

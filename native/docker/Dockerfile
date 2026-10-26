# The build image of the native program's packages (doc/linux_native/, L3a).
#
# Debian trixie, the distribution the Cardputer Zero's OS (Raspberry Pi OS
# trixie) is built from, so the packages link against the glibc and the
# library versions they will meet there. One image builds both:
#
#   arm64  cross-compiled with crossbuild-essential-arm64 against the arm64
#          development packages installed beside the amd64 ones (multiarch)
#   amd64  built natively, for Linux PCs running trixie or newer
#
# The base is pinned to a dated tag, never a moving one (the same rule as
# the top of docker-compose.yml): two machines on the same commit must get
# the same toolchain. Moving it is a deliberate change of this file.
#
# Built and used by rake native:image / native:cross / native:deb
# (rakelib/native.rake), never pushed anywhere. The repository is mounted at
# /src and everything runs as the calling user, so nothing in the checkout
# ends up owned by root.
FROM debian:trixie-20261020

ARG DEBIAN_FRONTEND=noninteractive

RUN dpkg --add-architecture arm64 \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config git ca-certificates file \
      ruby rake bison \
      dpkg-dev fakeroot \
      crossbuild-essential-arm64 \
      libsdl2-dev libdrm-dev libasound2-dev libssl-dev \
      libsdl2-dev:arm64 libdrm-dev:arm64 libasound2-dev:arm64 libssl-dev:arm64 \
 && rm -rf /var/lib/apt/lists/*

# rake and the picoruby build write their caches under $HOME; the calling
# user has no home inside the image.
ENV HOME=/tmp
WORKDIR /src

# Build and test image for node-printing.
#
# Provides everything needed to compile the native addon on Linux (g++ + CUPS
# headers), to syntax-check the Windows implementation against the real Win32
# headers (mingw-w64), and to run a real end-to-end print job against a CUPS
# daemon with virtual queues backed by the file: backend.
#
#   docker build -t node-printing-build test/docker
#   docker run --rm -v "$PWD":/work node-printing-build bash test/docker/e2e-cups.sh
#
# Debian trixie is used on purpose: the CUPS 2.4.2 of bookworm crashes inside a
# container ("cupsdDoSelect() failed - Bad address!"), 2.4.10 does not.
FROM debian:trixie-slim

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates \
      curl \
      build-essential \
      python3 \
      nodejs \
      npm \
      libnode-dev \
      libcups2-dev \
      cups \
      cups-daemon \
      cups-client \
      g++-mingw-w64-x86-64 \
 && rm -rf /var/lib/apt/lists/*

# Use the Debian Node headers instead of downloading them.
ENV npm_config_nodedir=/usr

WORKDIR /work

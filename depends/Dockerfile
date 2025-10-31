FROM ubuntu:24.04
ARG TARGETARCH

ENV LC_ALL=C.UTF-8

RUN apt-get update && \
    apt-get install --no-install-recommends --no-upgrade -qq \
        build-essential libtool cmake pkgconf curl git \
        ca-certificates ccache bsdmainutils python3 python3-venv bison && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /tapyrus-core

COPY depends .
RUN  if [ "$TARGETARCH" = "arm64" ]; then BUILD_HOST="aarch64-unknown-linux-gnu"; else BUILD_HOST="x86_64-pc-linux-gnu"; fi && \
     make HOST=$BUILD_HOST NO_QT=1 -j"$(($(nproc)+1))" install_cmake

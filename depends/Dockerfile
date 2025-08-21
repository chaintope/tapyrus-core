FROM ubuntu:24.04
ARG TARGETARCH

ENV LC_ALL=C.UTF-8

RUN apt-get update && \
    apt-get install --no-install-recommends --no-upgrade -qq \
        build-essential libtool autotools-dev automake cmake pkgconf curl git \
        ca-certificates ccache bsdmainutils python3 python3-venv bison && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /tapyrus-core

COPY depends .
RUN make NO_USDT=1 NO_QT=1 -j"$(($(nproc)+1))" install_cmake

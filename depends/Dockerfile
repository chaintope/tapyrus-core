FROM ubuntu:18.04
ARG TARGETARCH

ENV BUILD_HOST="x86_64-pc-linux-gnu"
ENV LC_ALL C.UTF-8
ENV BUILD_PACKAGES "build-essential libtool autotools-dev automake pkg-config bsdmainutils curl git ca-certificates ccache"
ENV PACKAGES "python3-zmq libevent-dev libboost-system-dev libboost-filesystem-dev libboost-chrono-dev libboost-test-dev libboost-thread-dev libdb5.3++-dev libminiupnpc-dev libzmq3-dev libqrencode-dev"


RUN echo BUILD_HOST=$BUILD_HOST
RUN apt-get update && \
    apt-get install --no-install-recommends --no-upgrade -qq $PACKAGES $BUILD_PACKAGES

WORKDIR /tapyrus-core

COPY . ./depends
RUN  if [ "$TARGETARCH" = "arm64" ]; then BUILD_HOST="aarch64-linux-gnu"; fi && \
     make HOST=$BUILD_HOST -j"$(($(nproc)+1))" -C depends

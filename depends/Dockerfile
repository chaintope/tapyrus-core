FROM ubuntu:18.04

ENV LC_ALL C.UTF-8
ENV BUILD_PACKAGES "build-essential libtool autotools-dev automake pkg-config bsdmainutils curl git ca-certificates ccache"
ENV PACKAGES "python3-zmq libevent-dev bsdmainutils libboost-system-dev libboost-filesystem-dev libboost-chrono-dev libboost-test-dev libboost-thread-dev libdb5.3++-dev libminiupnpc-dev libzmq3-dev libqrencode-dev"

RUN apt-get update && \
    apt-get install --no-install-recommends --no-upgrade -qq $PACKAGES $BUILD_PACKAGES

WORKDIR /tapyrus-core

COPY . ./depends
RUN make -j"$(($(nproc)+1))" -C depends

FROM ubuntu:18.04

ENV LC_ALL C.UTF-8
ENV BUILD_PACKAGES "build-essential libtool autotools-dev automake pkg-config bsdmainutils curl git ca-certificates ccache"
ENV PACKAGES "python3-zmq libevent-dev bsdmainutils libboost-system-dev libboost-filesystem-dev libboost-chrono-dev libboost-test-dev libboost-thread-dev libdb5.3++-dev libminiupnpc-dev libzmq3-dev libqrencode-dev"
ENV TAPYRUS_CONFIG "--disable-tests --disable-bench --disable-dependency-tracking --prefix=/tapyrus-core/depends/x86_64-pc-linux-gnu --bindir=/tapyrus-core/dist/bin  --libdir=/tapyrus-core/dist/lib --enable-zmq --enable-reduce-exports --with-incompatible-bdb --with-gui=no CPPFLAGS=-DDEBUG_LOCKORDER"

RUN apt-get update && \
    apt-get install --no-install-recommends --no-upgrade -qq $PACKAGES $BUILD_PACKAGES

WORKDIR /tapyrus-core

COPY . ./depends
RUN make -j"$(($(nproc)+1))" -C depends

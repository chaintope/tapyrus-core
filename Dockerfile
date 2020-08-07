FROM ubuntu:18.04 as builder

ARG MAKEJOBS=3
ENV LC_ALL C.UTF-8
ENV BUILD_PACKAGES "build-essential libtool autotools-dev automake pkg-config bsdmainutils curl git ca-certificates ccache"
ENV PACKAGES "python3-zmq libssl1.0-dev libevent-dev bsdmainutils libboost-system-dev libboost-filesystem-dev libboost-chrono-dev libboost-test-dev libboost-thread-dev libdb5.3++-dev libminiupnpc-dev libzmq3-dev libqrencode-dev"
ENV TAPYRUS_CONFIG "--disable-dependency-tracking --prefix=/tapyrus-core/depends/x86_64-pc-linux-gnu --bindir=/tapyrus-core/dist/bin  --libdir=/tapyrus-core/dist/lib --enable-zmq --enable-reduce-exports --with-incompatible-bdb --with-gui=no CPPFLAGS=-DDEBUG_LOCKORDER"
WORKDIR /tapyrus-core
COPY . .

RUN apt-get update && \
    apt-get install --no-install-recommends --no-upgrade -qq $PACKAGES $BUILD_PACKAGES && \
    ./autogen.sh && \
    ./configure --enable-cxx --disable-shared --disable-replication --with-pic --with-incompatible-bdb && \
    make -j $MAKEJOBS -C depends && \
    ./configure $TAPYRUS_CONFIG && \
    make -j $MAKEJOBS && \
    make install

FROM ubuntu:18.04

RUN apt-get update

COPY --from=builder /tapyrus-core/dist/bin/* /usr/local/bin/

ENV DATA_DIR='/var/lib/tapyrus' \
    CONF_DIR='/etc/tapyrus'
RUN mkdir ${DATA_DIR} && mkdir ${CONF_DIR}

# p2p port (Production/Development) rpc port (Production/Development)
EXPOSE 2357 12383 2377 12381

COPY entrypoint.sh /usr/local/bin/

VOLUME ["$DATA_DIR"]

ENTRYPOINT ["entrypoint.sh"]
CMD ["tapyrusd -datadir=${DATA_DIR} -conf=${CONF_DIR}/tapyrus.conf"]
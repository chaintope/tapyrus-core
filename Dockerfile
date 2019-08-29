# Create Docker image for Tapyrus Core.
#
# In default, this image run as testnet full node.
#
# ## Run other network
# If you want to run in other network, you can mount your directory which has conf and genesis.dat
# files on node's datadir
#
# Like this
#      $ docker run -d -P --name tapyrus-regtest -v /my/regtest/datadir:/var/lib/tapyrus [Image]
#
# ## Run utility commands
# You can run utility commands (like tapyrus-cli) using docker exec.
#
# Example
#      $ docker exec [Container ID] tapyrus-cli -conf=/var/lib/tapyrus/tapyrus.conf getblockchaininfo

FROM ubuntu:18.04

ENV TAPYRUS_ROOT /tapyrus-core
ENV MAKEJOBS -j3
ENV LC_ALL C.UTF-8
ENV BUILD_PACKAGES "build-essential libtool autotools-dev automake pkg-config bsdmainutils curl git ca-certificates ccache"
ENV PACKAGES "python3-zmq libssl1.0-dev libevent-dev bsdmainutils libboost-system-dev libboost-filesystem-dev libboost-chrono-dev libboost-test-dev libboost-thread-dev libdb5.3++-dev libminiupnpc-dev libzmq3-dev libprotobuf-dev protobuf-compiler libqrencode-dev"
ENV BITCOIN_CONFIG "--enable-zmq --with-incompatible-bdb --enable-glibc-back-compat --enable-reduce-exports --with-gui=no CPPFLAGS=-DDEBUG_LOCKORDER"

WORKDIR $TAPYRUS_ROOT
COPY . .

RUN apt-get update && \
    apt-get install --no-install-recommends --no-upgrade -qq $PACKAGES $BUILD_PACKAGES && \
    ./autogen.sh && \
    ./configure --disable-dependency-tracking $BITCOIN_CONFIG && \
    make $MAKEJOBS install && \
    make install

ENV datadir /var/lib/tapyrus
ENV conf $datadir/tapyrus.conf

RUN mkdir $datadir  && \
    echo "testnet=3\ntxindex=1\nserver=1\nrest=1\nrpcuser=user\nrpcpassword=pass\nrpcbind=0.0.0.0\nrpcallowip=0.0.0.0/0\nsignblockpubkeys=033cfe7fa1be58191b9108883543e921d31dc7726e051ee773e0ea54786ce438f8020464074b94702e9b07803d247021943bdcc1f8700b92b66defb7fadd76e80acf02cbe0ad70ffe110d097db648fda20bef14dc72b5c9979c137c451820c176ac23f\nsignblockthreshold=2" > $conf && \
    echo "0100000000000000000000000000000000000000000000000000000000000000000000002e4533a0ccb1c1b83a641ce00656d3d234e362ab2ce0d6d69eaaba1edceb9e962f69b65cb127339d5d8da08d50f6d4deb37b46157123dbe5fb0a30a641af68e70c3d285d0346304402201e9de308c52cfc760c9eb5a1638857563c39791a77bd3b4c53dcd26b4d02e8480220542963fe20dbedd31c609f8393cb68e4b80c4e849fcddf7a0a2dddaac4ce2e134630440220295288205c39d933dd7c236011857cf3b0147e226154afc5c44421a38cb0a77102202c580fdf814904ed3f17fce8f545ba66aa3ebd9a00eb7205371ee3de895e2a0346304402200f9301b705c64a5fb5cb9ecb8cf4498291142b93491cb560e36db6145f60d6e002200572b51df4e1397340a1487f9f8b35e07730055a2d4d9a83e0954ff22a8bc599010100000001000000000000000000000000000000000000000000000000000000000000000000000000240102210276d7a1b83fd97b8751d2b82e1f7a89dcd23a5b3f5cc2798083ad6f775018102effffffff0100f2052a010000001976a914a941c2ccebe78a5a1348dddd42362b588cb563b588ac00000000" > $datadir/genesis.dat

# mainet p2p port
EXPOSE 2357
# testnet p2p port
EXPOSE 12357
# regtest p2p port
EXPOSE 12383
# mainnet rpc port
EXPOSE 2377
# testnet rpc port
EXPOSE 12377
# regtest rpc port
EXPOSE 12381

VOLUME ["$datadir"]

ENTRYPOINT tapyrusd -datadir=$datadir -conf=$conf
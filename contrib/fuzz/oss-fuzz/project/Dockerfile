# Draft OSS-Fuzz project Dockerfile for tapyrus-core -- see README.md in this
# directory for what this is (and isn't) used for.

FROM gcr.io/oss-fuzz-base/base-builder

RUN apt-get update && apt-get install -y \
  build-essential cmake curl g++-multilib make \
  patch python3 libboost-dev libevent-dev libdb++-dev zip

RUN git clone --depth=1 https://github.com/chaintope/tapyrus-core.git tapyrus-core
WORKDIR tapyrus-core
RUN git submodule update --init --depth=1 src/secp256k1
COPY build.sh $SRC/

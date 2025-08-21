FROM tapyrus/builder:cmake as builder
ARG TARGETARCH

ENV LC_ALL=C.UTF-8

RUN apt-get update && \
    apt-get install --no-install-recommends --no-upgrade -qq \
        build-essential cmake pkgconf python3-venv ccache && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /tapyrus-core
COPY . .
COPY --from=tapyrus/builder:cmake /tapyrus-core/aarch64-unknown-linux-gnu /tapyrus-core/depends/aarch64-unknown-linux-gnu

RUN if [ "$TARGETARCH" = "arm64" ]; then BUILD_HOST="aarch64-unknown-linux-gnu"; else BUILD_HOST="x86_64-unknown-linux-gnu"; fi && \
    DEPENDS_PREFIX="/tapyrus-core/depends/$BUILD_HOST" && \
    TOOLCHAIN_FILE="$DEPENDS_PREFIX/toolchain.cmake" && \
    cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DCMAKE_MODULE_PATH="/tapyrus-core/cmake" \
        -DENABLE_ZMQ=ON \
        -DENABLE_GUI=OFF \
        -DENABLE_TRACING=OFF \
        -DENABLE_WALLET=ON \
        -DWITH_BDB=ON \
        -DENABLE_BENCH=OFF \
        -DCMAKE_INSTALL_PREFIX=/tapyrus-core/dist && \
    cmake --build build --parallel -j"$(($(nproc)+1))" --target all && \
    cmake --install build

FROM ubuntu:24.04

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

#run this container with --privileged option to use bpf trace scripts
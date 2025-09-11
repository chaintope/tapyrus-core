FROM tapyrus/builder:v0.7.0 AS builder
ARG TARGETARCH

ENV LC_ALL=C.UTF-8

WORKDIR /tapyrus-core

# Set BUILD_HOST based on target architecture and export for subsequent commands
RUN if [ "$TARGETARCH" = "arm64" ]; then BUILD_HOST="aarch64-unknown-linux-gnu"; else BUILD_HOST="x86_64-pc-linux-gnu"; fi && \
    echo "export BUILD_HOST=$BUILD_HOST" >> /etc/environment && \
    echo "export BUILD_HOST=$BUILD_HOST" >> /root/.bashrc && \
    mkdir -p "/tmp/depends-built" && \
    cp -r "/tapyrus-core/$BUILD_HOST" "/tmp/depends-built/$BUILD_HOST"

COPY . .

# Restore the built dependencies after copying source and build
RUN . /etc/environment && \
    mkdir -p "/tapyrus-core/depends" && \
    cp -r "/tmp/depends-built/$BUILD_HOST" "/tapyrus-core/depends/$BUILD_HOST" && \
    rm -rf /tmp/depends-built && \
    DEPENDS_PREFIX="/tapyrus-core/depends/$BUILD_HOST" && \
    TOOLCHAIN_FILE="$DEPENDS_PREFIX/toolchain.cmake" && \
    export PKG_CONFIG_PATH="$DEPENDS_PREFIX/lib/pkgconfig:$DEPENDS_PREFIX/share/pkgconfig" && \
    cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DCMAKE_MODULE_PATH="/tapyrus-core/cmake/module" \
        -DENABLE_ZMQ=ON \
        -DBUILD_GUI=OFF \
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

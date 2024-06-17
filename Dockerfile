FROM alpine:3.20 as base
WORKDIR /app

RUN <<EOF
    set -ex
    apk update
    apk add --no-cache \
        asio-dev \
        autoconf \
        build-base \
        binutils \
        cmake \
        curl \
        file \
        gcc \
        g++ \
        git \
        libgcc \
        libtool \
        linux-headers \
        make \
        musl-dev \
        ninja \
        tar \
        unzip \
        wget
EOF

COPY CMakeLists.txt /app/
COPY src/* /app/src/

RUN <<EOF
    set -ex
    mkdir build/
    cd build
    cmake -G Ninja -S ..
    ninja
EOF

FROM alpine:3.20
EXPOSE 5000

COPY --from=base /app/build/usb_proxy /app/usb_proxy

RUN <<EOF
    set -ex
    apk update
    apk add --no-cache \
        socat \
        libstdc++

    rm -rf /var/cache/apk/*

    chmod +x /app/usb_proxy
EOF

ENTRYPOINT ["/app/usb_proxy"]

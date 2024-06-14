FROM mcr.microsoft.com/devcontainers/base:alpine-3.20

RUN <<EOF
    set -ex
    apk update
    apk add --no-cache \
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

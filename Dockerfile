FROM alpine:3.20 as base
WORKDIR /app

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

COPY --from=base /app/build/connect /app/build/discover /var/www/

RUN <<EOF
    set -ex
    apk update
    apk add --no-cache \
        socat \
        lighttpd \
        libstdc++

    rm -rf /var/cache/apk/*
EOF

COPY <<EOF /app/lighttpd.conf

server.modules = (
    "mod_access",
    "mod_accesslog",
    "mod_alias",
    "mod_cgi"
)
server.document-root = "/var/www"
server.pid-file      = "/run/lighttpd.pid"
server.errorlog      = "/dev/pts/0"
server.port          = 5000
accesslog.filename   = "/dev/pts/0"
url.access-deny = ("~")
cgi.assign = ("" => "")
alias.url = ( "/usb-proxy/" => "/var/www/" )

EOF

ENTRYPOINT ["/usr/sbin/lighttpd"]
CMD ["-D", "-f", "/app/lighttpd.conf"]

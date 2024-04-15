# syntax=docker/dockerfile:1

ARG DOCKER_IMAGE_VERSION=26.0.0
ARG PROCPS_VERSION=v3.3.17

ARG REPO=axisecp
ARG ARCH=armv7hf

ARG VERSION=1.14
ARG UBUNTU_VERSION=22.04
ARG NATIVE_SDK=acap-native-sdk

FROM ${REPO}/${NATIVE_SDK}:${VERSION}-${ARCH}-ubuntu${UBUNTU_VERSION} AS sdk_image

FROM sdk_image AS build_image

# hadolint ignore=DL3009
RUN <<EOF
    apt-get update
    apt-get -q install -y -f --no-install-recommends \
        automake \
        autopoint \
        gettext \
        git \
        libtool
    ln -s /usr/bin/libtoolize /usr/bin/libtool
EOF

FROM build_image AS ps

ARG PROCPS_VERSION
ARG BUILD_DIR=/build
ARG EXPORT_DIR=/export

WORKDIR $BUILD_DIR
RUN git clone --depth 1 -b $PROCPS_VERSION 'https://gitlab.com/procps-ng/procps' .

ARG BUILD_CACHE=build.cache
RUN <<EOF
    echo ac_cv_func_realloc_0_nonnull=yes >$BUILD_CACHE
    echo ac_cv_func_malloc_0_nonnull=yes >>$BUILD_CACHE
EOF

RUN <<EOF
    . /opt/axis/acapsdk/environment-setup*
    ./autogen.sh
    ./configure --host="${TARGET_PREFIX%*-}" \
                --disable-shared \
                --without-ncurses  \
                --cache-file="$BUILD_CACHE"
    make ps/pscommand
    $STRIP ps/pscommand
EOF

WORKDIR $EXPORT_DIR
RUN cp $BUILD_DIR/ps/pscommand ps

FROM sdk_image AS docker-binaries

WORKDIR /download

ARG ARCH
ARG DOCKER_IMAGE_VERSION

# Download and extract dockerd and its dependencies
RUN <<EOF
    if [ "$ARCH" = "armv7hf" ]; then
        export DOCKER_ARCH="armhf";
    elif [ "$ARCH" = "aarch64" ]; then
        export DOCKER_ARCH="aarch64";
    fi;
    curl -Lo docker_binaries.tgz "https://download.docker.com/linux/static/stable/${DOCKER_ARCH}/docker-${DOCKER_IMAGE_VERSION}.tgz" ;
    tar -xz -f docker_binaries.tgz --strip-components=1 docker/dockerd ;
    tar -xz -f docker_binaries.tgz --strip-components=1 docker/docker-init ;
    tar -xz -f docker_binaries.tgz --strip-components=1 docker/docker-proxy ;
EOF

FROM sdk_image AS build

WORKDIR /opt/app

COPY app .
COPY --from=ps /export/ps .
COPY --from=docker-binaries \
    /download/dockerd \
    /download/docker-init \
    /download/docker-proxy ./

RUN <<EOF
    . /opt/axis/acapsdk/environment-setup*
    acap-build . \
        -a dockerd \
        -a docker-init \
        -a docker-proxy \
        -a empty_daemon.json \
        -a ps
EOF

ENTRYPOINT [ "/opt/axis/acapsdk/sysroots/x86_64-pokysdk-linux/usr/bin/eap-install.sh" ]

FROM scratch AS binaries

COPY --from=build /opt/app/*.eap /

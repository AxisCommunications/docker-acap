# syntax=docker/dockerfile:1

ARG DOCKER_IMAGE_VERSION=26.0.0
ARG PROCPS_VERSION=v3.3.17
ARG NSENTER_VERSION=v2.40
ARG SLIRP4NETNS_VERSION=1.2.3

ARG REPO=axisecp
ARG ARCH=armv7hf

ARG VERSION=1.14_rc3
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
        libtool \
        bison \
        flex
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

FROM build_image AS nsenter

ARG NSENTER_VERSION
ARG BUILD_DIR=/build
ARG EXPORT_DIR=/export

WORKDIR $BUILD_DIR
RUN git clone -b $NSENTER_VERSION 'https://github.com/util-linux/util-linux.git'

ARG BUILD_CACHE=build.cache
RUN <<EOF
    echo ac_cv_func_realloc_0_nonnull=yes >$BUILD_CACHE
    echo ac_cv_func_malloc_0_nonnull=yes >>$BUILD_CACHE
EOF

RUN <<EOF
    cd util-linux
    . /opt/axis/acapsdk/environment-setup*
    ./autogen.sh
    ./configure --host="${TARGET_PREFIX%*-}" \
                --disable-shared \
                --without-ncurses  \
                --cache-file="$BUILD_CACHE"
    make nsenter
    $STRIP nsenter
EOF

WORKDIR $EXPORT_DIR
RUN cp $BUILD_DIR/util-linux/nsenter nsenter

FROM sdk_image AS docker_binaries

WORKDIR /download

ARG ARCH
ARG DOCKER_IMAGE_VERSION
ARG SLIRP4NETNS_VERSION
ARG ROOTLESS_EXTRAS_VERSION=${DOCKER_IMAGE_VERSION}

# Download and extract slirp4netns
RUN <<EOF
    if [ "$ARCH" = "armv7hf" ]; then
        export SLIRP4NETNS_ARCH="armv7l";
    elif [ "$ARCH" = "aarch64" ]; then
        export SLIRP4NETNS_ARCH="aarch64";
    fi;
    curl -Lo slirp4netns \
    "https://github.com/rootless-containers/slirp4netns/releases/download/v${SLIRP4NETNS_VERSION}/slirp4netns-${SLIRP4NETNS_ARCH}";
    chmod +x slirp4netns
EOF

# Download and extract docker scripts and docker-rootless-extras scripts
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
    curl -Lo docker-rootless-extras.tgz "https://download.docker.com/linux/static/stable/${DOCKER_ARCH}/docker-rootless-extras-${ROOTLESS_EXTRAS_VERSION}.tgz" ;
    tar -xz -f docker-rootless-extras.tgz --strip-components=1 ;
EOF

FROM sdk_image AS build

WORKDIR /opt/app

COPY app .
COPY --from=ps /export/ps .
COPY --from=nsenter /export/nsenter .
COPY --from=docker_binaries \
    /download/dockerd \
    /download/docker-init \
    /download/docker-proxy \
    /download/rootlesskit \
    /download/rootlesskit-docker-proxy \
    /download/slirp4netns ./

RUN <<EOF
    . /opt/axis/acapsdk/environment-setup*
    acap-build . \
        -a dockerd \
        -a docker-init \
        -a docker-proxy \
        -a ps \
        -a slirp4netns \
        -a rootlesskit \
        -a rootlesskit-docker-proxy \
        -a nsenter
EOF

ENTRYPOINT [ "/opt/axis/acapsdk/sysroots/x86_64-pokysdk-linux/usr/bin/eap-install.sh" ]

FROM scratch AS binaries

COPY --from=build /opt/app/*.eap /

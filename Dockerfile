# syntax=docker/dockerfile:1

ARG DOCKER_IMAGE_VERSION=24.0.2

ARG REPO=axisecp
ARG ACAPARCH=armv7hf

ARG VERSION=1.8
ARG UBUNTU_VERSION=22.04
ARG NATIVE_SDK=acap-native-sdk

ARG ACAP3_SDK_VERSION=3.5
ARG ACAP3_UBUNTU_VERSION=20.04
ARG ACAP3_SDK=acap-sdk

FROM ${REPO}/${ACAP3_SDK}:${ACAP3_SDK_VERSION}-${ACAPARCH}-ubuntu${ACAP3_UBUNTU_VERSION} as acap-sdk

FROM ${REPO}/${NATIVE_SDK}:${VERSION}-${ACAPARCH}-ubuntu${UBUNTU_VERSION} as build_image

RUN <<EOF
    apt-get update
    apt-get -q install -y -f --no-install-recommends \
        automake \
        autopoint \
        gettext \
        git \
        libtool \
        bison
    ln -s /usr/bin/libtoolize /usr/bin/libtool
EOF

FROM build_image AS nsenter

ARG NSENTER_VERSION=v2.39.1
ARG BUILD_DIR=/build
ARG EXPORT_DIR=/export

WORKDIR $BUILD_DIR
RUN git clone -b $NSENTER_VERSION 'https://github.com/util-linux/util-linux.git'

ARG BUILD_CACHE=build.cache
RUN echo ac_cv_func_realloc_0_nonnull=yes >$BUILD_CACHE \
    && echo ac_cv_func_malloc_0_nonnull=yes >>$BUILD_CACHE
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

FROM build_image AS ps

ARG PROCPS_VERSION=v3.3.17
ARG BUILD_DIR=/build
ARG EXPORT_DIR=/export

WORKDIR $BUILD_DIR
RUN git clone --depth 1 -b $PROCPS_VERSION 'https://gitlab.com/procps-ng/procps' .

ARG BUILD_CACHE=build.cache
RUN echo ac_cv_func_realloc_0_nonnull=yes >$BUILD_CACHE \
 && echo ac_cv_func_malloc_0_nonnull=yes >>$BUILD_CACHE
RUN <<EOF
    . /opt/axis/acapsdk/environment-setup*
    ./autogen.sh
    ./configure --host=${TARGET_PREFIX%*-} \
                --disable-shared \
                --without-ncurses  \
                --cache-file="$BUILD_CACHE"
    make ps/pscommand
    $STRIP ps/pscommand
EOF

WORKDIR $EXPORT_DIR
RUN cp $BUILD_DIR/ps/pscommand ps

FROM build_image as build

ARG DOCKER_IMAGE_VERSION
ARG ACAPARCH
ARG SLIRP4NETNS_VERSION=1.2.0
ARG ROOTLESS_EXTRAS_VERSION=${DOCKER_IMAGE_VERSION}

# Copy over axparameter from the acap-sdk
COPY --from=acap-sdk /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/include/axsdk/ax_parameter /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/include/axsdk
COPY --from=acap-sdk /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/lib/libaxparameter.so /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/lib/libaxparameter.so
COPY --from=acap-sdk /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/lib/libaxparameter.so.1 /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/lib/libaxparameter.so.1
COPY --from=acap-sdk /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/lib/libaxparameter.so.1.0 /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/lib/libaxparameter.so.1.0
COPY --from=acap-sdk /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/lib/pkgconfig/axparameter.pc /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/lib/pkgconfig/axparameter.pc

COPY app /opt/app
COPY --from=ps /export/ps /opt/app
COPY --from=nsenter /export/nsenter /opt/app

COPY ./binaries/${ACAPARCH}/* /opt/app

# Temp fix to get binary onto aarch64 master fw
COPY ./binaries/systemd-user-runtime-dir /opt/app
COPY ./binaries/*.service /opt/app

WORKDIR /opt/app

# Download and extract slirp4netns
RUN <<EOF
    if [ "$ACAPARCH" = "armv7hf" ]; then
        export SLIRP4NETNS_ARCH="armv7l";
    elif [ "$ACAPARCH" = "aarch64" ]; then
        export SLIRP4NETNS_ARCH="aarch64";
    fi;
    curl -Lo slirp4netns "https://github.com/rootless-containers/slirp4netns/releases/download/v${SLIRP4NETNS_VERSION}/slirp4netns-${SLIRP4NETNS_ARCH}";
    chmod +x slirp4netns
EOF


# Download and extract docker scripts and docker-rootless-extras scripts
RUN <<EOF
    if [ "$ACAPARCH" = "armv7hf" ]; then
        export DOCKER_ARCH="armhf";
    elif [ "$ACAPARCH" = "aarch64" ]; then
        export DOCKER_ARCH="aarch64";
    fi;
    curl -Lo docker_binaries.tgz "https://download.docker.com/linux/static/stable/${DOCKER_ARCH}/docker-${DOCKER_IMAGE_VERSION}.tgz" ;
    tar -xz -f docker_binaries.tgz --strip-components=1 docker/dockerd ;
    tar -xz -f docker_binaries.tgz --strip-components=1 docker/docker-init ;
    tar -xz -f docker_binaries.tgz --strip-components=1 docker/docker-proxy ;
    curl -Lo docker-rootless-extras.tgz "https://download.docker.com/linux/static/stable/${DOCKER_ARCH}/docker-rootless-extras-${ROOTLESS_EXTRAS_VERSION}.tgz" ;
    tar -xz -f docker-rootless-extras.tgz --strip-components=1 ;
EOF

# Build eap
RUN <<EOF
    . /opt/axis/acapsdk/environment-setup*
    acap-build . \
        -a dockerd \
        -a docker-init \
        -a docker-proxy \
        -a empty_daemon.json \
        -a ps \
        -a slirp4netns \
        -a rootlesskit \
        -a rootlesskit-docker-proxy \
        -a nsenter \
        -a newgidmap \
        -a newuidmap \
        -a systemd-user-runtime-dir \
        -a acap-user-runtime-dir@.service \
        -a acap-user@.service
EOF

ENTRYPOINT [ "/opt/axis/acapsdk/sysroots/x86_64-pokysdk-linux/usr/bin/eap-install.sh" ]

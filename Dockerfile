# syntax=docker/dockerfile:1

ARG DOCKER_VERSION=24.0.2
ARG DOCKER_COMPOSE_VERSION=v2.18.1

ARG REPO=axisecp
ARG ACAPARCH=armv7hf

ARG VERSION=1.10
ARG UBUNTU_VERSION=22.04
ARG NATIVE_SDK=acap-native-sdk

ARG ACAP3_SDK_VERSION=3.5
ARG ACAP3_UBUNTU_VERSION=20.04
ARG ACAP3_SDK=acap-sdk

FROM ${REPO}/${NATIVE_SDK}:${VERSION}-${ACAPARCH}-ubuntu${UBUNTU_VERSION} as build_image

FROM ${REPO}/${ACAP3_SDK}:${ACAP3_SDK_VERSION}-${ACAPARCH}-ubuntu${ACAP3_UBUNTU_VERSION} as acap-sdk

FROM build_image AS ps
ARG PROCPS_VERSION=v3.3.17
ARG BUILD_DIR=/build
ARG EXPORT_DIR=/export

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

WORKDIR $BUILD_DIR
RUN git clone --depth 1 -b $PROCPS_VERSION 'https://gitlab.com/procps-ng/procps' .

ARG BUILD_CACHE=build.cache
RUN echo ac_cv_func_realloc_0_nonnull=yes >$BUILD_CACHE \
 && echo ac_cv_func_malloc_0_nonnull=yes >>$BUILD_CACHE
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

FROM build_image as build

ARG DOCKER_VERSION
ARG DOCKER_COMPOSE_VERSION
ARG ACAPARCH

# Copy over axparameter from the acap-sdk
COPY --from=acap-sdk /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/include/axsdk/ax_parameter /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/include/axsdk
COPY --from=acap-sdk /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/lib/libaxparameter.so /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/lib/libaxparameter.so
COPY --from=acap-sdk /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/lib/libaxparameter.so.1 /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/lib/libaxparameter.so.1
COPY --from=acap-sdk /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/lib/libaxparameter.so.1.0 /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/lib/libaxparameter.so.1.0
COPY --from=acap-sdk /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/lib/pkgconfig/axparameter.pc /opt/axis/acapsdk/sysroots/${ACAPARCH}/usr/lib/pkgconfig/axparameter.pc

COPY app /opt/app
COPY --from=ps /export/ps /opt/app

# Get docker* binaries and scripts
RUN <<EOF
    if [ "$ACAPARCH" = "armv7hf" ]; then
        export DOCKER_ARCH="armhf";
        export DOCKER_COMPOSE_ARCH="armv7";
    elif [ "$ACAPARCH" = "aarch64" ]; then
        export DOCKER_ARCH="aarch64";
        export DOCKER_COMPOSE_ARCH="aarch64";
    fi;
    curl -Lo docker_binaries.tgz "https://download.docker.com/linux/static/stable/${DOCKER_ARCH}/docker-${DOCKER_VERSION}.tgz" ;
    tar -xz -f docker_binaries.tgz --strip-components=1 docker/docker ;
    tar -xz -f docker_binaries.tgz --strip-components=1 docker/dockerd ;
    tar -xz -f docker_binaries.tgz --strip-components=1 docker/docker-init ;
    tar -xz -f docker_binaries.tgz --strip-components=1 docker/docker-proxy ;
    curl -Lo docker-compose "https://github.com/docker/compose/releases/download/${DOCKER_COMPOSE_VERSION}/docker-compose-linux-${DOCKER_COMPOSE_ARCH}" ;
    chmod +x docker-compose
EOF

WORKDIR /opt/app
RUN <<EOF
    . /opt/axis/acapsdk/environment-setup*
    acap-build . \
        -a docker \
        -a dockerd \
        -a docker-compose \
        -a docker-init \
        -a docker-proxy \
        -a empty_daemon.json \
        -a ps
EOF

ENTRYPOINT [ "/opt/axis/acapsdk/sysroots/x86_64-pokysdk-linux/usr/bin/eap-install.sh" ]

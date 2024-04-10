# syntax=docker/dockerfile:1

ARG DOCKER_IMAGE_VERSION=26.0.0

ARG REPO=axisecp
ARG ACAPARCH=armv7hf

ARG VERSION=1.13
ARG UBUNTU_VERSION=22.04
ARG NATIVE_SDK=acap-native-sdk

FROM ${REPO}/${NATIVE_SDK}:${VERSION}-${ACAPARCH}-ubuntu${UBUNTU_VERSION} as build_image

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

ARG BUILD_DIR=/opt/build
ARG DOCKER_IMAGE_VERSION
ENV DOCKERVERSION ${DOCKER_IMAGE_VERSION}
ARG ACAPARCH
ENV ARCH ${ACAPARCH}

COPY app /opt/app
COPY --from=ps /export/ps /opt/app

# Install build dependencies for cross compiling libraries
RUN DEBIAN_FRONTEND=noninteractive \
    apt-get update && apt-get install -y -f --no-install-recommends \
    cmake && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Build uriparser library
ARG URIPARSER_VERSION=0.9.7
ARG URIPARSER_DIR=${BUILD_DIR}/uriparser
ARG URIPARSER_SRC_DIR=${BUILD_DIR}/uriparser-${URIPARSER_VERSION}
ARG URIPARSER_BUILD_DIR=${URIPARSER_DIR}/build
WORKDIR ${BUILD_DIR}
SHELL ["/bin/bash", "-o", "pipefail", "-c"]
RUN curl -fsSL https://github.com/uriparser/uriparser/releases/download/uriparser-${URIPARSER_VERSION}/uriparser-${URIPARSER_VERSION}.tar.gz | tar -xz
WORKDIR ${URIPARSER_BUILD_DIR}
ENV COMMON_CMAKE_FLAGS="-S $URIPARSER_SRC_DIR \
        -B $URIPARSER_BUILD_DIR \
        -D CMAKE_INSTALL_PREFIX=$URIPARSER_BUILD_DIR \
        -D CMAKE_BUILD_TYPE=Release .. \
        -D URIPARSER_BUILD_DOCS=OFF \
        -D URIPARSER_BUILD_TESTS=OFF "
# hadolint ignore=SC2086
RUN if [ "$ACAPARCH" = armv7hf ]; then \
        # Source SDK environment to get cross compilation tools
        . /opt/axis/acapsdk/environment-setup* && \
        # Configure build with CMake
        cmake \
        -D CMAKE_CXX_COMPILER=${CXX%-g++*}-g++ \
        -D CMAKE_CXX_FLAGS="${CXX#*-g++}" \
        -D CMAKE_C_COMPILER=${CC%-gcc*}-gcc \
        -D CMAKE_C_FLAGS="${CC#*-gcc}" \
        -D CPU_BASELINE=NEON,VFPV3 \
        -D ENABLE_NEON=ON \
        -D ENABLE_VFPV3=ON \
        $COMMON_CMAKE_FLAGS && \
        # Build and install uriparser
        make -j "$(nproc)" install ; \
    elif [ "$ACAPARCH" = aarch64 ]; then \
        # Source SDK environment to get cross compilation tools
        . /opt/axis/acapsdk/environment-setup* && \
        # Configure build with CMake
        # No need to set NEON and VFP for aarch64 since they are implicitly
        # present in an any standard armv8-a implementation.
        cmake \
        -D CMAKE_CXX_COMPILER=${CXX%-g++*}-g++ \
        -D CMAKE_CXX_FLAGS="${CXX#*-g++}" \
        -D CMAKE_C_COMPILER=${CC%-gcc*}-gcc \
        -D CMAKE_C_FLAGS="${CC#*-gcc}" \
        $COMMON_CMAKE_FLAGS && \
        # Build and install uriparser
        make -j "$(nproc)" install ; \
    else \
        printf "Error: '%s' is not a valid value for the ACAPARCH variable\n", "$ACAPARCH"; \
        exit 1; \
    fi
WORKDIR /opt/app/lib
RUN cp -P /opt/build/uriparser/build/liburiparser.so* .

# Download and extract dockerd and its dependencies
WORKDIR /opt/app
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
EOF

WORKDIR /opt/app
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

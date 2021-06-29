# The Docker Engine ACAP

This is the ACAP packaging of the Docker Engine to be run on Axis devices with
container support. Please note that this is ACAP4 alpha and not ready for production
use.

## Building (default architecture)

    sh build.sh

### armv7hf

    sh build.sh

### aarch64

    Not supported at the moment

## Installing

    docker run --rm axisecp/docker-acap<-arch> <camera ip> <rootpasswd> install

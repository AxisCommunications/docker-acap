# The Docker Engine ACAP

This is the ACAP packaging of the Docker Engine to be run on Axis devices with
container support. Please note that this is ACAP4 alpha and not ready for production
use. 

## Building (default architecture)

    docker build . -t axisecp/docker-acap:latest

### armv7hf

    ARCH=arm make -f Makefile.dockerd
    docker build --build-arg ACAPARCH=armv7hf . -t axisecp/docker-acap-armv7hf:latest

or

    ARCH=arm make acap

which includes the `docker build` step above and everything.

### aarch64

    docker build --build-arg ACAPARCH=aarch64 . -t axisecp/docker-acap-aarch64:latest

or

    ARCH=arm64 make acap

similar to the step for `armv7hf`.

## Installing

    docker run --rm axisecp/docker-acap<-arch> <camera ip> <rootpasswd> install

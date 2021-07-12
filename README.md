# The Docker Engine ACAP

This is the ACAP packaging of the Docker Engine to be run on Axis devices with
container support. Please note that this is ACAP4 alpha and not ready for production
use.

### Building armv7hf

<<<<<<< Updated upstream
    sh build.sh
=======
    sh build.sh armv7hf
>>>>>>> Stashed changes

### Building aarch64

<<<<<<< Updated upstream
    sh build.sh

### aarch64

    Not supported at the moment

## Installing

    docker run --rm axisecp/docker-acap<-arch> <camera ip> <rootpasswd> install
=======
    sh build.sh aarch64

## Installing

    Install the build eap file using the camera GUI.
>>>>>>> Stashed changes

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

## Securing the Docker ACAP using TLS
The Docker ACAP can be run either unsecured or in TLS mode. The Docker ACAP uses TLS as default. Use the "Use TLS" dropdown in the web interface to switch between the two different modes. Note that the dockerd service will be restarted every time TLS is activated or deactivated. Running the ACAP using TLS requires some additional setup, see [TLS Setup](#tls-setup). Running the ACAP without TLS requires no further setup.

### TLS Setup
The following keys and certificates are required to use TLS.
#### The Certificate Authority (CA) certificate
This certificate needs to be present in the dockerdwrapper package folder on the camera and be named "ca.perm". The full path of the file should be "/usr/local/packages/dockerdwrapper/ca.pem".

#### The server certificate
This certificate needs to be present in the dockerdwrapper package folder on the camera and be named "server-cert.perm". The full path of the file should be "/usr/local/packages/dockerdwrapper/server-cert.pem".

#### The private server key
This key needs to be present in the dockerdwrapper package folder on the camera and be named "server-key.perm". The full path of the file should be "/usr/local/packages/dockerdwrapper/server-key.pem".

#### Client keys and certificates
All the clients also needs to have their own private keys. Each client also needs a certificate which has been authorized by the CA. These keys and certificates shall be used when running docker against the dockerd daemon on the camera. See below for an example.

```bash
docker --tlsverify \
       --tlscacert=ca.pem \
       --tlscert=client-cert.pem \
       --tlskey=client-key.pem \
       -H=<axis_device_ip>:2376 \
       version
```

## Using an SD card as storage
An SD card might be necessary to run the dockerdwrapper correctly. Docker containers and docker images can be quite large, and putting them on an SD card gives more freedom in how many and how large containers that can be run. Switching between storage on the SD card or internal storage is done by toggling the "SD card support" dropdown in the web interface. Toggling this setting will automatically restart the docker daemon using the specified storage. The default setting is to use the internal storage on the camera.

Note that dockerdwrapper requires that Unix permissions are supported by the file system. Examples of file system which supports this is ext4, ext3 and xfs. It might be necessary to reformat the SD card to one of these file systems, for example if the original file system of the SD card is vfat.

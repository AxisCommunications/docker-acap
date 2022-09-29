# The Docker ACAP

This is the ACAP packaging of the Docker Engine to be run on Axis devices with container support.

## Compatibility

The Docker ACAP requires a container capable device. You may check the compatibility of your device
by running:

```sh
DEVICE_IP=<device ip>
DEVICE_PASSWORD='<password>'

curl -s --anyauth -u "root:$DEVICE_PASSWORD" \
  "http://$DEVICE_IP/axis-cgi/param.cgi?action=update&root.Network.SSH.Enabled=yes"

ssh root@$DEVICE_IP 'command -v containerd >/dev/null 2>&1 && echo Compatible with Docker ACAP || echo Not compatible with Docker ACAP'
```

where `<device ip>` is the IP address of the Axis device and `<password>` is the root password. Please
note that you need to enclose your password with quotes (`'`) if it contains special characters.

## Installing

The recommended way to install this ACAP is to use the pre-built
[docker hub](https://hub.docker.com/r/axisecp/docker-acap) image:

```sh
docker run --rm axisecp/docker-acap:latest-<ARCH> <device ip> <rootpasswd> install
```

Where `<ARCH>` is either `armv7hf` or `aarch64` depending on device architecture.

It's also possible to build and use a locally built image. See the
[Building the Docker ACAP](#building-the-docker-acap) section for more information.

## Securing the Docker ACAP using TLS

The Docker ACAP can be run either unsecured or in TLS mode. The Docker ACAP uses
TLS as default. Use the "Use TLS" dropdown in the web interface to switch
between the two different modes. It's also possible to toggle this option by
calling the parameter management API in [VAPIX](https://www.axis.com/vapix-library/) and setting the
`root.dockerdwrapper.UseTLS` parameter to `yes` or `no`. The following commands would enable TLS:

```sh
DEVICE_IP=<device ip>
DEVICE_PASSWORD='<password>'

curl -s --anyauth -u "root:$DEVICE_PASSWORD" \
  "http://$DEVICE_IP/axis-cgi/param.cgi?action=update&root.dockerdwrapper.UseTLS=yes"
```

Note that the dockerd service will be restarted every time TLS is activated or
deactivated. Running the ACAP using TLS requires some additional setup, see next chapter.
Running the ACAP without TLS requires no further setup.

### TLS Setup

TLS requires a few keys and certificates to work, which are listed in the
subsections below. For more information on how to generate these files, please
consult the official [Docker documentation](https://docs.docker.com/engine/security/protect-access/).
Most of these keys and certificates need to be moved to the Axis device. There are multiple ways to
achieve this, for example by using `scp` to copy the files from a remote machine onto the device.
This can be done by running the following command on the remote machine:

```sh
scp ca.pem server-cert.pem server-key.pem root@<device ip>:/usr/local/packages/dockerdwrapper/
```

#### The Certificate Authority (CA) certificate

This certificate needs to be present in the dockerdwrapper package folder on the
Axis device and be named `ca.pem`. The full path of the file should be
`/usr/local/packages/dockerdwrapper/ca.pem`.

#### The server certificate

This certificate needs to be present in the dockerdwrapper package folder on the
Axis device and be named `server-cert.pem`. The full path of the file should be
`/usr/local/packages/dockerdwrapper/server-cert.pem`.

#### The private server key

This key needs to be present in the dockerdwrapper package folder on the Axis device
and be named `server-key.pem`. The full path of the file should be
`/usr/local/packages/dockerdwrapper/server-key.pem`.

#### Client key and certificate

A client will need to have its own private key, together with a certificate authorized by the CA.
Key, certificate and CA shall be used when running Docker against the dockerd daemon on
the Axis device. See below for an example:

```sh
DOCKER_PORT=2376
docker --tlsverify \
       --tlscacert=ca.pem \
       --tlscert=cert.pem \
       --tlskey=key.pem \
       -H=<device ip>:$DOCKER_PORT \
       version
```

Specifying the files on each Docker command will soon become tedious. To configure Docker to
automatically use your key and certificate, please export the `DOCKER_CERT_PATH` environment variable:

```sh
export DOCKER_CERT_PATH=<client certificate directory>
DOCKER_PORT=2376
docker --tlsverify \
       -H=<device ip>:$DOCKER_PORT \
       version
```

where `<client certificate directory>` is the directory on your computer where the files `ca.pem`,
`cert.pem` and `key.pem` are stored.

## Using an SD card as storage

An SD card might be necessary to run the Docker ACAP correctly. Docker
containers and docker images can be quite large, and putting them on an SD card
gives more freedom in how many and how large images can be stored. Switching
between storage on the SD card or internal storage is done by toggling the "SD
card support" dropdown in the web interface. It's also possible to toggle this
option by calling the parameter management API in
[VAPIX](https://www.axis.com/vapix-library/) (accessing this documentation
requires creating a free account) and setting the
`root.dockerdwrapper.SDCardSupport` parameter to `yes` or `no`.

Toggling this setting will automatically restart the docker daemon using the
specified storage. The default setting is to use the internal storage on the Axis device.

Note that dockerdwrapper requires that Unix permissions are supported by the
file system. Examples of file systems which support this are ext4, ext3 and xfs.
It might be necessary to reformat the SD card to one of these file systems, for
example if the original file system of the SD card is vfat.

Make sure to use an SD card that has enough capacity to hold your applications.
Other properties of the SD card, like the speed, might also affect the performance of your
applications. For example, the Computer Vision SDK example
[object-detector-python](https://github.com/AxisCommunications/acap-computer-vision-sdk-examples/tree/main/object-detector-python) has a significantly higher inference time when using a small and slow SD card.
To get more informed about specifications, check the
[SD Card Standards](https://www.sdcard.org/developers/sd-standard-overview/).

## Using the Docker ACAP

The Docker ACAP does not contain the docker client binary. This means that all
calls need to be done from a separate machine. This can be achieved by using
the -H flag when running the docker command.

The port used will change depending on if the Docker ACAP runs using TLS or not.
The Docker ACAP will be reachable on port 2375 when running unsecured, and on
port 2376 when running secured using TLS. Please read section
[Securing the Docker ACAP using TLS](#securing-the-docker-acap-using-tls) for
more information.
Below is an example of how to remotely run a docker command on an Axis device running
the Docker ACAP in unsecured mode:

```sh
DOCKER_INSECURE_PORT=2375
docker -H=<device ip>:$DOCKER_INSECURE_PORT version
```

See [Client key and certificate](#client-key-and-certificate) for an example
of how to remotely run docker commands on a device running a secured Docker ACAP
using TLS.

## Building the Docker ACAP

### armv7hf

```sh
./build.sh armv7hf
```

### aarch64

```sh
./build.sh aarch64
```

## Installing a locally built Docker ACAP

Installation can be done in two ways. Either by using the locally built docker image:

```sh
docker run --rm docker-acap:1.0 <device ip> <rootpasswd> install
```

Or by manually navigating to device GUI by browsing to the following page
(replace `<device ip>` with the IP number of your Axis video device)

```sh
http://<axis_device_ip>/#settings/apps
```

Go to your device web page above > Click on the tab **App** in the device GUI >
Add **(+)** sign and browse to the newly built
**Docker_Daemon_1_1_0_<arch>.eap** > Click **Install** > Run the application by
enabling the **Start** switch.

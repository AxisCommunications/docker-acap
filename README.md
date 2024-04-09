# The Docker ACAP

This is the ACAP packaging of the Docker Engine to be run on Axis devices with container support.

## Compatibility

### Device

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

### Host

The host machine is required to have [Docker](https://docs.docker.com/get-docker/) and
[Docker Compose](https://docs.docker.com/compose/install/) installed. To build Docker ACAP locally
it is required to also have [Buildx](https://docs.docker.com/build/install-buildx/) installed.

## Installing

The Docker ACAP application is available as a **signed** eap-file in [Releases][latest-releases].

> [!IMPORTANT]
> From AXIS OS 11.8 `root` user is not allowed by default and in 12.0 it will be disallowed completely. Read more on the [Developer Community](https://www.axis.com/developer-community/news/axis-os-root-acap-signing). \
> Docker ACAP 1.X requires root and work is ongoing to create a version that does not.
> Meanwhile, the solution is to allow root to be able to install the Docker ACAP.
>
> On the web page of the device:
>
> 1. Go to the Apps page, toggle on `Allow root-privileged apps`.
> 1. Go to System -> Account page, under SSH accounts toggle off `Restrict root access` to be able to send the TLS certificates. Make sure to set the password of the `root` SSH user.

The prebuilt Docker ACAP application is signed, read more about signing [here][signing-documentation].

Install and use any image from [prereleases or releases][all-releases] with
a tag on the form `<version>_<ARCH>`, where `<version>` is the docker-acap release
version and `<ARCH>` is either `armv7hf` or `aarch64` depending on device architecture.
E.g. `Docker_Daemon_1_3_0_aarch64_signed.eap`.
The eap-file can be installed as an ACAP application on the device,
where it can be controlled in the device GUI **Apps** tab.

```sh
# Get download url for a signed ACAP with curl
# Where <ARCH> is the architecture
curl -s https://api.github.com/repos/AxisCommunications/docker-acap/releases/latest | grep "browser_download_url.*Docker_Daemon_.*_<ARCH>\_signed.eap"
```

### Installation of version 1.3.0 and previous

To install this ACAP for version 1.3.0 or previous use the pre-built
[docker hub](https://hub.docker.com/r/axisecp/docker-acap) image:

```sh
docker run --rm axisecp/docker-acap:latest-<ARCH> <device ip> <rootpasswd> install
```

Where `<ARCH>` is either `armv7hf` or `aarch64` depending on device architecture.

It's also possible to build and use a locally built image. See the
[Building the Docker ACAP](#building-the-docker-acap) section for more information.

## Securing the Docker ACAP using TLS

The Docker Compose ACAP application can be run in either TLS mode or unsecured mode. The Docker Compose
ACAP application uses TLS mode by default. It is important to note that Dockerd will fail to start if
TCP socket or IPC socket parameters are not selected, one of these sockets must be set to `yes`.

Use the "Use TLS" and "TCP Socket" dropdowns in the web interface to switch between the
two different modes(yes/no). Whenever these settings change, the Docker daemon will automatically restart.
It's also possible to toggle this option by calling the parameter management API in
[VAPIX](https://www.axis.com/vapix-library/) and setting `root.dockerdwrapperwithcompose.UseTLS` and
`root.dockerdwrapperwithcompose.TCPSocket` parameters to `yes` or `no`.
The following commands would enable those parameters:

```sh
DEVICE_IP=<device ip>
DEVICE_PASSWORD='<password>'
```

Enable TLS:

```sh
curl -s --anyauth -u "root:$DEVICE_PASSWORD" \
  "http://$DEVICE_IP/axis-cgi/param.cgi?action=update&root.dockerdwrapper.UseTLS=yes"
```

Enable TCP Socket:

```sh
curl -s --anyauth -u "root:$DEVICE_PASSWORD" \
  "http://$DEVICE_IP/axis-cgi/param.cgi?action=update&root.dockerdwrapperwithcompose.TCPSocket=yes"
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
scp ca.pem server-cert.pem server-key.pem root@<device ip>:/usr/local/packages/dockerdwrapper/localdata/
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
[object-detector-python](https://github.com/AxisCommunications/acap-computer-vision-sdk-examples/tree/main/object-detector-python)
has a significantly higher inference time when using a small and slow SD card.
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

The application can provide a TCP socket if the TCP Socket setting is set to `yes` and an IPC socket
if the IPC Socket setting is set to `yes`. Please be aware that at least one of these sockets must be
selected for the application to start.

## Status codes

The application use a parameter called `Status` to inform about what state it is currently in.
The value can be read with a call to the VAPIX param.cgi API, e.g. by using curl:

```sh
curl --anyauth -u <user:user password> \
  'http://<device ip>/axis-cgi/param.cgi?action=list&group=root.DockerdWrapper.Status'
```

Following are the possible values of `Status`:

```text
-1 NOT STARTED                The application is not started.
 0 RUNNING                    The application is started and dockerd is running.
 1 TLS CERT MISSING           Use TLS is selected but there but certificates are missing on the device.
                              The application is running but dockerd is stopped.
                              Upload certificates and restart the application.
 2 NO SOCKET                  Neither TCP Socket or IPC Socket are selected.
                              The application has stopped.
                              Select one or both sockets and start the application.
 3 NO SD CARD                 Use SD Card is selected but no SD Card is mounted in the device.
                              The application is running but dockerd is stopped.
                              Insert and mount a SD Card.
 4 SD CARD WRONG FS           Use SD Card is selected but the mounted SD Card has the wrong file system.
                              The application is running but dockerd is stopped.
                              Format the SD Card with the correct file system.
 5 SD CARD WRONG PERMISSION   Use SD Card is selected but the application user does not have the correct file
                              permissions to use it.
                              The application is running but dockerd is stopped.
                              Make sure no directories with the wrong user permissions are left on the
                              SD Card.
```

## Building the Docker ACAP

To build the Docker ACAP use docker buildx with the provided Dockerfile:

```sh
# Build Docker ACAP image
docker buildx build --file Dockerfile --tag docker-acap:<ARCH> --build-arg ACAPARCH=<ARCH> --output <build-folder> .
```

where `<ARCH>` is either `armv7hf` or `aarch64`. `<build-folder>` is the path to an output folder
on your machine, eg. `build`. This will be created for you if not already existing.
Once the build has completed the Docker ACAP eap-file can be found in the `<build-folder>`.

## Installing a locally built Docker ACAP

Installation can be done either by running the locally built docker image:

```sh
docker run --rm docker-acap:<ARCH> <device ip> <rootpasswd> install
```

Or by manually installing the .eap file from the `build` folder by using the Web GUI in the device:

```sh
http://<device ip>/#settings/apps
```

Go to your device web page above > Click on the tab **App** in the device GUI >
Add **(+)** sign and browse to the newly built .eap-file > Click **Install** > Run the application by
enabling the **Start** switch.

## Contributing

Take a look at the [CONTRIBUTING.md](CONTRIBUTING.md) file.

## License

[Apache 2.0](LICENSE)

<!-- Links to external references -->
<!-- markdownlint-disable MD034 -->
[all-releases]: https://github.com/AxisCommunications/docker-acap/releases
[latest-releases]: https://github.com/AxisCommunications/docker-acap/releases/latest
[signing-documentation]: https://axiscommunications.github.io/acap-documentation/docs/faq/security.html#sign-acap-applications

<!-- markdownlint-enable MD034 -->

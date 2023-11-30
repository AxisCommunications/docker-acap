<!-- omit in toc -->
# The Docker ACAP

The Docker ACAP application provides the means to run rootless Docker on a compatible Axis
device.

> **Note**
>
> This is a preview of the rootless Docker ACAP. Even though it uses a non-root user at runtime,
> it requires root privileges during installation and uninstallation. This can be accomplished by
> setting the `AllowRoot` toggle to `true` when installing and uninstalling the application.
> See the [VAPIX documentation][vapix-allow-root] for details.
>
> **Known Issues**
>
> * Only uid and gid are properly mapped between device and containers, not the other groups that
> the user is a member of. This means that resources on the device, even if they are volume or device
> mounted can be inaccessible inside the container. This can also affect usage of unsupported dbus
>  methods from the container. See [Using host user secondary groups in container](#using-host-user-secondary-groups-in-container)
> for how to handle this.
> * iptables use is disabled.

<!-- omit in toc -->
## Table of contents

* [Overview](#overview)
* [Requirements](#requirements)
* [Installation and Usage](#installation-and-usage)
  * [Installation](#installation)
  * [Securing the Docker ACAP using TLS](#securing-the-docker-acap-using-tls)
  * [Using an SD card as storage](#using-an-sd-card-as-storage)
  * [Using the Docker ACAP](#using-the-docker-acap)
* [Building the Docker ACAP](#building-the-docker-acap)
* [Installing a locally built Docker ACAP](#installing-a-locally-built-docker-acap)
* [Contributing](#contributing)
* [License](#license)

## Overview

The Docker ACAP provides the means to run a Docker daemon on an Axis device, thereby
making it possible to deploy and run Docker containers on it. When started the daemon
will run in rootless mode, i.e. the user owning the daemon process will not be root,
and in extension, the containers will not have root access to the host system.
See [Rootless Mode][docker-rootless-mode] on Docker.com for details. That page also
contains known limitations when running rootless Docker.

> **Note**
>
> The Docker ACAP application can be run with TLS authentication or without.
> Be aware that running without TLS authentication is extremely insecure and we
strongly recommend against this.
> See [Securing the Docker ACAP using TLS](#securing-the-docker-acap-using-tls)
for information on how to generate certificates for TLS authentication when using
the Docker ACAP application.

## Requirements

The following requirements need to be met.

* Axis device:
  * Axis OS version 11.7 or higher.
  * The device needs to have ACAP Native SDK support. See [Axis devices & compatibility][devices]
  for more information.
  * Additionally, the device must be container capable. To check the compatibility
  of your device run:

```sh
DEVICE_IP=<device ip>
DEVICE_PASSWORD='<password>'

curl -s --anyauth -u "root:$DEVICE_PASSWORD" \
  "http://$DEVICE_IP/axis-cgi/param.cgi?action=update&root.Network.SSH.Enabled=yes"

ssh root@$DEVICE_IP 'command -v containerd >/dev/null 2>&1 && echo Compatible with Docker ACAP || echo Not compatible with Docker ACAP'
```

where `<device ip>` is the IP address of the Axis device and `<password>` is the root password. Please
note that you need to enclose your password with quotes (`'`) if it contains special characters.

* Computer:
  * Either [Docker Desktop][dockerDesktop] version 4.11.1 or higher, or
  [Docker Engine][dockerEngine] version 20.10.17 or higher.
  * To build Docker ACAP locally it is required to have [Buildx][buildx] installed.

## Installation and Usage

The Docker ACAP application is available as a **signed** eap-file in [Releases][latest-releases].
The prebuilt Docker ACAP application is signed, read more about signing [here][signing-documentation].

### Installation

Install and use any signed eap-file from [prereleases or releases][all-releases] with
a tag on the form `<version>_<ARCH>`, where `<version>` is the docker-acap release
version and `<ARCH>` is either `armv7hf` or `aarch64` depending on device architecture.
E.g. `Docker_Daemon_2_0_0_aarch64_signed.eap`.
The eap-file can be installed as an ACAP application on the device,
where it can be controlled in the device GUI **Apps** tab.

```sh
# Get download url for a signed ACAP with curl
# Where <ARCH> is the architecture
curl -s https://api.github.com/repos/AxisCommunications/docker-acap/releases/latest | grep "browser_download_url.*Docker_Daemon_.*_<ARCH>\_signed.eap"
```

### Securing the Docker ACAP using TLS

The Docker ACAP can be run either unsecured or in TLS mode. The Docker ACAP uses
TLS as default. Use the "Use TLS" dropdown in the web interface to switch
between the two different modes. It's also possible to toggle this option by
calling the parameter management API in [VAPIX][vapix] and setting the
`root.dockerdwrapper.UseTLS` parameter to `yes` or `no`. The following commands would enable TLS:

```sh
export DEVICE_IP=<device ip>

curl -s --anyauth -u "root:<device root password>" \
  "http://$DEVICE_IP/axis-cgi/param.cgi?action=update&root.dockerdwrapper.UseTLS=yes"
```

Note that the dockerd service will be restarted every time TLS is activated or
deactivated. Running the ACAP using TLS requires some additional setup, see next chapter.
Running the ACAP without TLS requires no further setup.

#### TLS Setup

TLS requires a keys and certificates to work, which are listed in the
subsections below. For more information on how to generate these files, please
consult the official [Docker documentation][docker-tls].
Most of these keys and certificates need to be moved to the Axis device. There are multiple ways to
achieve this, for example by using `scp` to copy the files from a remote machine onto the device.
This can be done by running the following command on the remote machine:

```sh
scp ca.pem server-cert.pem server-key.pem root@<device ip>:/usr/local/packages/dockerdwrapper/
```

Once copied to the Axis device the correct permissions need to be set for the certificates:

```sh
ssh root@<device IP> 'chown acap-dockerdwrapper /usr/local/packages/dockerdwrapper/*.pem'

```

##### The Certificate Authority (CA) certificate

This certificate needs to be present in the dockerdwrapper package folder on the
Axis device and be named `ca.pem`. The full path of the file should be
`/usr/local/packages/dockerdwrapper/ca.pem`.

##### The server certificate

This certificate needs to be present in the dockerdwrapper package folder on the
Axis device and be named `server-cert.pem`. The full path of the file should be
`/usr/local/packages/dockerdwrapper/server-cert.pem`.

##### The private server key

This key needs to be present in the dockerdwrapper package folder on the Axis device
and be named `server-key.pem`. The full path of the file should be
`/usr/local/packages/dockerdwrapper/server-key.pem`.

##### Client key and certificate

A client will need to have its own private key, together with a certificate authorized by the CA.
Key, certificate and CA shall be used when running Docker against the dockerd daemon on
the Axis device. See below for an example:

```sh
DOCKER_PORT=2376
docker --tlsverify \
       --tlscacert=ca.pem \
       --tlscert=client-cert.pem \
       --tlskey=client-key.pem \
       --host tcp://$DEVICE_IP:$DOCKER_PORT \
       version
```

Specifying the files on each Docker command will soon become tedious. To configure Docker to
automatically use your key and certificate, please export the `DOCKER_CERT_PATH` environment variable:

```sh
export DOCKER_CERT_PATH=<client certificate directory>
DOCKER_PORT=2376
docker --tlsverify \
       --host tcp://$DEVICE_IP:$DOCKER_PORT \
       version
```

where `<client certificate directory>` is the directory on your computer where the files `ca.pem`,
`client-cert.pem` and `client-key.pem` are stored.

### Using an SD card as storage

An SD card might be necessary to run the Docker ACAP correctly. Docker
containers and docker images can be quite large, and putting them on an SD card
gives more freedom in how many and how large images can be stored. Switching
between storage on the SD card or internal storage is done by toggling the "SD
card support" dropdown in the web interface. It's also possible to toggle this
option by calling the parameter management API in
[VAPIX][vapix] (accessing this documentation
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
[object-detector-python][object-detector-python]
has a significantly higher inference time when using a small and slow SD card.
To get more informed about specifications, check the
[SD Card Standards][sd-card-standards].

>**Note**
>
>If Docker ACAP v1.4 or previous has been used on the device with SD card as storage
>the storage directory might already be created with root permissions.
>Since v2.0 the Docker ACAP is run in rootless mode and it will then not be able
>to access that directory. To solve this, either reformat the SD card or manually
>remove the directory that is used by the Docker ACAP.

### Using the Docker ACAP

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
docker --host tcp://$DEVICE_IP:$DOCKER_INSECURE_PORT version
```

See [Client key and certificate](#client-key-and-certificate) for an example
of how to remotely run docker commands on a device running a secured Docker ACAP
using TLS.

#### Test that the Docker ACAP can run a container

Make sure the Docker ACAP, using TLS, is running, then pull and run the
[hello-world][docker-hello-world] image from Docker Hub:

```sh
>docker --tlsverify --host tcp://$DEVICE_IP:$DOCKER_PORT pull hello-world
Using default tag: latest
latest: Pulling from library/hello-world
70f5ac315c5a: Pull complete 
Digest: sha256:88ec0acaa3ec199d3b7eaf73588f4518c25f9d34f58ce9a0df68429c5af48e8d
Status: Downloaded newer image for hello-world:latest
docker.io/library/hello-world:latest
>docker --tlsverify --host tcp://$DEVICE_IP:$DOCKER_PORT run hello-world

Hello from Docker!
This message shows that your installation appears to be working correctly.

To generate this message, Docker took the following steps:
 1. The Docker client contacted the Docker daemon.
 2. The Docker daemon pulled the "hello-world" image from the Docker Hub.
    (arm64v8)
 3. The Docker daemon created a new container from that image which runs the
    executable that produces the output you are currently reading.
 4. The Docker daemon streamed that output to the Docker client, which sent it
    to your terminal.

To try something more ambitious, you can run an Ubuntu container with:
 $ docker run -it ubuntu bash

Share images, automate workflows, and more with a free Docker ID:
 https://hub.docker.com/

For more examples and ideas, visit:
 https://docs.docker.com/get-started/

```

#### Loading images onto a device

If you have images in a local repository that you want to transfer to a device, or
if you have problems getting the `pull` command to work in your environment, `save`
and `load` can be used.

```sh
docker save <image on host local repository> | docker --tlsverify --host tcp://$DEVICE_IP:$DOCKER_PORT load
```

#### Using host user secondary groups in container

The Docker Compose ACAP is run by a non-root user on the device. This user is set
up to be a member in a number of secondary groups as listed in the
[manifest.json](https://github.com/AxisCommunications/docker-compose-acap/blob/rootless-preview/app/manifest.json#L6-L11)
file. When running a container a user called `root`, (uid 0), belonging to group `root`, (gid 0)
will be the default user inside the container. It will be mapped to the non-root user on
the device, and the group will be mapped to the non-root users primary group.
In order to get access inside the container to resources on the device that are group owned by any
of the non-root users secondary groups these need to be added for the container user.
This can be done by using `group_add` in a docker-compose.yaml (`--group-add` if using Docker cli).
Unfortunately, adding the names of the secondary groups are not supported, instead the *mapped* ids
of the groups need to be used. At the moment of writing this the mappings are

| device group | container group id |
| ------------ | ------------------ |
| datacache    | "1"                |
| sdk          | "2"                |
| storage      | "3"                |
| vdo          | "4"                |
| optics       | "5"                |

Note that the names of the groups will not be correctly displayed inside the container.

## Building the Docker ACAP

To build the Docker ACAP use docker buildx with the provided Dockerfile:

```sh
# Build Docker ACAP image
docker buildx build --file Dockerfile --tag docker-acap:<ARCH> --build-arg ACAPARCH=<ARCH> .
```

where `<ARCH>` is either `armv7hf` or `aarch64`.

To extract the Docker ACAP eap-file use docker cp to copy it to a `build` folder:

```sh
docker cp "$(docker create "docker-acap:<ARCH>")":/opt/app/ ./build
```

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
[buildx]: https://docs.docker.com/build/install-buildx/
[devices]: https://axiscommunications.github.io/acap-documentation/docs/axis-devices-and-compatibility#sdk-and-device-compatibility
[dockerDesktop]: https://docs.docker.com/desktop/
[dockerEngine]: https://docs.docker.com/engine/
[docker-hello-world]: https://hub.docker.com/_/hello-world
[docker-tls]: https://docs.docker.com/engine/security/protect-access/
[docker-rootless-mode]: https://docs.docker.com/engine/security/rootless/
[latest-releases]: https://github.com/AxisCommunications/docker-acap/releases/latest
[object-detector-python]: https://github.com/AxisCommunications/acap-computer-vision-sdk-examples/tree/main/object-detector-python
[sd-card-standards]: https://www.sdcard.org/developers/sd-standard-overview/
[signing-documentation]: https://axiscommunications.github.io/acap-documentation/docs/faq/security.html#sign-acap-applications
[vapix]: https://www.axis.com/vapix-library/
[vapix-allow-root]: https://www.axis.com/vapix-library/subjects/t10102231/section/t10036126/display?section=t10036126-t10185050
<!-- markdownlint-enable MD034 -->

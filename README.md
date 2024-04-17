<!-- omit in toc -->
# The Docker ACAP application

The Docker ACAP application provides the means to run Docker on a compatible Axis
device.

<!-- omit in toc -->
## Notable Releases
<!-- markdownlint-disable MD013 -->
| Release                 | AXIS OS min. version | Dockerd version | Type     | Comment                         |
| ----------------------: | -------------------: | --------------: |----------|---------------------------------|
| [3.0.N][latest-release] | 11.10                | 26.0.0          | rootless | Latest release                  |
| [2.0.0][2.0.0-release]  | 11.9                 | 26.0.0          | rootful  | Legacy release AXIS OS 2024 LTS |
| [1.5.0][1.5.0-release]  | 10.12                | 20.10.9         | rootful  | Legacy release AXIS OS 2022 LTS |

<!-- markdownlint-enable MD013 -->
> [!IMPORTANT]
> From AXIS OS 12.0 running 'rootful' ACAP applications, i.e. an application setup with the `root` user,
> will no longer be supported. To install a 'rootful' ACAP application on a device running AXIS OS
> versions between 11.5 and 11.11, allow root must be enabled. See the [VAPIX documentation][vapix-allow-root]
> for details. Alternatively, On the web page of the device:
>
> 1. Go to the Apps page, toggle on `Allow root-privileged apps`.
> 1. Go to System -> Account page, under SSH accounts toggle off `Restrict root access` to be able to
> send the TLS certificates. Make sure to set the password of the `root` SSH user.

<!-- omit in toc -->
## Table of contents

- [Overview](#overview)
- [Requirements](#requirements)
- [Installation and Usage](#installation-and-usage)
  - [Download a pre-built eap-file](#download-a-pre-built-eap-file)
  - [Installation](#installation)
  - [Using TLS to secure the Docker ACAP application](#using-tls-to-secure-the-docker-acap-application)
  - [Using an SD card as storage](#using-an-sd-card-as-storage)
  - [Using the Docker ACAP application](#using-the-docker-acap-application)
- [Building the Docker ACAP application](#building-the-docker-acap-application)
- [Installing a locally built Docker ACAP application](#installing-a-locally-built-docker-acap-application)
- [Contributing](#contributing)
- [License](#license)

## Overview

> [!NOTE]
>
> The Docker ACAP application can be run with TLS authentication or without.
> Be aware that running without TLS authentication is extremely insecure and we
> strongly recommend against this.
> See [Using TLS to secure the Docker ACAP application](#using-tls-to-secure-the-docker-acap-application)
> for information on how to generate certificates for TLS authentication when using
> the Docker ACAP application.

The Docker ACAP application provides the means to run a Docker daemon on an Axis device, thereby
making it possible to deploy and run Docker containers on it. When started the daemon
will run in rootless mode, i.e. the user owning the daemon process will not be root,
and by extension, the containers will not have root access to the host system.
See [Rootless Mode][docker-rootless-mode] on Docker.com for more information. The page also
contains known limitations when running rootless Docker.

<!-- omit in toc -->
### Known Issues

- Only uid and gid are properly mapped between device and containers, not the secondary groups that the
user is a member of. This means that resources on the device, even if they are volume or device mounted
can be inaccessible inside the container. This can also affect usage of unsupported D-Bus methods from
the container. See [Using host user secondary groups in container](#using-host-user-secondary-groups-in-container)
for how to handle this.
- iptables use is disabled.

## Requirements

The following requirements need to be met for running the Docker ACAP application built from the
main branch.

- Axis device:
  - AXIS OS version 11.10 or higher.
  - The device needs to have ACAP Native SDK support. See [Axis devices & compatibility][devices]
  for more information.
  - The device must be [container capable](#container-capability).
- Computer:
  - Either [Docker Desktop][dockerDesktop] version 4.11.1 or higher, or
  [Docker Engine][dockerEngine] version 20.10.17 or higher.
  - To build Docker ACAP locally it is required to have [Buildx][buildx] installed.

<!-- omit in toc -->
### Container capability

A list of Container capable Axis devices can be found on the Axis [Product Selector][product-selector]
page by checking the [Container support check box][product-selector-container].

## Installation and Usage

The following substitutes will be used in this documentation:

| Substitute    | Meaning                                           |
| --------------| --------------------------------------------------|
| `<ARCH>`      | Device architecture, either `armv7hf`or `aarch64` |
| `<device-ip>` | The IP of the device                              |

### Download a pre-built eap-file

Download the the eap-file for the architecture of your device from [Releases][latest-release].
From command line this can be done with:

```sh
curl -s https://api.github.com/repos/AxisCommunications/docker-acap/releases/latest \
 | grep "browser_download_url.*Docker_Daemon_.*_<ARCH>\_signed.eap"
```

The prebuilt Docker ACAP application is signed, read more about signing
[here][signing-documentation].

### Installation

> [!NOTE]
> Migrating from rootful Docker ACAP application
>
> If you are upgrading from a rootful Docker ACAP application, i.e, any version before 3.0,
> the following is recommended:
>
>- Copy any Docker images that you want to persist from the device to your computer.
>- Stop the Docker ACAP application.
>- Uninstall the Docker ACAP application.
>- If you use the SD card as storage either format it or manually remove the `dockerd` directory (`/var/spool/storage/SD_DISK/dockerd`).
>- Restart the device.
>- Install the rootless Docker ACAP application.

Installation can be done by using either the device web ui or the [VAPIX][vapix] application API.

#### Installation via web ui

Navigate to `<device-ip>/camera/index.html#/apps`, then click on the `+Add app` button on the page.
In the popup window that appears, select the eap-file to install.

### Using TLS to secure the Docker ACAP application

When using the Docker ACAP application with TCP socket, the application can be run in either TLS or
unsecured mode. The default selection is to use TLS mode. To change this use the "Use TLS" dropdown
in the web interface to switch between the two different modes. It's also possible to toggle this
option by calling the parameter management API in [VAPIX][vapix] and setting the
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

An SD card might be necessary to run the Docker ACAP application correctly. Docker
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

> [!NOTE]
>
>If Docker ACAP application v1.4 or previous has been used on the device with SD card as storage
>the storage directory might already be created with root permissions.
>Since v2.0 the Docker ACAP application is run in rootless mode and it will then not be able
>to access that directory. To solve this, either reformat the SD card or manually
>remove the directory that is used by the Docker ACAP application.

### Using the Docker ACAP application

The Docker ACAP application does not contain the docker client binary. This means that all
calls need to be done from a separate machine. This can be achieved by using
the `--host` flag when running the docker command.

The port used will change depending on if the Docker ACAP application runs using TLS or not.
The Docker ACAP application will be reachable on port 2375 when running unsecured, and on
port 2376 when running secured using TLS. Please read section
[Using TLS to secure the Docker ACAP application](#using-tls-to-secure-the-docker-acap-application) for
more information.
Below is an example of how to remotely run a docker command on an Axis device running
the Docker ACAP application in unsecured mode:

```sh
DOCKER_INSECURE_PORT=2375
docker --host tcp://$DEVICE_IP:$DOCKER_INSECURE_PORT version
```

See [Client key and certificate](#client-key-and-certificate) for an example
of how to remotely run docker commands on a device running a secured Docker ACAP application
using TLS.

#### Run a container using the Docker ACAP application

Make sure the application, using TLS, is running, then pull and run the
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

The Docker ACAP application is run by a non-root user on the device. This user is set
up to be a member in a number of secondary groups as listed in the
[manifest.json](https://github.com/AxisCommunications/docker-compose-acap/blob/rootless-preview/app/manifest.json#L6-L11)
file. When running a container, a user called `root`, (uid 0), belonging to group `root`, (gid 0),
will be the default user inside the container. It will be mapped to the non-root user on
the device, and the group will be mapped to the non-root users primary group.
In order to get access inside the container to resources on the device that are group owned by any
of the non-root users secondary groups, these need to be added for the container user.
This can be done by using `group_add` in a docker-compose.yaml or `--group-add` if using the Docker cli.
Unfortunately, adding the name of a secondary group is not supported. Instead the *mapped* id
of the group need to be used. At the moment of writing this the mappings are:

| device group | container group id |
| ------------ | :----------------: |
| datacache    | "1"                |
| sdk          | "2"                |
| storage      | "3"                |
| vdo          | "4"                |
| optics       | "5"                |

Note that the names of the groups will *not* be correctly displayed inside the container.

## Building the Docker ACAP application

This repository provides a build script that uses Docker to build the application and then extract
the .eap file:

```sh
./build.sh <ARCH>
```

where `<ARCH>` is either `armv7hf` or `aarch64`.

To run the individual commands manually instead:

```sh
# Build Docker ACAP image
docker buildx build --file Dockerfile --tag docker-acap:<ARCH> --build-arg ACAPARCH=<ARCH> .

# Extract the eap-file into a folder called `build`
docker cp "$(docker create "docker-acap:<ARCH>")":/opt/app/ ./build
```

## Installing a locally built Docker ACAP application

Installation can be done either by running the locally built docker image:

```sh
docker run --rm docker-acap:<ARCH> <device ip> <rootpasswd> install
```

Or by manually installing the .eap file by using the Web GUI in the device:

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
[1.5.0-release]: https://github.com/AxisCommunications/docker-acap/releases/tag/1.5.0
[2.0.0-release]: https://github.com/AxisCommunications/docker-acap/releases/tag/2.0.0
[buildx]: https://docs.docker.com/build/install-buildx/
[devices]: https://axiscommunications.github.io/acap-documentation/docs/axis-devices-and-compatibility#sdk-and-device-compatibility
[dockerDesktop]: https://docs.docker.com/desktop/
[dockerEngine]: https://docs.docker.com/engine/
[docker-hello-world]: https://hub.docker.com/_/hello-world
[docker-tls]: https://docs.docker.com/engine/security/protect-access/
[docker-rootless-mode]: https://docs.docker.com/engine/security/rootless/
[latest-release]: https://github.com/AxisCommunications/docker-acap/releases/latest
[object-detector-python]: https://github.com/AxisCommunications/acap-computer-vision-sdk-examples/tree/main/object-detector-python
[product-selector]: https://www.axis.com/support/tools/product-selector
[product-selector-container]: https://www.axis.com/support/tools/product-selector/shared/%5B%7B%22index%22%3A%5B4%2C2%5D%2C%22value%22%3A%22Yes%22%7D%5D
[sd-card-standards]: https://www.sdcard.org/developers/sd-standard-overview/
[signing-documentation]: https://axiscommunications.github.io/acap-documentation/docs/faq/security.html#sign-acap-applications
[vapix]: https://www.axis.com/vapix-library/
[vapix-allow-root]: https://www.axis.com/vapix-library/subjects/t10102231/section/t10036126/display?section=t10036126-t10185050
<!-- markdownlint-enable MD034 -->

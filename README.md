<!-- omit in toc -->
# The Docker ACAP application

The Docker ACAP application, from here on called the application, provides the means to run Docker on
a compatible Axis device.

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
> From AXIS OS 12.0, running 'rootful' ACAP applications, i.e. an application setup with the `root` user,
> will no longer be supported. To install a 'rootful' ACAP application on a device running AXIS OS
> versions between 11.5 and 11.11, allow root must be enabled. See the [VAPIX documentation][vapix-allow-root]
> for details. Alternatively, on the web page of the device:
>
> 1. Go to the Apps page, toggle on `Allow root-privileged apps`.
> 2. Go to System → Account page, under SSH accounts, toggle off `Restrict root access` to be able to
> send the TLS certificates. Make sure to set the password of the `root` SSH user.

<!-- omit in toc -->
## Table of contents

- [Overview](#overview)
- [Requirements](#requirements)
- [Substitutions](#substitutions)
- [Installation and Usage](#installation-and-usage)
  - [Download a pre-built EAP file](#download-a-pre-built-eap-file)
  - [Installation](#installation)
  - [Settings](#settings)
  - [Using TLS to secure the application](#using-tls-to-secure-the-application)
  - [Using an SD card as storage](#using-an-sd-card-as-storage)
  - [Using the application](#using-the-application)
- [Building the application](#building-the-application)
- [Contributing](#contributing)
- [License](#license)

## Overview

> [!NOTE]
>
> When TCP socket is selected, the application can be run with TLS authentication or without.
> Be aware that running without TLS authentication is extremely insecure and we
> strongly recommend against this.
> See [Using TLS to secure the application](#using-tls-to-secure-the-application)
> for information on how to generate certificates for TLS authentication.

The application provides the means to run a Docker daemon on an Axis device, thereby
making it possible to deploy and run Docker containers on it. When started, the daemon
will run in rootless mode, i.e. the user owning the daemon process will not be root,
and by extension, the containers will not have root access to the host system.
See [Rootless Mode][docker-rootless-mode] on Docker.com for more information. That page also
contains known limitations when running rootless Docker.

<!-- omit in toc -->
### Known Issues

- Only uid and gid are properly mapped between device and containers, not the secondary groups that the
user is a member of. This means that resources on the device, even if they are volume or device mounted,
can be inaccessible inside the container. This can also affect usage of unsupported D-Bus methods from
the container. See [Using host user secondary groups in container](#using-host-user-secondary-groups-in-container)
for how to handle this.
- iptables use is disabled.

## Requirements

The following requirements need to be met for running the application built from the
main branch.

- Axis device:
  - AXIS OS version 11.10 or higher.
  - The device needs to have ACAP Native SDK support. See [Axis devices & compatibility][devices]
  for more information.
  - The device must be [container capable](#container-capability).
- Computer:
  - Either [Docker Desktop][dockerDesktop] version 4.11.1 or higher, or
  [Docker Engine][dockerEngine] version 20.10.17 or higher.
  - To build the application locally it is required to have [Buildx][buildx] installed.

<!-- omit in toc -->
### Container capability

A list of Container capable Axis devices can be found on the Axis [Product Selector][product-selector]
page by checking the [Container support check box][product-selector-container].

## Substitutions

The following substitutions will be used in this documentation:

|                      | Meaning                                                |
| ---------------------| :------------------------------------------------------|
| `<application-name>` | `dockerdwrapper`                                       |
| `<ARCH>`             | Device architecture, either `armv7hf`or `aarch64`      |
| `<device-ip>`        | The IP address of the device                           |
| `<user>`             | The name of a user on the device with admin rights     |
| `<password>`         | The password of a user on the device with admin rights |

## Installation and Usage

### Download a pre-built EAP file

Download the EAP file for the architecture of your device from [Releases][latest-release].
From the command line this can be done with:

```sh
curl -s https://api.github.com/repos/AxisCommunications/docker-acap/releases/latest \
 | grep "browser_download_url.*Docker_Daemon_.*_<ARCH>\_signed.eap"
```

The prebuilt application is signed. Read more about signing
[here][signing-documentation].

### Installation

> [!NOTE]
> **Migrating from rootful application**
>
> If you are upgrading from a rootful version of this application, i.e, any version before 3.0,
> the following is recommended:
>
>- Copy any Docker images that you want to persist from the device to your computer.
>- Stop the application.
>- Uninstall the application.
>- Format the SD card if you will use it with the application. Make sure to manually
>  back up any data you wish to keep first.
>- Restart the device.
>- Install the rootless application.

Installation can be done by using either the [device web ui](#installation-via-the-device-web-ui) or
the [VAPIX application API][vapix-install].

#### Installation via the device web ui

Navigate to `<device-ip>/camera/index.html#/apps`, then click on the `+Add app` button on the page.
In the popup window that appears, select the EAP file to install.

### Settings

Settings can be accessed either in the device web ui, or via [VAPIX][vapix], eg. using curl:

```sh
# To read "<setting-name>"
curl -s anyauth -u "<user>:<password>" \
"http://<device-ip>/axis-cgi/param.cgi?action=list&group=root.<application-name>.<setting-name>"

# To update "<setting-name>" to "<new-value>"
curl -s anyauth -u "<user>:<password>" \
"http://<device-ip>/axis-cgi/param.cgi?action=update&root.<application-name>.<setting-name>=<new-value>"
```

Note that changing the settings while the application is running will lead to dockerd being restarted.

The following settings are available
| Setting                              | Type    | Action | Possible values                       |
| :----------------------------------- | :------ | :----: |---------------------------------------|
| [SDCardSupport](#sd-card-support)    | Boolean | RW     | `yes`,`no`                            |
| [UseTLS](#use-tls)                   | Boolean | RW     | `yes`,`no`                            |
| [TCPSocket](#tcp-socket--ipc-socket) | Boolean | RW     | `yes`,`no`                            |
| [IPCSocket](#tcp-socket--ipc-socket) | Boolean | RW     | `yes`,`no`                            |
| [ApplicationLogLevel](#log-levels)   | Enum    | RW     | `debug`,`info`                        |
| [DockerdLogLevel](#log-levels)       | Enum    | RW     | `debug`,`info`,`warn`,`error`,`fatal` |
| [Status](#status-codes)              | String  | R      | See [Status Codes](#status-codes)     |

#### SD card support

Selects if the docker daemon data-root should be on the internal storage of the device (default) or on
an SD card. See [Using an SD card as storage](#using-an-sd-card-as-storage) for further information.

#### TCP Socket / IPC Socket

To be able to connect remotely to the docker daemon on the device, `TCP Socket` needs to be selected.
`IPC Socket` needs to be selected for containers running on the device to be able to communicate with each other.
At least one of the sockets needs to be selected for the application to start dockerd.

#### Use TLS

Toggle to select if TLS should be disabled when using `TCP Socket`. See
[Using TLS to secure the application](#using-tls-to-secure-the-application) for further information.

#### Log levels

Log levels are set separately for the application and for dockerd. For rootlesskit the log level is
set to `debug` if `DockerdLogLevel` is set to `debug`.

#### Status codes

The application use a parameter called `Status` to inform about what state it is currently in.

Following are the possible values of `Status`:

```text
-1 NOT STARTED                The application is not started.
 0 RUNNING                    The application is started and dockerd is running.
 1 DOCKERD STOPPED            Dockerd was stopped successfully and will soon be restarted.
 2 DOCKERD RUNTIME ERROR      Dockerd has reported an error during runtime that needs to be resolved by the operator.
                              Change at least one parameter or restart the application in order to start dockerd again.
 3 TLS CERT MISSING           Use TLS is selected but there but certificates are missing on the device.
                              The application is running but dockerd is stopped.
                              Upload certificates and restart the application or de-select Use TLS.
 4 NO SOCKET                  Neither TCP Socket or IPC Socket are selected.
                              The application is running but dockerd is stopped.
                              Select one or both sockets.
 5 NO SD CARD                 Use SD card is selected but no SD card is mounted in the device.
                              The application is running but dockerd is stopped.
                              Insert and mount an SD card.
 6 SD CARD WRONG FS           Use SD card is selected but the mounted SD card has the wrong file system.
                              The application is running but dockerd is stopped.
                              Format the SD card with the correct file system.
 7 SD CARD WRONG PERMISSION   Use SD card is selected but the application user does not have the correct file
                              permissions to use it.
                              The application is running but dockerd is stopped.
                              Make sure no directories with the wrong user permissions are left on the
                              SD card, then restart the application.
```

### Using TLS to secure the application

When using the application with TCP socket, the application can be run in either TLS or
unsecured mode. The default selection is to use TLS mode.

#### TLS Setup

TLS requires the following keys and certificates on the device:

- Certificate Authority certificate `ca.pem`
- Server certificate `server-cert.pem`
- Private server key `server-key.pem`

For more information on how to generate these files, please consult the official
[Docker documentation][docker_protect-access].

The files can be uploaded to the device using HTTP. The request will be rejected if the file
being uploaded has the incorrect header or footer for that file type. The dockerd service will
restart, or try to start, after each successful HTTP POST request.
Uploading a new certificate will replace an already present file.

```sh
curl --anyauth -u "<user>:<password>" -F file=@<file_name> -X POST \
  http://<device-ip>/local/<application-name>/<file-name>
```

To delete any of the certificates from the device HTTP DELETE can be used. Note
that this will *not* restart dockerd.

```sh
curl --anyauth -u "<user>:<password>" -X DELETE \
  http://<device-ip>/local/<application-name>/<file-name>
```

An alternative way to upload the certificates using `scp`. This method requires an
an SSH user with write permissions to `/usr/local/packages/<application-name>/localdata`.
In this case the application needs to be restarted for these certificates to be used.

```sh
scp ca.pem server-cert.pem server-key.pem <user>@<device-ip>:/usr/local/packages/<application-name>/localdata/
```

##### Client key and certificate

When configured for TLS, the Docker daemon will listen to port 2376.
A client will need to have its own private key, together with a certificate authorized by the CA.

```sh
docker --tlsverify \
       --tlscacert=ca.pem \
       --tlscert=client-cert.pem \
       --tlskey=client-key.pem \
       --host tcp://<device-ip>:2376 \
       version
```

Instead of specifying the files with each Docker command,
Docker can be configured to use the keys and certificates from a directory of your choice
by using the `DOCKER_CERT_PATH` environment variable:

```sh
export DOCKER_CERT_PATH=<client-certificate-directory>
docker --tlsverify \
       --host tcp://<device-ip>:2376 version
```

where `<client-certificate-directory>` is the directory on your computer where the files `ca.pem`,
`client-cert.pem` and `client-key.pem` are stored.

##### Usage example without TLS

With `TCP Socket` active and `Use TLS` inactive, the Docker daemon will instead listen to port 2375.

```sh
docker --host tcp://<device-ip>:2375 version
```

### Using an SD card as storage

An SD card might be necessary to run the application correctly. Docker
containers and docker images can be quite large, and putting them on an SD card
gives more freedom in how many and how large images that can be stored.

Note that dockerd requires that Unix permissions are supported by the
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
>If this application with version before 3.0 has been used on the device with SD card as storage,
>the storage directory might already be created with root permissions.
>Since version 3.0 the application is run in rootless mode and it will then not be able
>to access that directory. To solve this, either reformat the SD card or manually
>remove the directory that is used by the application.
>For versions before 2.0 the path was `/var/spool/storage/SD_DISK/dockerd`.
>For versions from 2.0 the path is `/var/spool/storage/areas/SD_DISK/<application-name>`.

### Using the application

The application does not contain the docker client binary. This means that all
calls need to be done from a separate machine. This can be achieved by using
the `--host` flag when running the docker command and requires `TCP Socket` to be selected.

The port used will change depending on if the application runs using TLS or not.
The Docker daemon will be reachable on port 2375 when running unsecured, and on
port 2376 when running secured using TLS. Please read section
[Using TLS to secure the application](#using-tls-to-secure-the-application) for
more information.

#### Run a container

Make sure the application, using TLS, is running, then pull and run the
[hello-world][docker-hello-world] image from Docker Hub:

```sh
$ docker --tlsverify --host tcp://<device-ip>:2376 pull hello-world
Using default tag: latest
latest: Pulling from library/hello-world
70f5ac315c5a: Pull complete 
Digest: sha256:88ec0acaa3ec199d3b7eaf73588f4518c25f9d34f58ce9a0df68429c5af48e8d
Status: Downloaded newer image for hello-world:latest
docker.io/library/hello-world:latest
$ docker --tlsverify --host tcp://<device-ip>:2376 run hello-world

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
docker save <image-in-client-local-repository> | docker --tlsverify --host tcp://<device-ip>:2376 load
```

#### Using host user secondary groups in container

The application is run by a non-root user on the device. This user is set
up to be a member in a number of secondary groups as listed in the
[manifest.json](https://github.com/AxisCommunications/docker-acap/blob/main/app/manifest.json#L6-L11)
file.

When running a container, a user called `root`, (uid 0), belonging to group `root`, (gid 0),
will be the default user inside the container. It will be mapped to the non-root user on
the device, and the group will be mapped to the non-root user's primary group.
In order to get access inside the container to resources on the device that are group owned by any
of the non-root users secondary groups, these need to be added for the container user.
This can be done by using `group_add` in a docker-compose.yaml or `--group-add` if using the Docker cli.
Unfortunately, adding the name of a secondary group is not supported. Instead the *mapped* id
of the group need to be used. At the moment of writing this the mappings are:

| device group | container group id |
| ------------ | :----------------: |
| `storage`    | "1"                |

Note that the names of the groups will *not* be correctly displayed inside the container.

## Building the application

Docker can be used to build the application and output the EAP file:

```sh
docker buildx build --file Dockerfile --build-arg ARCH=<ARCH> --output <build-folder> .
```

where `<build-folder>` is the path to an output folder on your machine, eg. `build`. This will be
created for you if not already existing. Once the build has completed the EAP file can be found
in the `<build-folder>`.

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
[docker_protect-access]: https://docs.docker.com/engine/security/protect-access/
[dockerEngine]: https://docs.docker.com/engine/
[docker-hello-world]: https://hub.docker.com/_/hello-world
[docker-rootless-mode]: https://docs.docker.com/engine/security/rootless/
[latest-release]: https://github.com/AxisCommunications/docker-acap/releases/latest
[object-detector-python]: https://github.com/AxisCommunications/acap-computer-vision-sdk-examples/tree/main/object-detector-python
[product-selector]: https://www.axis.com/support/tools/product-selector
[product-selector-container]: https://www.axis.com/support/tools/product-selector/shared/%5B%7B%22index%22%3A%5B4%2C2%5D%2C%22value%22%3A%22Yes%22%7D%5D
[sd-card-standards]: https://www.sdcard.org/developers/sd-standard-overview/
[signing-documentation]: https://axiscommunications.github.io/acap-documentation/docs/faq/security.html#sign-acap-applications
[vapix]: https://www.axis.com/vapix-library/
[vapix-install]: https://www.axis.com/vapix-library/subjects/t10102231/section/t10036126/display?section=t10036126-t10010609
[vapix-allow-root]: https://www.axis.com/vapix-library/subjects/t10102231/section/t10036126/display?section=t10036126-t10185050
<!-- markdownlint-enable MD034 -->

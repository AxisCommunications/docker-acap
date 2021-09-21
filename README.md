# The Docker Engine ACAP

This is the ACAP packaging of the Docker Engine to be run on Axis devices with container support.

## Building

### armv7hf

```sh
./build.sh armv7hf
```

### aarch64

```sh
./build.sh aarch64
```

## Installing

Installation can be done in two ways. Either by using the built docker image:

```sh
docker run --rm docker-acap:1.0 <camera ip> <rootpasswd> install
```

Or by manually navigating to device GUI by browsing to the following page
(replace <axis_device_ip> with the IP number of your Axis video device)

```sh
http://<axis_device_ip>/#settings/apps
```

Go to your device web page above > Click on the tab **App** in the device GUI >
Add **(+)** sign and browse to the newly built
**Docker_Daemon_1_1_0_<arch>.eap** > Click **Install** > Run the application by
enabling the **Start** switch.

## Using the Docker ACAP

The Docker ACAP does not contain the docker client binary. This means that all
calls need to be done from a separate machine. This can be achieved by using
the -H flag when running the docker command.

The port used will change depending on if the Docker ACAP runs using TLS or not.
The Docker ACAP will be reachable on port 2375 when running unsecured, and on
port 2376 when running secured using TLS. Please read section
[Securing the Docker ACAP using TLS](#securing-the-docker-acap-using-tls) for
more information.
Below is an example of how to remotely run a docker command on a camera running
the Docker ACAP in unsecured mode:

```sh
docker -H=<axis_device_ip>:2375 version
```

See [Client keys and certificates](#client-keys-and-certificates) for an example
of how to remotely run docker commands on a camera running a secured Docker ACAP
using TLS.

## Securing the Docker ACAP using TLS

The Docker ACAP can be run either unsecured or in TLS mode. The Docker ACAP uses
TLS as default. Use the "Use TLS" dropdown in the web interface to switch
between the two different modes. It's also possible to toggle this option by
calling the parameter management API in [VAPIX](https://www.axis.com/vapix-library/)
(accessing this documentation requires creating a free account) and setting the
`root.dockerdwrapper.UseTLS` parameter to "yes" or "no".

Note that the dockerd service will be restarted every time TLS is activated or
deactivated. Running the ACAP using TLS requires some additional setup, see
[TLS Setup](#tls-setup). Running the ACAP without TLS requires no further setup.

### TLS Setup

TLS requires a few keys and certificates to work, which are listed in the
subsections below. Most of these needs to be moved to the camera. There are
mutliple ways of achieveing this, for example by using `scp` to copy the files
from a remote machine onto the camera. This can be done by running the following
command on the remote machine:

```sh
scp ca.pem server-cert.pem server-key.pem root@<axis_device_ip>:/usr/local/packages/dockerdwrapper/
```

#### The Certificate Authority (CA) certificate

This certificate needs to be present in the dockerdwrapper package folder on the
camera and be named "ca.perm". The full path of the file should be
"/usr/local/packages/dockerdwrapper/ca.pem".

#### The server certificate

This certificate needs to be present in the dockerdwrapper package folder on the
camera and be named "server-cert.perm". The full path of the file should be
"/usr/local/packages/dockerdwrapper/server-cert.pem".

#### The private server key

This key needs to be present in the dockerdwrapper package folder on the camera
and be named "server-key.perm". The full path of the file should be
"/usr/local/packages/dockerdwrapper/server-key.pem".

#### Client keys and certificates

All the clients also need to have their own private keys. Each client also needs
a certificate which has been authorized by the CA. These keys and certificates
shall be used when running docker against the dockerd daemon on the camera. See
below for an example:

```sh
docker --tlsverify \
       --tlscacert=ca.pem \
       --tlscert=client-cert.pem \
       --tlskey=client-key.pem \
       -H=<axis_device_ip>:2376 \
       version
```

## Using an SD card as storage

An SD card might be necessary to run the Docker ACAP correctly. Docker
containers and docker images can be quite large, and putting them on an SD card
gives more freedom in how many and how large images can be stored. Switching
between storage on the SD card or internal storage is done by toggling the "SD
card support" dropdown in the web interface. It's also possible to toggle this
option by calling the parameter management API in
[VAPIX](https://www.axis.com/vapix-library/) (accessing this documentation
requires creating a free account) and setting the
`root.dockerdwrapper.SDCardSupport` parameter to "yes" or "no".

Toggling this setting will automatically restart the docker daemon using the
specified storage. The default setting is to use the internal storage on the
camera.

Note that dockerdwrapper requires that Unix permissions are supported by the
file system. Examples of file systems which support this are ext4, ext3 and xfs.
It might be necessary to reformat the SD card to one of these file systems, for
example if the original file system of the SD card is vfat.

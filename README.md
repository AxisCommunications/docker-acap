# The Docker Engine ACAP

This is the ACAP packaging of the Docker Engine to be run on Axis devices with
container support. Please note that this is ACAP4 alpha and not ready for production
use. 

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
    
Or by manually navigating to device GUI by browsing to the following page (replace <axis_device_ip> with the IP number of your Axis video device)

```sh
http://<axis_device_ip>/#settings/apps
```

*Goto your device web page above > Click on the tab **App** in the device GUI > Add **(+)** sign and browse to
the newly built **Docker_Daemon_1_1_0_<arch>.eap** > Click **Install** > Run the application by enabling the **Start** switch.

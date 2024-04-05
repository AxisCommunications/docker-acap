#!/bin/bash -ex

function vapix_get {
    $curl "http://$AXIS_TARGET_IP/axis-cgi/$1" $2 $3
}

function set_param {
	 vapix_get "param.cgi?action=update&dockerdwrapper.$1=$2"
}

curl="curl -s -S -u root:pass --anyauth"

if [ -n "$1" ]; then
	vapix_get "applications/upload.cgi" -F file=@"$1"
	shift
fi

vapix_get "applications/control.cgi?action=stop&package=dockerdwrapper"
sshpass -v -p pass ssh -x -o StrictHostKeyChecking=no root@$AXIS_TARGET_IP "rm -rvf /var/spool/storage/SD_DISK/dockerd"
sshpass -v -p pass ssh -x -o StrictHostKeyChecking=no root@$AXIS_TARGET_IP "rm -rvf /var/spool/storage/areas/SD_DISK/dockerdwrapper"
vapix_get "applications/control.cgi?action=start&package=dockerdwrapper"

set_param IPCSocket no
set_param SDCardSupport yes
set_param TCPSocket yes
set_param UseTLS no
sleep 15

docker -H=$AXIS_TARGET_IP:2375 system df
docker -H=$AXIS_TARGET_IP:2375 pull hello-world
docker -H=$AXIS_TARGET_IP:2375 system df

if [ -n "$1" ]; then
	vapix_get "applications/upload.cgi" -F file=@"$1"
	shift
	vapix_get "applications/control.cgi?action=start&package=dockerdwrapper"
	sleep 15
	docker -H=$AXIS_TARGET_IP:2375 system df
fi

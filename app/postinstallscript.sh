#!/bin/sh -e

if [ ! -e /usr/bin/containerd ]; then
	logger -p user.warn "$0: Container support required to install application."
	exit 77 # EX_NOPERM
fi

# Create empty daemon.json
if [ ! -e localdata/daemon.json ]; then
	umask 077
	echo "{}" >localdata/daemon.json
fi

# Make sure containerd is started before dockerd and set PATH
cat >>/etc/systemd/system/sdkdockerdwrapper.service <<EOF
[Unit]
BindsTo=containerd.service
After=network-online.target containerd.service var-spool-storage-SD_DISK.mount
Wants=network-online.target
EOF

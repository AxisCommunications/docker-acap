#!/bin/sh

# Move the daemon.json file into localdata folder
if [ ! -e localdata/daemon.json ]; then
	mv empty_daemon.json localdata/daemon.json
else
	rm empty_daemon.json
fi

# Set extended requirements and set PATH
cat >>/etc/systemd/system/sdkdockerdwrapper.service <<EOF
[Unit]
After=network-online.target var-spool-storage-SD_DISK.mount
Wants=network-online.target
[Service]
Environment=PATH=/usr/local/packages/dockerdwrapper:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin
EOF

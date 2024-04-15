#!/bin/sh -e

if [ ! -e /usr/bin/containerd ]; then
	logger -p user.warn "$0: Container support required to install application."
	exit 77 # EX_NOPERM
fi

# Move the daemon.json file into localdata folder
if [ ! -e localdata/daemon.json ]; then
	mv empty_daemon.json localdata/daemon.json
else
	rm empty_daemon.json
fi

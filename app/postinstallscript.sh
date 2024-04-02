#!/bin/sh -e

# *** non-root user should be able to do this ****

if [ ! -e /usr/bin/containerd ]; then
	logger -p user.warn "$0: Container support required to install application."
	exit 77 # EX_NOPERM
fi

# Move the daemon.json file into localdata folder
mv -n empty_daemon.json localdata/daemon.json
rm -f empty_daemon.json

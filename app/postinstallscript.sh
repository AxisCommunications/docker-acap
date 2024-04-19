#!/bin/sh -e

if [ ! -e /usr/bin/containerd ]; then
	logger -p user.warn "$0: Container support required to install application."
	exit 77 # EX_NOPERM
fi

# Create empty daemon.json
if [ ! -e localdata/daemon.json ]; then
	umask 077
	echo "{}" >localdata/daemon.json
	[ "$(id -u)" -ne 0 ] || chown "$(stat -c %u.%g localdata)" localdata/daemon.json
fi

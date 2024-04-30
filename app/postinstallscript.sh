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

# ACAP framework does not handle ownership on SD card, which causes problem when the app user ID changes.
# If run as root, this script will repair the ownership.
SD_CARD_AREA=/var/spool/storage/SD_DISK/areas/"$(basename "$(pwd)")"
if [ "$(id -u)" -eq 0 ] && [ -d "$SD_CARD_AREA" ]; then
	chown -R "$(stat -c %u.%g localdata)" "$SD_CARD_AREA"
fi

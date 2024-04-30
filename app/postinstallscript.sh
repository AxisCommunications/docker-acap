#!/bin/sh -e

if [ ! -e /usr/bin/containerd ]; then
	logger -p user.warn "$0: Container support required to install application."
	exit 77 # EX_NOPERM
fi

UID_DOT_GID="$(stat -c %u.%g localdata)"
IS_ROOT=$([ "$(id -u)" -eq 0 ] && echo true || echo false)

# Create empty daemon.json
DAEMON_JSON=localdata/daemon.json
if [ ! -e "$DAEMON_JSON" ]; then
	umask 077
	echo "{}" >"$DAEMON_JSON"
	! $IS_ROOT || chown "$UID_DOT_GID" "$DAEMON_JSON"
fi

# ACAP framework does not handle ownership on SD card, which causes problem when
# the app user ID changes. If run as root, this script will repair the ownership.
APP_NAME="$(basename "$(pwd)")"
SD_CARD_AREA=/var/spool/storage/SD_DISK/areas/"$APP_NAME"
if $IS_ROOT && [ -d "$SD_CARD_AREA" ]; then
	chown -R "$UID_DOT_GID" "$SD_CARD_AREA"
fi

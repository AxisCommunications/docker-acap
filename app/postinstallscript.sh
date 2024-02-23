#!/bin/sh -e

if [ "$(id -un)" = "root" ]; then
    logger -p user.warn "$0: Must be run as user '$(id -un)' instead of 'root'."
    exit 77 # EX_NOPERM
fi

# *** non-root user should be able to do this ****

# Move the daemon.json file into localdata folder
mv -n empty_daemon.json localdata/daemon.json
rm -f empty_daemon.json

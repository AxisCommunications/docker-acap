#!/bin/sh -e

# *** non-root user should be able to do this ****

# Move the daemon.json file into localdata folder
mv -n empty_daemon.json localdata/daemon.json
rm -f empty_daemon.json

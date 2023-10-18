#!/bin/sh -e

# *** root user required ****
# TODO Add a check of who the user is and log warning if not root

# Get name and uid of acap user
_appname=dockerdwrapper
_appdirectory=/usr/local/packages/$_appname
_uname="$(stat -c '%U' "$_appdirectory")"
_uid="$(id "$_uname" -u)"

# Remove the user folder (this step should only be needed for cgroups v1 system)
if [ -d "/run/user/$_uid" ]; then rm -Rf "/run/user/$_uid"; fi

# Remove the service files (this step should only be needed for cgroups v2 system)
if [ -f "/etc/systemd/system/acap-user-runtime-dir@.service" ]; 
    then rm -Rf /etc/systemd/system/acap-user-runtime-dir@.service; fi
if [ -f "/etc/systemd/system/acap-user@.service" ]; 
    then rm -Rf /etc/systemd/system/acap-user@.service; fi

# Remove the subuid/subgid mappings
sed -i "/$_uname:100000:65536/d" /etc/subuid
sed -i "/$_uname:100000:65536/d" /etc/subgid

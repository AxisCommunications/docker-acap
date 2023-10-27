#!/bin/sh -e

if [ "$(id -un)" != "root" ]; then
    logger -p user.warn "$0: Must be run as 'root' instead of user '$(id -un)'."
    exit 77 # EX_NOPERM
fi

# Get name and uid of acap user
_appname=dockerdwrapper
_appdirectory=/usr/local/packages/$_appname
_uname="$(stat -c '%U' "$_appdirectory")"
_uid="$(id "$_uname" -u)"

# Remove the user folder (this step should only be needed for cgroups v1 system)
rm -Rf "/run/user/$_uid"

# Remove the service files (this step should only be needed for cgroups v2 system)
rm -Rf /etc/systemd/system/acap-user-runtime-dir@.service
rm -Rf /etc/systemd/system/acap-user@.service

# Remove the subuid/subgid mappings
sed -i "/$_uname:100000:65536/d" /etc/subuid
sed -i "/$_uname:100000:65536/d" /etc/subgid
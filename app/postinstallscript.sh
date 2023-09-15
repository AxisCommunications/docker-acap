#!/bin/sh

# Get uid of acap user
_uname=sdk
_uid="$(id $_uname -u)"

# If cgroups v2 is used we need to start user.service
if [ ! -d /sys/fs/cgroup/unified ]; then
    # Move systemd-user-runtime-dir to /usr/lib/systemd
    mv acap-user-runtime-dir@.service /etc/systemd/system/acap-user-runtime-dir@.service
    mv acap-user@.service /etc/systemd/system/acap-user@.service
    
    chown root:root /etc/systemd/system/acap-user-runtime-dir@.service
    chown root:root /etc/systemd/system/acap-user@.service

    # start user service
    systemctl daemon-reload
    systemctl set-environment XDG_RUNTIME_DIR="/run/user/$_uid"
    systemctl start acap-user@"$_uid".service
fi

# If it's cgroups v1 or if /run/user/$_uid wasn't created, let's create it
if [ ! -d /run/user/"$_uid" ]
then
    mkdir /run/user/"$_uid"
    chown $_uname /run/user/"$_uid"
fi

# Move the daemon.json file into localdata folder
if [ ! -e localdata/daemon.json ]
then
    mv empty_daemon.json localdata/daemon.json
else
    rm empty_daemon.json
fi

if [ -d "/run/docker" ]; then rm -Rf /run/docker; fi
if [ -d "/run/containerd" ]; then rm -Rf /run/containerd; fi
if [ -d "/run/xtables.lock" ]; then rm -Rf /run/xtables.lock; fi

echo "$_uname:100000:65536" > /etc/subuid
echo "$_uname:100000:65536" > /etc/subgid

# Let root own these two utilities and make the setuid
chown root:root newuidmap
chown root:root newgidmap
chmod u+s newuidmap
chmod u+s newgidmap
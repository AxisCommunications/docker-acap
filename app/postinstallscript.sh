#!/bin/sh

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

mkdir /run/user/204
chown sdk /run/user/204

echo "sdk:100000:65536" > /etc/subuid
echo "sdk:100000:65536" > /etc/subgid

# Let root own these two utilities and make the setuid
chown root:root newuidmap
chown root:root newgidmap
chmod u+s newuidmap
chmod u+s newgidmap
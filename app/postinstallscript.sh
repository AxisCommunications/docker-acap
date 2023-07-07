#!/bin/sh

# Move the daemon.json file into localdata folder
if [ ! -e localdata/daemon.json ]
then
    mv empty_daemon.json localdata/daemon.json
else
    rm empty_daemon.json
fi

# # Make sure containerd is started before dockerd and set PATH
# cat >> /etc/systemd/system/sdkdockerdwrapper.service << EOF
# [Unit]
# BindsTo=containerd.service
# After=network-online.target containerd.service var-spool-storage-SD_DISK.mount
# Wants=network-online.target
# [Service]
# Environment=PATH=/usr/local/packages/dockerdwrapper:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin
# EOF

if [ -d "/run/docker" ]; then rm -Rf /run/docker; fi
if [ -d "/run/containerd" ]; then rm -Rf /run/containerd; fi
if [ -d "/run/xtables.lock" ]; then rm -Rf /run/xtables.lock; fi

mkdir /run/user/0
chown sdk /run/user/0

echo "sdk:100000:65536" > /etc/subuid
echo "sdk:100000:65536" > /etc/subgid

mv dockerdwrapper dockerdrwapper.bak
mv dockerd-rootless-mod.sh dockerdwrapper

chown sdk dockerdwrapper



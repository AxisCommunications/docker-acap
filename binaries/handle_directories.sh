#!/bin/sh -e

# These directories will be owned by root if they exist so remove them
# so that our rootless setup works as intended
if [ -d "/run/docker" ]; then rm -Rf /run/docker; fi
if [ -d "/run/containerd" ]; then rm -Rf /run/containerd; fi
if [ -d "/run/xtables.lock" ]; then rm -Rf /run/xtables.lock; fi

# Create /usr/run/<uid> on cgroups v1 system since user service will not run there
# since script is run by root the required inputs are uid, user, group
if [ -d /sys/fs/cgroup/unified ]; then
    mkdir -p /run/user/"$1"
    chown "$2":"$3" /run/user/"$1"
fi

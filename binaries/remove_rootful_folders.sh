#!/bin/sh -e

# These folders will be owned by root if they exist so remove them
# so that our rootless setup works as intended
if [ -d "/run/docker" ]; then rm -Rf /run/docker; fi
if [ -d "/run/containerd" ]; then rm -Rf /run/containerd; fi
if [ -d "/run/xtables.lock" ]; then rm -Rf /run/xtables.lock; fi
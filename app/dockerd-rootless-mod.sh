#!/bin/sh
# dockerd-rootless-mod.sh executes dockerd in rootless mode.
#
# Usage: dockerd-rootless-mod.sh [DOCKERD_OPTIONS]
#
# External dependencies:
# * newuidmap and newgidmap needs to be installed.
# * /etc/subuid and /etc/subgid needs to be configured for the current user.
# * slirp4netns (>= v0.4.0) needs to be installed.
#
# See the documentation for the further information: https://docs.docker.com/go/rootless/

export HOME=$(pwd)
export PATH=$PATH:$(pwd):/usr/sbin
export XDG_RUNTIME_DIR=/run/user/0 
export DOCKER_HOST=unix://$XDG_RUNTIME_DIR/docker.sock

set -e -x

dockerd_options=$*

dockerd_command="/usr/local/packages/dockerdwrapper/dockerd --iptables=false -H tcp://0.0.0.0:2375 --tls=false $dockerd_options"
echo '#!/bin/sh' > dockerd_command.sh
echo $dockerd_command >> dockerd_command.sh
chmod +x dockerd_command.sh

ip_address=$(hostname -i | cut -d " " -f1)

./rootlesskit --net=slirp4netns --disable-host-loopback --copy-up=/etc --copy-up=/run --propagation=rslave \
 --port-driver slirp4netns -p "$ip_address":2375:2375/tcp dockerd_command.sh

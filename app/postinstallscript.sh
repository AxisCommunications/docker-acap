#!/bin/sh

# Make sure containerd is started before dockerd and set a PATH that includes docker-proxy
cat >> /etc/systemd/system/sdkdockerdwrapper.service << EOF
[Unit]
BindsTo=containerd.service
After=network-online.target containerd.service var-spool-storage-SD_DISK.mount
Wants=network-online.target
[Service]
Environment=PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/usr/local/packages/dockerdwrapper
EOF

mkdir -p /etc/docker
cat > /etc/docker/setup.sh << 'EOFEOF'
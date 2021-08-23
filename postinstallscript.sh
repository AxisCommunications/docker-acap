#!/bin/sh

# Make sure containerd is started before dockerd and set a PATH that includes docker-proxy
cat >> /etc/systemd/system/sdkdockerdwrapper.service << EOF
[Unit]
BindsTo=containerd.service
After=network-online.target containerd.service var-spool-storage-SD_DISK.mount
Wants=network-online.target
[Service]
Environment="HTTP_PROXY=http://wwwproxy.se.axis.com:3128"
Environment="HTTPS_PROXY=http://wwwproxy.se.axis.com:3128"
Environment="NO_PROXY=localhost,127.0.0.0/8,10.0.0.0/8,192.168.0.0/16,172.16.0.0/12,.se.axis.com"
Environment=PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/usr/local/packages/dockerdwrapper
EOF

mkdir -p /etc/docker
cat > /etc/docker/setup.sh << 'EOFEOF'
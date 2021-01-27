#!/bin/sh

# Make sure containerd is started before dockerd and set a PATH that includes docker-proxy
cat >> /etc/systemd/system/sdkrun_dockerd.service << EOF
[Unit]
BindsTo=containerd.service
After=network-online.target containerd.service var-spool-storage-SD_DISK.mount
Wants=network-online.target
[Service]
Environment=PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/usr/local/packages/run_dockerd/docker-proxy
ExecStartPre=sh /etc/docker/setup.sh
EOF

mkdir -p /etc/docker
cat > /etc/docker/setup.sh << 'EOFEOF'
#!/bin/sh
USE_SDCARD=$( parhandclient get root.Run_dockerd.SDCardSupport | grep yes )
if [ -d "/var/spool/storage/SD_DISK" ] && [ ! -z "$USE_SDCARD" ]
then
	logger sdkrun_dockerd: Configure to run from SD Card
	mkdir -p /var/spool/storage/SD_DISK/dockerd/data
	mkdir -p /var/spool/storage/SD_DISK/dockerd/exec
	cat > /etc/docker/daemon.json << EOF
{
  "data-root": "/var/spool/storage/SD_DISK/dockerd/data",
  "exec-root": "/var/spool/storage/SD_DISK/dockerd/exec"
}
EOF
else
	logger sdkrun_dockerd: Configure to run from ACAP install directory
	# Set the data-root of dockerd to be in the
	mkdir -p /usr/local/packages/run_dockerd/data
	cat > /etc/docker/daemon.json << EOF
{
  "data-root": "/usr/local/packages/run_dockerd/data"
}
EOF
fi
EOFEOF

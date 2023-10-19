#!/bin/sh -e

# *** root user required ****
# TODO Add a check of who the user is and log warning if not root

# Get name and uid of acap user and group
_appname=dockerdwrapper
_appdirectory=/usr/local/packages/$_appname
_uname="$(stat -c '%U' "$_appdirectory")"
_uid="$(id "$_uname" -u)"
_gname="$(id "$_uname" -gn)"

# If the device supports cgroups v2 we need to start the user.service
if [ ! -d /sys/fs/cgroup/unified ]; then
# Move systemd-user-runtime-dir to /usr/lib/systemd
    mv acap-user-runtime-dir@.service /etc/systemd/system/acap-user-runtime-dir@.service
    mv acap-user@.service /etc/systemd/system/acap-user@.service
    
    chown root:root /etc/systemd/system/acap-user-runtime-dir@.service
    chown root:root /etc/systemd/system/acap-user@.service

    # Update the app service file to Want acap-user@.service
    echo "[Unit]
Wants=acap-user@$_uid.service" >> /etc/systemd/system/sdkdockerdwrapper.service

fi

# Create mapping for subuid and subgid - both shall use user name!
echo "$_uname:100000:65536" > /etc/subuid
echo "$_uname:100000:65536" > /etc/subgid

# Let root own these two utilities and make the setuid
chown root:root newuidmap
chown root:root newgidmap
chmod u+s newuidmap
chmod u+s newgidmap

# Update the app service file to work for our special case
cat >> /etc/systemd/system/sdkdockerdwrapper.service << EOF
[Unit]
BindsTo=containerd.service
After=network-online.target containerd.service var-spool-storage-SD_DISK.mount
Wants=network-online.target
[Service]
Environment=PATH=/bin:/usr/bin:$_appdirectory:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin
Environment=HOME=$_appdirectory
Environment=DOCKER_HOST=unix://run/user/$_uid/docker.sock
ExecStartPre=+systemctl set-environment XDG_RUNTIME_DIR=/run/user/$_uid
ExecStartPre=+$_appdirectory/handle_directories.sh $_uid $_uname $_gname
EOF

# reload daemon for service file changes to take effect
systemctl daemon-reload
# *** non-root user should be able to do this ****

# Move the daemon.json file into localdata folder
if [ ! -e localdata/daemon.json ]
then
    mv empty_daemon.json localdata/daemon.json
else
    rm empty_daemon.json
fi



#!/bin/sh -e

if [ "$(id -un)" != "root" ]; then
    logger -p user.warn "$0: Must be run as 'root' instead of user '$(id -un)'."
    exit 77 # EX_NOPERM
fi

# Get name and uid of acap user and group
_appname=dockerdwrapper
_appdirectory=/usr/local/packages/$_appname
_uname="$(stat -c '%U' "$_appdirectory")"
_uid="$(id "$_uname" -u)"       # user id
_gid="$(id "$_uname" -g)"       # user group id
_gname="$(id "$_uname" -gn)"    # user group name
_all_gids="$(id "$_uname" -G)"  # user sub-group ids

# If the device supports cgroups v2 we need to start the user.service
if [ ! -d /sys/fs/cgroup/unified ]; then
    # Update the app service file to Require user@.service

    echo "[Unit]
After=user@$_uid.service
Requires=user@$_uid.service" >> /etc/systemd/system/sdkdockerdwrapper.service

fi

# Create mapping for subuid and subgid - both shall use user id as first value!
echo "$_uid:100000:65536" >> /etc/subuid
for sub_group_id in $_all_gids ; do
    if [ "$sub_group_id" -ne "$_gid" ]; then
        echo "$_uid:$sub_group_id:1" >> /etc/subgid
    fi
done
echo "$_uid:100000:65536" >> /etc/subgid

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
Environment=XDG_RUNTIME_DIR=/run/user/$_uid
ExecStartPre=+$_appdirectory/handle_directories.sh $_uid $_uname $_gname
EOF

# Reload daemon for service file changes to take effect
systemctl daemon-reload

# *** non-root user should be able to do this ****

# Move the daemon.json file into localdata folder
mv -n empty_daemon.json localdata/daemon.json
rm -f empty_daemon.json

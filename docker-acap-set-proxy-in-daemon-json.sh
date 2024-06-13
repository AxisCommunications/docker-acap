#!/bin/bash
[ $# -eq 3 ] || {
	printf "Error: Missing arguments\n$0"
  exit 0
}

HOSTIP=$1
SSHUSER=$2
PASS=$3
scriptfilename=docker-acap-proxy-append.sh

# Log function
logger() { printf "\n# $*\n" ; }

logger "Create temporary local script file to copy to device"
echo '#!/bin/sh

appname=dockerdwrapper
daemonfile=/usr/local/packages/$appname/localdata/daemon.json

if [ "$(grep proxies $daemonfile)" ] ;then
  printf "===>> Proxy already set in docker-acap daemon file\n"
else
  printf "===>> Set proxy in docker-acap daemon file\n"
  cat > $daemonfile <<EOF
{
  "proxies": {
    "http-proxy": "http://<myproxy>:<port>",
    "https-proxy": "http://<myproxy>:<port>",
    "no-proxy": "localhost,127.0.0.0/8,10.0.0.0/8,192.168.0.0/16,172.16.0.0/12,.<domain>"
  }
}
EOF

  echo "Content of $daemonfile:"
  cat $daemonfile
fi' > $scriptfilename
chmod +x $scriptfilename

logger "Copy script file to device /tmp folder"
sshpass -v -p $PASS scp -o StrictHostKeyChecking=no $scriptfilename acap-dockerdwrapper@$HOSTIP:/tmp/

logger "Run script file on device to append proxy, if it doesn't exist"
sshpass -v -p $PASS ssh -o StrictHostKeyChecking=no acap-dockerdwrapper@$HOSTIP "/tmp/$scriptfilename && rm /tmp/$scriptfilename"

logger "Remove local script file"
rm -f $scriptfilename

logger "Restart Docker ACAP for changes to take effect"

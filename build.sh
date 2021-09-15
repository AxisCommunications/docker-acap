#!/bin/sh
case "$1" in
    armv7hf)
       strip=arm-none-eabi-strip
       ;;
    aarch64)
       strip=aarch64-none-elf-strip
       ;;
    *)
       # error
       echo "Invalid argument '$1', valid arguments are armv7hf or aarch64"
       exit 1
       ;;
esac


dockerdtag=dockerd:1.0
imagetag=${2:-docker-acap:1.0}
dockerdname=dockerd_name

# First we build and copy out dockerd
docker build --build-arg ACAPARCH=$1 \
             --build-arg STRIP=$strip \
             --tag $dockerdtag \
             --no-cache \
             --file Dockerfile.dockerd .

docker run -v /var/run/docker.sock:/var/run/docker.sock \
           --name $dockerdname \
           -it $dockerdtag
docker cp $dockerdname:/opt/dockerd/dockerd app/

docker stop $dockerdname
docker rm $dockerdname

# Now build and copy out the acap
docker build --build-arg ACAPARCH=$1 \
             --file Dockerfile.acap \
             --no-cache \
             --tag $imagetag . 

docker cp $(docker create $imagetag):/opt/app/ ./build

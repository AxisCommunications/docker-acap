#!/bin/sh -e
case "${1:-}" in
    armv7hf|aarch64)
       ;;
    *)
       # error
       echo "Invalid argument '${1:-}', valid arguments are armv7hf or aarch64"
       exit 1
       ;;
esac

dockerdtag=dockerd:1.0
imagetag=${2:-docker-acap:1.0}
dockerdname=dockerd_name

# First we build and copy out dockerd
docker buildx build --build-arg ACAPARCH="$1" \
             --build-arg HTTP_PROXY="$HTTP_PROXY" \
             --build-arg HTTPS_PROXY="$HTTPS_PROXY" \
             --tag $dockerdtag \
             --no-cache \
             --file Dockerfile.dockerd .

docker run -v /var/run/docker.sock:/var/run/docker.sock \
           --env HTTP_PROXY="$HTTP_PROXY" \
           --env HTTPS_PROXY="$HTTPS_PROXY" \
           --name $dockerdname \
           $dockerdtag

docker cp $dockerdname:/opt/dockerd/dockerd app/

docker stop $dockerdname
docker rm $dockerdname

# Now build and copy out the acap
docker buildx build --build-arg ACAPARCH="$1" \
             --build-arg HTTP_PROXY="$HTTP_PROXY" \
             --build-arg HTTPS_PROXY="$HTTPS_PROXY" \
             --file Dockerfile.acap \
             --no-cache \
             --tag "$imagetag" . 

docker cp "$(docker create "$imagetag")":/opt/app/ ./build-"$1"

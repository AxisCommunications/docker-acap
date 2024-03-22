#!/bin/sh -eu
case "${1:-}" in
    armv7hf|aarch64)
       ;;
    *)
       # error
       echo "Invalid argument '${1:-}', valid arguments are armv7hf or aarch64"
       exit 1
       ;;
esac

imagetag="${2:-docker-acap-with-compose:1.0}"

# Now build and copy out the acap
docker buildx build --build-arg ACAPARCH="$1" \
             --build-arg HTTP_PROXY="${HTTP_PROXY:-}" \
             --build-arg HTTPS_PROXY="${HTTPS_PROXY:-}" \
             --file Dockerfile \
             --no-cache \
             --tag "$imagetag" . 

docker cp "$(docker create "$imagetag")":/opt/app/ ./build-"$1"

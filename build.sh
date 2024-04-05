#!/bin/bash -eu

imagetag="docker-acap:1.0"

function exit_with_message {
    echo "$1"
    echo
    echo "Usage: $0 ARCH [ --plain ] [ --cache ] [ --image-tag IMAGETAG ]"
    echo
    echo "ARCH must be 'armv7hf' or 'aarch64'"
    echo "--plain will simplify the Docker progress bar."
    echo "--cache will enable Docker's caching mechanism."
    echo "--image-tag sets the supplied tag of the image. Default is $imagetag."
    exit 1
}

arch="${1:-}"

case "$arch" in
armv7hf | aarch64) ;;
*) exit_with_message "Invalid architecture '$arch'" ;;
esac
shift

progress_arg=
cache_arg=--no-cache

while (($#)); do
    case "$1" in
    --plain) progress_arg="--progress plain" ;;
    --cache) cache_arg= ;;
    --image-tag)
        shift
        imagetag="$1"
        ;;
    *) exit_with_message "Invalid argument '$1'" ;;
    esac
    shift
done

# Build and copy out the acap
# shellcheck disable=SC2086
docker buildx build --build-arg ACAPARCH="$arch" \
    --build-arg HTTP_PROXY="${HTTP_PROXY:-}" \
    --build-arg HTTPS_PROXY="${HTTPS_PROXY:-}" \
    --file Dockerfile \
    $progress_arg \
    $cache_arg \
    --tag "$imagetag" \
    --output build-"$arch" .

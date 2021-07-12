if [ -z $1 ]; then
        echo "Please supply an argument, valid arguments are armv7hf or aarch64"
        exit
fi

if [ $1 != "armv7hf" ] && [ $1 != "aarch64" ]; then
        echo "Invalid argument, valid arguments are armv7hf or aarch64"
        exit
fi

dockerdtag=dockerd:1.0
imagetag=docker-acap:1.0
dockerdname=dockerd_name

# First we build and copy out dockerd
docker build --build-arg http_proxy=${HTTP_PROXY} \
             --build-arg https_proxy=${HTTPS_PROXY} \
             --build-arg ARCH=$1 \
             --tag $dockerdtag \
             --no-cache \
             --file Dockerfile.dockerd .

docker run --privileged --name $dockerdname -it $dockerdtag
docker cp $dockerdname:/opt/app/bin .
ls bin
mv bin/* app/
rm -rf bin
docker stop $dockerdname
docker rm $dockerdname

# Now build and copy out the acap
docker build --build-arg ARCH=$1 \
             --file Dockerfile.acap \
             --no-cache \
             --tag $imagetag . 
docker cp $(docker create $imagetag):/opt/app/ ./build

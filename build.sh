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

if [[ "$1" = "armv7hf" ]] ; then \
    strip=arm-none-eabi-strip ; else \
    strip=aarch64-none-elf-strip ; \
    fi

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
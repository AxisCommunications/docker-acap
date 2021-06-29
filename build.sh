imagetag=docker-acap:1.0
envname=docker-acap-env

export http_proxy=http://wwwproxy.se.axis.com:3128
export https_proxy=http://wwwproxy.se.axis.com:3128
export DOCKER_CONFIG="$WORKSPACE/.docker"
mkdir -p "$DOCKER_CONFIG"
echo '{"proxies":{ "default":{"httpProxy": "http://wwwproxy.se.axis.com:3128", "httpsProxy": "http://wwwproxy.se.axis.com:3128"}}}' > "$DOCKER_CONFIG/config.json"

docker build --build-arg http_proxy="${http_proxy}" \
             --build-arg https_proxy="${https_proxy}" \
             --tag $envname .

docker run -v /var/run/docker.sock:/var/run/docker.sock --network host --name docker_acap $envname /bin/bash ./build_acap.sh $imagetag
# docker run -v /var/run/docker.sock:/var/run/docker.sock -it --network host --name docker_acap $envname
docker cp docker_acap:/opt/app/build .
docker stop docker_acap
docker rm docker_acap
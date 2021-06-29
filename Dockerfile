ARG ARCH=armv7hf
FROM axisecp/acap-sdk:3.2-$ARCH

# Get docker to be able to build ACAP inside this container
RUN apt-get update
RUN apt-get install -y apt-transport-https \
	ca-certificates \
	curl \
	software-properties-common
RUN curl -fsSL https://download.docker.com/linux/ubuntu/gpg | apt-key add -
RUN add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu bionic stable"
RUN apt-get update && apt-get install docker-ce -y

# Get correct version of strip
RUN apt-get install binutils-arm-linux-gnueabi -y

COPY ./app /opt/app/
WORKDIR /opt/app
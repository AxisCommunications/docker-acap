include DOCKERVERSION

PROG1	= dockerdwrapper
OBJS1	= $(PROG1).c

PKGS = gio-2.0 glib-2.0 axparameter
CFLAGS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags $(PKGS))
LDLIBS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs $(PKGS))

CFLAGS += -W -Wformat=2 -Wpointer-arith -Wbad-function-cast -Wstrict-prototypes -Wmissing-prototypes -Winline -Wdisabled-optimization -Wfloat-equal -Wall -Werror

ifeq ($(ARCH),arm64)
  ACAPARCH = aarch64
  DOCKERARCH = aarch64
else ifeq ($(ARCH),arm)
  ACAPARCH = armv7hf
  DOCKERARCH = armhf
else
  $(error No Docker download path defined for ARCH "$(ARCH)")
endif

all:	$(PROG1) dockerd docker-init docker-proxy

$(PROG1): $(OBJS1)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LIBS) $(LDLIBS) -o $@

dockerd:
	ARCH=$(ARCH) $(MAKE) -f Makefile.dockerd

download_docker_binaries:
	curl https://download.docker.com/linux/static/stable/$(DOCKERARCH)/docker-$(DOCKERVERSION).tgz -o docker_binaries.tgz

docker-init: download_docker_binaries
	tar xz -f docker_binaries.tgz --strip-components=1 docker/$@ && $(STRIP) $@

docker-proxy: download_docker_binaries
	tar xz -f docker_binaries.tgz --strip-components=1 docker/$@ && $(STRIP) $@

acap: dockerd
	docker build --build-arg ACAPARCH=$(ACAPARCH) . -t axisecp/docker-acap-$(ACAPARCH):latest

clean:
	mv package.conf.orig package.conf || :
	rm -f $(PROG1) dockerd docker_binaries.tgz docker-init docker-proxy *.o *.eap

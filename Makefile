PROG1	= dockerdwrapper
OBJS1	= $(PROG1).c

PKGS = gio-2.0 glib-2.0 axhttp
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

$(PROG1): $(OBJS1)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LIBS) $(LDLIBS) -o $@

dockerd:
	ARCH=$(ARCH) $(MAKE) -f Makefile.dockerd

all:	$(PROG1) dockerd

acap: dockerd
	docker build --build-arg ACAPARCH=$(ACAPARCH) . -t axisecp/docker-acap-$(ACAPARCH):latest \
	&& docker cp $$(docker create axisecp/docker-acap-$(ACAPARCH):latest):/opt/app ./build

clean:
	mv package.conf.orig package.conf || :
	rm -f $(PROG1) dockerd *.o *.eap

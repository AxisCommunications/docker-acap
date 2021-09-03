PROG1	= dockerdwrapper
OBJS1	= $(PROG1).c
DOCKS	= dockerd docker-proxy

PROGS	= $(PROG1)

PKGS = gio-2.0 glib-2.0 axparameter
CFLAGS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags $(PKGS))
LDLIBS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs $(PKGS))

CFLAGS += -W -Wformat=2 -Wpointer-arith -Wbad-function-cast -Wstrict-prototypes -Wmissing-prototypes -Winline -Wdisabled-optimization -Wfloat-equal -Wall -Werror

ifeq ($(ARCH),arm64)
  DOCKERARCH = aarch64
else ifeq ($(ARCH),arm)
  DOCKERARCH = armhf
else
  $(error No Docker download path defined for ARCH "$(ARCH)")
endif

docker%:
	curl https://download.docker.com/linux/static/stable/$(DOCKERARCH)/docker-19.03.8.tgz | tar xz --strip-components=1 docker/$@
	$(STRIP) $@

all:	$(PROGS) $(DOCKS)

$(PROG1): $(OBJS1)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LIBS) $(LDLIBS) -o $@

clean:
	mv package.conf.orig package.conf
	rm -f $(PROGS) $(DOCKS) *.o *.eap

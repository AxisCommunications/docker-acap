PROG1	= dockerdwrapperwithcompose
OBJS1	= $(PROG1).c

PKGS = gio-2.0 glib-2.0 axparameter
DOCKS = docker dockerd docker-compose docker-init docker-proxy
CFLAGS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags $(PKGS))
LDLIBS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs $(PKGS))

FLAGS += -W -Wformat=2 -Wpointer-arith -Wbad-function-cast -Wstrict-prototypes \
		-Wmissing-prototypes -Winline -Wdisabled-optimization -Wfloat-equal -Wall -Werror \
		-Wno-unused-variable

all:	$(PROG1)

$(PROG1): $(OBJS1)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LIBS) $(LDLIBS) -o $@

clean:
	mv package.conf.orig package.conf || :
	rm -f $(PROG1) docker dockerd docker_binaries.tgz docker-compose docker-init docker-proxy *.o *.eap

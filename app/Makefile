PROG1	= dockerdwrapper
OBJS1	= $(PROG1).c storage.c

PKGS = gio-2.0 glib-2.0 axparameter axstorage
CFLAGS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags $(PKGS))
LDLIBS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs $(PKGS))

CFLAGS += -W -Wformat=2 -Wpointer-arith -Wbad-function-cast \
		-Wmissing-prototypes -Winline -Wdisabled-optimization -Wfloat-equal -Wall -Werror \
		-Wno-unused-variable

all: $(PROG1)

$(PROG1): $(OBJS1)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LIBS) $(LDLIBS) -o $@

storage.o: storage.h

clean:
	mv package.conf.orig package.conf || :
	rm -f $(PROG1) dockerd docker_binaries.tgz docker-init docker-proxy *.o *.eap

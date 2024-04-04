PROG1	= dockerdwrapper
OBJS1	= $(PROG1).o log.o sd_disk_storage.o

PKGS = gio-2.0 glib-2.0 axparameter axstorage
CFLAGS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags $(PKGS))
LDLIBS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs $(PKGS))

CFLAGS += -W -Wformat=2 -Wpointer-arith -Wbad-function-cast -Wstrict-prototypes \
		-Wmissing-prototypes -Winline -Wdisabled-optimization -Wfloat-equal -Wall -Werror \
		-Wno-unused-variable \
		-D APP_NAME=\"$(PROG1)\"

all: $(PROG1)

$(PROG1): $(OBJS1)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LIBS) $(LDLIBS) -o $@

$(PROG1).o log.o: log.h
$(PROG1).o sd_disk_storage.o: sd_disk_storage.h

clean:
	mv package.conf.orig package.conf || :
	rm -f $(PROG1) dockerd docker_binaries.tgz docker-init docker-proxy *.o *.eap

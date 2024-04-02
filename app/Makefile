PROG1	= dockerdwrapper
OBJS1	= $(PROG1).c fastcgi_cert_manager.c

PKGS = gio-2.0 glib-2.0 axparameter fcgi
CFLAGS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags $(PKGS))
LDLIBS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs $(PKGS))

# Link the built library
LDFLAGS = -L./lib -Wl,--no-as-needed,-rpath,'$$ORIGIN/lib'
CFLAGS += -I/opt/build/uriparser/build/include
LDLIBS += -luriparser

CFLAGS += -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -Werror=format-security
CFLAGS += -W -Wformat=2 -Wpointer-arith -Wbad-function-cast -Wstrict-prototypes \
		-Wmissing-prototypes -Winline -Wdisabled-optimization -Wfloat-equal -Wall -Werror \
		-Wno-unused-variable

all: $(PROG1)

$(PROG1): $(OBJS1)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LIBS) $(LDLIBS) -o $@

clean:
	mv package.conf.orig package.conf || :
	rm -f $(PROG1) dockerd docker_binaries.tgz docker-init docker-proxy *.o *.eap

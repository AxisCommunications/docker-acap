PROG1	= dockerdwrapper
OBJS1	= $(PROG1).o fcgi_server.o fcgi_write_file_from_stream.o http_request.o log.o sd_disk_storage.o tls.o

PKGS = gio-2.0 glib-2.0 axparameter axstorage fcgi
CFLAGS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags $(PKGS))
LDLIBS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs $(PKGS))

CFLAGS += -W -Wformat=2 -Wpointer-arith -Wbad-function-cast -Wstrict-prototypes \
		-Wmissing-prototypes -Winline -Wdisabled-optimization -Wfloat-equal -Wall -Werror \
		-Wno-unused-variable \
		-D APP_NAME=\"$(PROG1)\"

ifdef BUILD_WITH_SANITIZERS
    CFLAGS += -g -fsanitize=address -fsanitize=leak -fsanitize=undefined
    LDFLAGS += -static-libasan -static-liblsan -static-libubsan
endif

all: $(PROG1)

$(PROG1): $(OBJS1)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LIBS) $(LDLIBS) -o $@

$(PROG1).o tls.o: app_paths.h
$(PROG1).o fcgi_server.o: fcgi_server.h
fcgi_server.o fcgi_write_file_from_stream.o: fcgi_write_file_from_stream.h
$(PROG1).o fcgi_server.o http_request.o log.o sd_disk_storage.o tls.o: log.h
$(PROG1).o http_request.o: http_request.h
$(PROG1).o sd_disk_storage.o: sd_disk_storage.h
$(PROG1).o tls.o: tls.h

clean:
	mv package.conf.orig package.conf || :
	rm -f $(PROG1) dockerd docker_binaries.tgz docker-init docker-proxy *.o *.eap

#include "fcgiapp.h"
#include <glib.h>

enum HTTP_Request {
    POST,
    DELETE
};

typedef void *fcgi_handle;
typedef void (*fcgi_request_callback)(fcgi_handle handle,
                                      int request_method,
                                      char *cert_name,
                                      char *file_path);


int fcgi_start(fcgi_request_callback cb);
void fcgi_stop(void);
#pragma once
#include <fcgiapp.h>

typedef void (*fcgi_request_callback)(FCGX_Request* request, void* userdata);

int fcgi_start(fcgi_request_callback request_callback, void* request_callback_parameter);
void fcgi_stop(void);

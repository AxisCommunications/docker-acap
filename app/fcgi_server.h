#pragma once

typedef void (*fcgi_request_callback)(void* request_void_ptr, void* userdata);

int fcgi_start(fcgi_request_callback request_callback, void* request_callback_parameter);
void fcgi_stop(void);

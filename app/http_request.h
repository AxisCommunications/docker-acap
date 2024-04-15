#pragma once

// Callback function called from a thread by the FCGI server
void http_request_callback(void* request_void_ptr, void* restart_dockerd_void_ptr);

typedef void (*restart_dockerd_t)(void);

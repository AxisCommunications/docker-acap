#pragma once
#include <fcgiapp.h>

// Given a request with multipart/form-data, store incoming data in a file on /tmp. On success,
// return the filename and let the caller do all cleanup. On failure, log the error, clean up the
// file and return NULL.
char* fcgi_write_file_from_stream(FCGX_Request request);

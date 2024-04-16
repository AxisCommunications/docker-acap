#pragma once
#include <stdbool.h>

bool tls_missing_certs(void);
void tls_log_missing_cert_warnings(void);
const char* tls_file_description(const char* filename);
const char* tls_args_for_dockerd(void);
bool tls_file_has_correct_format(const char* filename, const char* path_to_file);

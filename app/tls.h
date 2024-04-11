#pragma once
#include <stdbool.h>

bool tls_missing_certs(void);
void tls_log_missing_cert_warnings(void);
const char* tls_args_for_dockerd(void);

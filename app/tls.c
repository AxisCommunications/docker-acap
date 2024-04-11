#include "tls.h"
#include "app_paths.h"
#include "log.h"
#include <glib.h>
#include <unistd.h>

#define TLS_CERT_PATH APP_LOCALDATA

struct cert {
    const char* dockerd_option;
    const char* filename;
    const char* description;
};

static struct cert tls_certs[] = {{"--tlscacert", "ca.pem", "CA certificate"},
                                  {"--tlscert", "server-cert.pem", "server certificate"},
                                  {"--tlskey", "server-key.pem", "server key"}};

#define NUM_TLS_CERTS (sizeof(tls_certs) / sizeof(tls_certs[0]))

static bool cert_file_exists(const struct cert* tls_cert) {
    g_autofree char* full_path = g_strdup_printf("%s/%s", TLS_CERT_PATH, tls_cert->filename);
    return access(full_path, F_OK) == 0;
}

bool tls_missing_certs(void) {
    for (size_t i = 0; i < NUM_TLS_CERTS; ++i)
        if (!cert_file_exists(&tls_certs[i]))
            return true;
    return false;
}

void tls_log_missing_cert_warnings(void) {
    for (size_t i = 0; i < NUM_TLS_CERTS; ++i)
        if (!cert_file_exists(&tls_certs[i]))
            log_warning("No %s found at %s/%s",
                        tls_certs[i].description,
                        TLS_CERT_PATH,
                        tls_certs[i].filename);
}

const char* tls_args_for_dockerd(void) {
    static char args[512];  // Too small buffer will cause truncated options, nothing more.
    const char* end = args + sizeof(args);
    char* ptr = args + g_snprintf(args, end - args, "--tlsverify");

    for (size_t i = 0; i < NUM_TLS_CERTS; ++i)
        ptr += g_snprintf(ptr,
                          end - ptr,
                          " %s %s/%s",
                          tls_certs[i].dockerd_option,
                          TLS_CERT_PATH,
                          tls_certs[i].filename);
    return args;
}

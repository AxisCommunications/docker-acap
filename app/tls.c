#include "tls.h"
#include "app_paths.h"
#include "log.h"
#include <glib.h>
#include <stdio.h>
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

#define BEGIN(x)        "-----BEGIN " x "-----\n"
#define END(x)          "-----END " x "-----\n"
#define CERTIFICATE     "CERTIFICATE"
#define PRIVATE_KEY     "PRIVATE KEY"
#define RSA_PRIVATE_KEY "RSA PRIVATE KEY"

// Filename is assumed to be one of those listed in tls_certs[].
static bool is_key_file(const char* filename) {
    return strstr(filename, "key");
}

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

const char* tls_file_description(const char* filename) {
    for (size_t i = 0; i < NUM_TLS_CERTS; ++i)
        if (strcmp(filename, tls_certs[i].filename) == 0)
            return tls_certs[i].description;
    return NULL;
}

const char* tls_file_dockerd_args(void) {
    static char args[512];  // Too small buffer will cause truncated options, nothing more.
    const char* end = args + sizeof(args);
    char* ptr = args;

    for (size_t i = 0; i < NUM_TLS_CERTS; ++i)
        ptr += g_snprintf(ptr,
                          end - ptr,
                          "%s %s/%s ",
                          tls_certs[i].dockerd_option,
                          TLS_CERT_PATH,
                          tls_certs[i].filename);
    ptr[-1] = '\0';  // Remove space after last item.
    return args;
}

static bool read_bytes_from(FILE* fp, int whence, char* buffer, int num_bytes) {
    const long offset = whence == SEEK_SET ? 0 : -num_bytes;
    if (fseek(fp, offset, whence) != 0) {
        log_error("Could not reposition stream to %s%ld: %s",
                  whence == SEEK_SET ? "SEEK_SET+" : "SEEK_END",
                  offset,
                  strerror(errno));
        return false;
    }
    if (fread(buffer, num_bytes, 1, fp) != 1) {
        log_error("Could not read %d bytes: %s", num_bytes, strerror(errno));
        return false;
    }
    return true;
}

static bool is_file_section_equal_to(FILE* fp, int whence, const char* section) {
    char buffer[128];
    int to_read = strlen(section);
    if (!read_bytes_from(fp, whence, buffer, to_read))
        return false;
    buffer[to_read] = '\0';
    return strncmp(buffer, section, to_read) == 0;
}

static bool has_header_and_footer(FILE* fp, const char* header, const char* footer) {
    return is_file_section_equal_to(fp, SEEK_SET, header) &&
           is_file_section_equal_to(fp, SEEK_END, footer);
}

bool tls_file_has_correct_format(const char* filename, const char* path_to_file) {
    FILE* fp = fopen(path_to_file, "r");
    if (!fp) {
        log_error("Could not read %s", path_to_file);
        return false;
    }

    bool correct = is_key_file(filename)
                       ? (has_header_and_footer(fp, BEGIN(PRIVATE_KEY), END(PRIVATE_KEY)) ||
                          has_header_and_footer(fp, BEGIN(RSA_PRIVATE_KEY), END(RSA_PRIVATE_KEY)))
                       : has_header_and_footer(fp, BEGIN(CERTIFICATE), END(CERTIFICATE));
    if (!correct)
        log_error("%s does not contain the headers and footers for a %s.",
                  path_to_file,
                  tls_file_description(filename));
    fclose(fp);
    return correct;
}

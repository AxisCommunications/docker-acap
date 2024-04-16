#include "http_request.h"
#include "app_paths.h"
#include "fcgi_write_file_from_stream.h"
#include "log.h"
#include "tls.h"
#include <gio/gio.h>
#include <sys/stat.h>

#define HTTP_200_OK                    "200 OK"
#define HTTP_204_NO_CONTENT            "204 No Content"
#define HTTP_400_BAD_REQUEST           "400 Bad Request"
#define HTTP_404_NOT_FOUND             "404 Not Found"
#define HTTP_405_METHOD_NOT_ALLOWED    "405 Method Not Allowed"
#define HTTP_422_UNPROCESSABLE_CONTENT "422 Unprocessable Content"
#define HTTP_500_INTERNAL_SERVER_ERROR "500 Internal Server Error"

static char* localdata_full_path(const char* filename) {
    return g_strdup_printf("%s/%s", APP_LOCALDATA, filename);
}

static bool copy_to_localdata(const char* source_path, const char* destination_filename) {
    g_autofree char* full_path = localdata_full_path(destination_filename);
    log_debug("Copying %s to %s.", source_path, full_path);

    GFile* source = g_file_new_for_path(source_path);
    GFile* destination = g_file_new_for_path(full_path);
    GError* error = NULL;
    bool success =
        g_file_copy(source, destination, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error);
    if (!success)
        log_error("Failed to copy %s to %s: %s.", source_path, full_path, error->message);
    g_object_unref(source);
    g_object_unref(destination);
    g_clear_error(&error);

    return success;
}

static bool exists_in_localdata(const char* filename) {
    g_autofree char* full_path = localdata_full_path(filename);
    struct stat sb;
    return stat(full_path, &sb) == 0;
}

static bool remove_from_localdata(const char* filename) {
    g_autofree char* full_path = localdata_full_path(filename);
    log_debug("Removing %s.", full_path);
    bool success = !unlink(full_path);
    if (!success)
        // Log as warning rather than error, since 'No such file' is also treated as a failure.
        log_warning("Failed to remove %s: %s.", filename, strerror(errno));
    return success;
}

static void
response(FCGX_Request* request, const char* status, const char* content_type, const char* body) {
    FCGX_FPrintF(request->out,
                 "Status: %s\r\n"
                 "Content-Type: %s\r\n\r\n"
                 "%s",
                 status,
                 content_type,
                 body);
}

static void response_204_no_content(FCGX_Request* request) {
    const char* status = HTTP_204_NO_CONTENT;
    log_debug("Send response %s", status);
    FCGX_FPrintF(request->out, "Status: %s\r\n\r\n", status);
}

static void response_msg(FCGX_Request* request, const char* status, const char* message) {
    log_debug("Send response %s: %s", status, message);
    g_autofree char* body = g_strdup_printf("%s\r\n", message);
    response(request, status, "text/plain", body);
}

static void post_request(FCGX_Request* request,
                         const char* filename,
                         struct restart_dockerd_context* restart_dockerd_context) {
    g_autofree char* temp_file = fcgi_write_file_from_stream(*request);
    if (!temp_file) {
        response_msg(request, HTTP_422_UNPROCESSABLE_CONTENT, "Upload to temporary file failed.");
        return;
    }
    if (!tls_file_has_correct_format(filename, temp_file)) {
        g_autofree char* msg =
            g_strdup_printf("File is not a valid %s.", tls_file_description(filename));
        response_msg(request, HTTP_400_BAD_REQUEST, msg);
    } else if (!copy_to_localdata(temp_file, filename))
        response_msg(request, HTTP_500_INTERNAL_SERVER_ERROR, "Failed to copy file to localdata");
    else {
        response_204_no_content(request);
        restart_dockerd_context->restart_dockerd(restart_dockerd_context->app_state);
    }

    if (unlink(temp_file) != 0)
        log_error("Failed to remove %s: %s", temp_file, strerror(errno));
}

static void delete_request(FCGX_Request* request, const char* filename) {
    if (!exists_in_localdata(filename))
        response_msg(request, HTTP_404_NOT_FOUND, "File not found in localdata");
    else if (!remove_from_localdata(filename))
        response_msg(request,
                     HTTP_500_INTERNAL_SERVER_ERROR,
                     "Failed to remove file from localdata");
    else
        response_204_no_content(request);
}

static void unsupported_request(FCGX_Request* request, const char* method, const char* filename) {
    log_error("Unsupported request %s %s", method, filename);
    response_msg(request, HTTP_405_METHOD_NOT_ALLOWED, "Unsupported request method");
}

static void malformed_request(FCGX_Request* request, const char* method, const char* uri) {
    log_error("Malformed request %s %s", method, uri);
    response_msg(request, HTTP_400_BAD_REQUEST, "Malformed request");
}

void http_request_callback(void* request_void_ptr, void* restart_dockerd_context_void_ptr) {
    FCGX_Request* request = (FCGX_Request*)request_void_ptr;

    const char* method = FCGX_GetParam("REQUEST_METHOD", request->envp);
    const char* uri = FCGX_GetParam("REQUEST_URI", request->envp);

    log_info("Processing HTTP request %s %s", method, uri);

    const char* filename = strrchr(uri, '/');
    if (!filename) {
        malformed_request(request, method, uri);
    } else {
        filename++;  // Strip leading '/'

        if (strcmp(method, "POST") == 0)
            post_request(request, filename, restart_dockerd_context_void_ptr);
        else if (strcmp(method, "DELETE") == 0)
            delete_request(request, filename);
        else
            unsupported_request(request, method, filename);
    }
    FCGX_Finish_r(request);
}

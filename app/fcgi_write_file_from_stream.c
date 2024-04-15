#include "fcgi_write_file_from_stream.h"
#include "fcgi_server.h"
#include "log.h"
#include <unistd.h>

static int request_content_length(const FCGX_Request* request) {
    const char* content_length_str = FCGX_GetParam("CONTENT_LENGTH", request->envp);
    if (!content_length_str)
        return 0;
    return strtol(content_length_str, NULL, 10);
}

char* fcgi_write_file_from_stream(FCGX_Request request) {
    char* temp_file = NULL;
    const int content_length = request_content_length(&request);
    const char* content_type = FCGX_GetParam("CONTENT_TYPE", request.envp);

    log_debug("Content-Type: %s", content_type);

    const char* MULTIPART_FORM_DATA = "multipart/form-data";
    if (strncmp(content_type, MULTIPART_FORM_DATA, sizeof(MULTIPART_FORM_DATA) - 1) != 0) {
        log_error("Content type \"%s\" is not supported. Use \"%s\" instead.",
                  content_type,
                  MULTIPART_FORM_DATA);
        return NULL;
    }

    const char* BOUNDARY_KEY = "boundary=";
    const char* boundary_text = strstr(content_type, BOUNDARY_KEY);
    if (!boundary_text) {
        log_error("No multipart boundary found in content-type \"%s\".", content_type);
        return NULL;
    }
    boundary_text += strlen(BOUNDARY_KEY);
    const int boundary_len = strlen(boundary_text);

    temp_file = g_strdup_printf("/tmp/fcgi_upload.XXXXXX");
    int file_des = mkstemp(temp_file);
    if (file_des == -1) {
        log_error("Failed to create %s, err %s.", temp_file, strerror(errno));
        return NULL;
    }
    log_debug("Opened %s for writing.", temp_file);

    bool remove_temp_file = true;  // Clear this to return the filename to the caller.

    const int bufferLen = 2048;
    char buffer[bufferLen + 1 /* Allow for NULL termination */];

    const char* data_start = "\r\n\r\n";
    const char* data_end = "\r\n--";

    int total_bytes_processed = 0;
    bool pre_boundary_found = false;
    bool post_boundary_found = false;

    int loop_counter = 0;  // First iteration is special.
    char* p_payload = buffer;
    char* p_payload_end = NULL;

    while (total_bytes_processed < content_length) {
        loop_counter++;
        const int available_len = bufferLen - (p_payload - buffer);

        const int bytes_read = FCGX_GetStr(p_payload, available_len, request.in);
        log_debug("FCGX_GetStr: bytes_read %d, p_payload %p(%d), available_len %d(%d)",
                  bytes_read,
                  p_payload,
                  (int)(p_payload - buffer),
                  available_len,
                  available_len - bufferLen);
        if (bytes_read < 0) {
            log_error("Failed to read from FCGI stream: %s", strerror(errno));
            break;
        }

        /* Look for pre boundary */
        if (!pre_boundary_found) {
            buffer[bytes_read] = 0; /* NULL terminate */
            p_payload = strstr(buffer + boundary_len + 1, data_start);
            if (p_payload == NULL) {
                log_error("Failed to find boundary in uploaded data.");
            }
            pre_boundary_found = true;
            p_payload += strlen(data_start);
        } else {
            log_debug("Pre boundary already found");
            p_payload = buffer;
        }

        /* Look for post boundary */
        if (!post_boundary_found) {
            char* pchar;
            for (pchar = (loop_counter == 1) ? p_payload : buffer;
                 pchar < buffer + bytes_read - ((loop_counter == 1) ? boundary_len : 0);
                 pchar++) {
                if (memcmp(pchar, boundary_text, boundary_len) == 0) {
                    log_debug("Post boundary found for %.*s", boundary_len, pchar);
                    pchar -= strlen(data_end);
                    if (memcmp(pchar, data_end, strlen(data_end)) != 0) {
                        log_error("Post boundary data end not found");
                        log_debug("Found %02x%02x%02x%02x at post boundary",
                                  pchar[0],
                                  pchar[1],
                                  pchar[2],
                                  pchar[3]);
                        goto end;
                    }
                    post_boundary_found = true;
                    break;
                }
            }
            p_payload_end = pchar;
        }

        int to_write = p_payload_end - p_payload;
        int written = 0;
        while (to_write > 0) {
            if ((written = write(file_des, p_payload + written, to_write)) < 0) {
                log_error("Failed to write %d bytes to %s: %s",
                          to_write,
                          temp_file,
                          strerror(errno));
                goto end;
            }
            total_bytes_processed += written;
            to_write -= written;
        }
        log_debug("write: p_payload %p, %d bytes", p_payload, (int)(p_payload_end - p_payload));
        log_debug("loop %d, bytes_read %d, done %d",
                  loop_counter,
                  bytes_read,
                  total_bytes_processed);

        if (post_boundary_found) {
            total_bytes_processed = content_length;
            remove_temp_file = false;  // File has been successfully received.
        } else {
            if (!pre_boundary_found) {
                log_error("No pre boundary found");
                goto end;
            }
            if (bytes_read != available_len) {
                log_error("No post boundary found");
                goto end;
            }

            /* Post boundary may have been partial at payload end. Ensure possible rematch */
            p_payload = buffer + boundary_len;
            memcpy(buffer, p_payload_end, boundary_len);
        }
    }

end:
    if (file_des != -1) {
        log_debug("Closing %s after writing %ld bytes.", temp_file, lseek(file_des, 0, SEEK_CUR));
        if (close(file_des) == -1)
            log_warning("Failed to close %s: %s", temp_file, strerror(errno));
    }
    if (remove_temp_file && temp_file) {
        if (unlink(temp_file) != 0)
            log_error("Failed to remove %s: %s", temp_file, strerror(errno));
        g_free(temp_file);
        temp_file = NULL;
    }
    return temp_file;
}

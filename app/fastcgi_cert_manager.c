#include "fastcgi_cert_manager.h"
#include <fcgi_config.h>
#include <glib-object.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include "fcgi_stdio.h"
#include "uriparser/Uri.h"

#define FCGI_SOCKET_NAME "FCGI_SOCKET_NAME"

#define syslog_v(...)                                                          \
  if (g_verbose) {                                                             \
    syslog(__VA_ARGS__);                                                       \
  }

static const char *tmp_path = "/tmp";

static fcgi_request_callback g_callback;
const char *g_socket_path = NULL;
static int g_socket = -1;
static GThread *g_thread = NULL;
static bool g_verbose = false;

static UriQueryListA *
parse_uri(const char *uriString)
{
  UriUriA uri;
  UriQueryListA *queryList;
  int itemCount;
  const char *errorPos;

  syslog(LOG_INFO, "Parsing URI: %s", uriString);

  /* Parse the URI into data structure */
  if (uriParseSingleUriA(&uri, uriString, &errorPos) != URI_SUCCESS) {
    syslog(LOG_ERR, "Failed to parse URI");
    return NULL;
  }

  /* Parse the query string into data structure */
  if (uriDissectQueryMallocA(
          &queryList, &itemCount, uri.query.first, uri.query.afterLast) !=
      URI_SUCCESS) {
    syslog(LOG_ERR, "Failed to parse query");
    uriFreeUriMembersA(&uri);
    return NULL;
  }

  uriFreeUriMembersA(&uri);
  return queryList;
}

static char *
get_value_from_query_list(const char *search_key, UriQueryListA *queryList)
{
  char *key_value = NULL;
  UriQueryListA *queryItem = queryList;

  while (queryItem) {
    if (strcmp(queryItem->key, search_key) == 0 && queryItem->value != NULL) {
      key_value = g_strdup(queryItem->value);
    }
    queryItem = queryItem->next;
  }
  return key_value;
}

static gchar *
write_file_from_stream(const char *cert_file_name,
                       int contentLength,
                       FCGX_Request request)
{
  bool success = false;
  const char *MULTIPART_FORM_DATA = "multipart/form-data";
  gchar *file_path = NULL;
  int file_des = -1;
  const char *datastart = "\r\n\r\n";
  const char *dataend = "\r\n--";
  char *contentType = FCGX_GetParam("CONTENT_TYPE", request.envp);

  syslog_v(LOG_INFO, "Content-Type: %s", contentType);
  if (!strncmp(
          contentType, MULTIPART_FORM_DATA, sizeof(MULTIPART_FORM_DATA) - 1)) {
    char *boundarytext =
        strstr(contentType, "boundary="); /* Use to find the endof input */
    boundarytext += strlen("boundary=");
    syslog_v(LOG_INFO, "Boundary text %s%s%s", "SB>", boundarytext, "<EB");

    file_path = g_strdup_printf("%s/%s.XXXXXX", tmp_path, cert_file_name);
    file_des = mkstemp(file_path);
    if (file_des == -1) {
      syslog(
          LOG_ERR, "Failed to create %s, err %s", file_path, strerror(errno));
      goto end;
    } else {
      int done = 0;
      int bufferLen = 2048;
      int pre_boundary_found = 0;
      int post_boundary_found = 0;
      char buffer[bufferLen + 1 /* Allow for NULL termination */];
      syslog(LOG_INFO, "Opened temp file for upload: %s", file_path);
      syslog_v(LOG_INFO, "bufferLen: %d", bufferLen);

      int counter = 0;
      char *p_payload = buffer;
      char *p_payload_end = NULL;
      int boundarytextLen = strlen(boundarytext);
      int dataendLen = strlen(dataend);
      while (done < contentLength) {
        counter++;
        int availableLen = bufferLen - (p_payload - buffer);

        int bytesRead = FCGX_GetStr(p_payload, availableLen, request.in);
        syslog_v(
            LOG_INFO,
            "FCGX_GetStr: bytesRead %d, p_payload %p(%d), availableLen %d(%d)",
            bytesRead,
            p_payload,
            (int)(p_payload - buffer),
            availableLen,
            availableLen - bufferLen);
        if (bytesRead < 0) {
          syslog(LOG_ERR, "Failed to read data %s", strerror(errno));
          break;
        }

        /* Look for pre boundary */
        if (pre_boundary_found == 0) {
          buffer[bytesRead] = 0; /* NULL terminate */
          p_payload = strstr(buffer + boundarytextLen + 1, datastart);
          if (p_payload == NULL) {
            syslog(LOG_ERR, "Failed to find boundary");
          }
          pre_boundary_found = 1;
          p_payload += strlen(datastart);
        } else {
          syslog_v(LOG_INFO, "Pre boundary already found");
          p_payload = buffer;
        }

        /* Look for post boundary */
        char *pchar;
        if (post_boundary_found == 0) {
          for (pchar = (counter == 1) ? p_payload : buffer;
               pchar <
               buffer + bytesRead - ((counter == 1) ? boundarytextLen : 0);
               pchar++) {
            if (memcmp(pchar, boundarytext, boundarytextLen) == 0) {
              syslog_v(LOG_INFO,
                       "Post boundary found for %.*s",
                       boundarytextLen,
                       pchar);
              pchar -= dataendLen;
              if (memcmp(pchar, dataend, dataendLen) != 0) {
                syslog(LOG_ERR, "Post boundary data end not found");
                syslog_v(LOG_INFO,
                         "Found %02x%02x%02x%02x at post boundary",
                         pchar[0],
                         pchar[1],
                         pchar[2],
                         pchar[3]);
                goto end;
              }
              post_boundary_found = 1;
              break;
            }
          }
          p_payload_end = pchar;
        }

        int towrite = p_payload_end - p_payload;
        int written = 0;
        while (towrite > 0) {
          if ((written = write(file_des, p_payload + written, towrite)) < 0) {
            syslog(LOG_ERR,
                   "Failed to write %d to %s, err %s",
                   towrite,
                   file_path,
                   strerror(errno));
            goto end;
          }
          done += written;
          towrite -= written;
        }
        syslog_v(LOG_INFO,
                 "write: p_payload %p, %d bytes",
                 p_payload,
                 (int)(p_payload_end - p_payload));
        syslog_v(LOG_INFO,
                 "counter %d, bytesRead %d, done %d",
                 counter,
                 bytesRead,
                 done);

        if (post_boundary_found) {
          done = contentLength;
          success = true;
        } else {
          if (!pre_boundary_found) {
            syslog(LOG_ERR, "No pre boundary found");
            goto end;
          }
          if (bytesRead != availableLen) {
            syslog(LOG_ERR, "No post boundary found");
            goto end;
          }

          /* Post boundary may have been partial at payload end. Ensure possible
           * rematch */
          p_payload = buffer + boundarytextLen;
          memcpy(buffer, p_payload_end, boundarytextLen);
        }
      }
    }
  } else {
    /* TODO: Handle other Content-Type's */
    syslog(LOG_INFO,
           "Content-Type not currently supported. Try \"%s\"..",
           MULTIPART_FORM_DATA);
  }

end:
  if (file_des != -1) {
    if (close(file_des) == -1) {
      syslog(LOG_ERR, "Failed to close %s, err %s", file_path, strerror(errno));
    }
  }
  if (!success && file_path) {
    /* Cleanup */
    if (g_remove(file_path) != 0) {
      syslog(
          LOG_ERR, "Failed to remove %s, err: %s", file_path, strerror(errno));
    }
    g_free(file_path);
    file_path = NULL;
  }
  return file_path;
}

static void
handle_http(void *data, __attribute__((unused)) void *userdata)
{
  FCGX_Request *request = (FCGX_Request *)data;
  char *status = "200 OK";
  char *return_message = "";
  char *method = FCGX_GetParam("REQUEST_METHOD", request->envp);
  syslog(LOG_INFO, "cert_manager.cgi: %s", method);

  if (strcmp(method, "POST") == 0 || strcmp(method, "DELETE") == 0) {
    const char *uriString = FCGX_GetParam("REQUEST_URI", request->envp);
    UriQueryListA *queryList = parse_uri(uriString);
    if (queryList == NULL) {
      status = "400 Bad Request";
      return_message = "URI could not be parsed";
      goto end;
    }

    if (strcmp(method, "POST") == 0) {
      char *cert_file_name = get_value_from_query_list("file_name", queryList);
      if (cert_file_name == NULL) {
        status = "400 Bad Request";
        return_message = "URI did not contain required key \"file_name\".";
        goto end;
      }

      if (FCGX_GetParam("CONTENT_LENGTH", request->envp) != NULL) {
        int contentLength =
            strtol(FCGX_GetParam("CONTENT_LENGTH", request->envp), NULL, 10);
        char *file_path =
            write_file_from_stream(cert_file_name, contentLength, *request);
        if (file_path == NULL) {
          status = "422 Unprocessable Content";
          return_message = "Upload of temporary cert file failed.";
          goto end;
        }
        g_callback((fcgi_handle)request, POST, cert_file_name, file_path);
        g_free(cert_file_name);
        g_free(file_path);
      }

    } else if (strcmp(method, "DELETE") == 0) {
      char *cert_file_name = get_value_from_query_list("file_name", queryList);

      if (cert_file_name == NULL) {
        status = "400 Bad Request";
        return_message = "URI did not contain required key \"file_name\".";
        goto end;
      }
      g_callback((fcgi_handle)request, DELETE, cert_file_name, NULL);
      g_free(cert_file_name);
    }
  } else if (strcmp(method, "GET") == 0) {
    /* Return usage/help */
    return_message =
        "Usage/Help\n\n"
        "Method   Action | URI | cURL\n\n"
        "GET      This Usage/Help\n"
        "         http://$DEVICE_IP/local/dockerdwrapper/cert_manager.cgi\n"
        "         curl --anyauth --user $DEVICE_USER:$DEVICE_PASSWORD -X GET "
        "\"http://$DEVICE_IP/local/dockerdwrapper/cert_manager.cgi\"\n"
        "POST     Upload TLS certificate with file_name 'file'\n"
        "         "
        "http://$DEVICE_IP/local/dockerdwrapper/"
        "cert_manager.cgi?file_name=file\n"
        "         curl --anyauth --user $DEVICE_USER:$DEVICE_PASSWORD -F "
        "file=@file_path -X POST "
        "\"http://$DEVICE_IP/local/dockerdwrapper/"
        "cert_manager.cgi?file_name=file\"\n"
        "DELETE   Remove TLS certificate with file_name 'file'\n"
        "         "
        "http://$DEVICE_IP/local/dockerdwrapper/"
        "cert_manager.cgi?file_name=file\n"
        "         curl --anyauth --user $DEVICE_USER:$DEVICE_PASSWORD -X "
        "DELETE "
        "\"http://$DEVICE_IP/local/dockerdwrapper/"
        "cert_manager.cgi?file_name=file\"\n"
        "\n";
  } else {
    status = "405 Method Not Allowed";
    return_message = "The used request message is not allowed";
  }

end:
  FCGX_FPrintF(request->out,
               "Status: %s\r\n"
               "Content-Type: text/html\r\n\r\n"
               "%s",
               status,
               return_message);
  FCGX_Finish_r(request);
}

static void *
handle_fcgi(__attribute__((unused)) void *arg)
{
  GThreadPool *workers =
      g_thread_pool_new((GFunc)handle_http, NULL, -1, FALSE, NULL);
  while (workers) {
    FCGX_Request *request = g_malloc0(sizeof(FCGX_Request));
    FCGX_InitRequest(request, g_socket, FCGI_FAIL_ACCEPT_ON_INTR);
    if (FCGX_Accept_r(request) < 0) {
      syslog(LOG_INFO, "FCGX_Accept_r: %s", strerror(errno));
      g_free(request);
      break;
    }
    g_thread_pool_push(workers, request, NULL);
  }
  syslog(LOG_INFO, "Stopping FCGI handler");
  g_thread_pool_free(workers, true, false);
  return NULL;
}

int
fcgi_start(fcgi_request_callback cb, bool verbose)
{
  openlog(NULL, LOG_PID, LOG_DAEMON);

  syslog(LOG_INFO, "Starting FCGI handler");
  g_callback = cb;
  g_verbose = verbose;
  if (g_thread) {
    return EXIT_SUCCESS; /* Already started, just update parameters */
  }

  g_socket_path = getenv(FCGI_SOCKET_NAME);
  if (!g_socket_path) {
    syslog(LOG_ERR, "Failed to get environment variable FCGI_SOCKET_NAME");
    return EXIT_FAILURE;
  }

  if (FCGX_Init() != 0) {
    syslog(LOG_ERR, "FCGX_Init failed: %s", strerror(errno));
    return EXIT_FAILURE;
  }

  if ((g_socket = FCGX_OpenSocket(g_socket_path, 5)) < 0) {
    syslog(LOG_ERR, "FCGX_OpenSocket failed: %s", strerror(errno));
    return EXIT_FAILURE;
  }
  chmod(g_socket_path, S_IRWXU | S_IRWXG | S_IRWXO);

  /* Create a thread for request handling */
  if ((g_thread = g_thread_new("fcgi_handler", &handle_fcgi, NULL)) == NULL) {
    syslog(LOG_ERR, "Failed to launch FCGI handler thread");
    return EXIT_FAILURE;
  }

  syslog(LOG_INFO, "Created handler thread");
  return EXIT_SUCCESS;
}

void
fcgi_stop(void)
{
  FCGX_ShutdownPending();

  if (g_socket != -1) {
    if (shutdown(g_socket, SHUT_RD) != 0) {
      syslog(
          LOG_WARNING, "Could not shutdown socket, err: %s", strerror(errno));
    }
    if (g_unlink(g_socket_path) != 0) {
      syslog(LOG_WARNING, "Could not unlink socket, err: %s", strerror(errno));
    }
  }
  g_thread_join(g_thread);

  closelog();
  g_socket_path = NULL;
  g_socket = -1;
  g_thread = NULL;
}

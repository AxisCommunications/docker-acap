#include "fastcgi_tls_receiver.h"
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
#include "fcgi_stdio.h"
#include "uriparser/Uri.h"

#define FCGI_SOCKET_NAME "FCGI_SOCKET_NAME"

static const char *app_localdata_path =
    "/usr/local/packages/dockerdwrapper/localdata/";
static gchar *tmp_file_dir = NULL;

static fcgi_request_callback g_callback;
const char *g_socket_path = NULL;
static int g_socket = -1;
static GThread *g_thread = NULL;

UriQueryListA *parse_uri(const char *uriString);
gchar *write_file_from_stream(int contentLength, FCGX_Request request);

UriQueryListA *
parse_uri(const char *uriString)
{
  UriUriA uri;
  UriQueryListA *queryList;
  int itemCount;
  const char *errorPos;

  syslog(LOG_INFO, "Parsing URI: %s", uriString);

  // Parse the URI into data structure
  if (uriParseSingleUriA(&uri, uriString, &errorPos) != URI_SUCCESS) {
    /* Failure (no need to call uriFreeUriMembersA) */

    syslog(LOG_ERR, "Failed to parse URI");
    return NULL;
  }
  syslog(LOG_INFO, "URI: %s", uriString);

  // Parse the query string into data structure
  if (uriDissectQueryMallocA(
          &queryList, &itemCount, uri.query.first, uri.query.afterLast) !=
      URI_SUCCESS) {
    /* Failure */
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

gchar *
write_file_from_stream(int contentLength, FCGX_Request request)
{
  gchar *file_path = NULL;
  const char *datastart = "\r\n\r\n";
  char *contentType = FCGX_GetParam("CONTENT_TYPE", request.envp);
  syslog(LOG_INFO, "ContentType %s", contentType);
  char *boundarytext =
      strstr(contentType, "boundary="); // Use to find the endof input
  boundarytext += strlen("boundary=");
  syslog(LOG_INFO, "Boundary text %s%s%s", "SB>", boundarytext, "<EB");

  // create at tmpdir for the file
  if (tmp_file_dir == NULL) {
    tmp_file_dir = g_strdup_printf("%s%s", app_localdata_path, "tmp_upload");

    if (g_mkdir_with_parents(tmp_file_dir, 740) != 0) { // TODO Check permission
      syslog(LOG_ERR,
             "Failed to create temporary directory:%s, error: %s",
             tmp_file_dir,
             strerror(errno));
      tmp_file_dir = NULL;
      goto end;
    }
    syslog(LOG_INFO, "got tmp_dir %s", tmp_file_dir);
  }

  file_path = g_strdup_printf("%s/temp_upload.txt", tmp_file_dir);

  FCGI_FILE *fileOut = FCGI_fopen(file_path, "w");
  if (!fileOut) {
    syslog(LOG_ERR, "File %s was not opened", file_path);
    goto end;
  }
  syslog(LOG_INFO, "Opened temp file for upload in %s", file_path);

  if (fileOut) {
    int done = 0;
    int chunkLen = 3072;
    int boundary_found = 0;
    int pre_boundary_found = 0;
    int post_boundary_found = 0;

    int counter = 0;
    while (done < contentLength) {
      counter++;
      char buffer[chunkLen];
      char *boundary = NULL;
      char *p_payload = NULL;
      char *p_payload_end = NULL;
      char *pchar = NULL;
      int packetRead;
      packetRead = FCGX_GetStr(buffer, sizeof(buffer), request.in);

      if (packetRead < 0) {
        syslog(LOG_ERR, "failed to read data %s", strerror(errno));
        break;
      }

      // Look for boundary
      if (pre_boundary_found == 0) {
        p_payload = strstr(buffer + strlen(boundarytext) + 1, datastart);
        if (p_payload == NULL) {
          syslog(LOG_ERR, "Failed to find boundary");
        }
        pre_boundary_found = 1;
        p_payload += strlen(datastart);
      } else {
        syslog(LOG_INFO, "no pre boundary");
        p_payload = buffer;
      }
      if (post_boundary_found == 0) {
        for (pchar = p_payload;
             pchar < buffer + contentLength - strlen(boundarytext);
             pchar++) {
          if (memcmp(pchar, boundarytext, strlen(boundarytext)) == 0) {
            post_boundary_found = 1;
            syslog(LOG_INFO, "End boundary found for %s", pchar);
            break;
          }
        }
        p_payload_end = pchar;
      }

      // TODO Remove this before merge
      syslog(LOG_DEBUG, "About to write %s", p_payload);
      syslog(LOG_DEBUG, "Stopping at %s", p_payload_end);

      FCGI_fwrite(p_payload, 1, p_payload_end - p_payload, fileOut);

      if (post_boundary_found) {
        done = contentLength;
      } else {
        done += packetRead;
      }
      syslog(LOG_INFO, "Done is %d", done);
    }

    FCGI_fclose(fileOut);
  }
  end:
  return file_path;
}

static void
handle_http(void *data, __attribute__((unused)) void *userdata)
{
  syslog(LOG_INFO, "Entered handle_http");
  FCGX_Request *request = (FCGX_Request *)data;
  char *status = "200 OK";
  char *return_message = "";

  char *method = FCGX_GetParam("REQUEST_METHOD", request->envp);
  if (strcmp(method, "GET") == 0 || strcmp(method, "POST") == 0 ||
      strcmp(method, "DELETE") == 0) {
    const char *uriString = FCGX_GetParam("REQUEST_URI", request->envp);
    UriQueryListA *queryList = parse_uri(uriString);
    if (queryList == NULL) {
      status = "400 Bad Request";
      return_message = "URI could not be parsed";
      goto end;
    }

    if (strcmp(method, "POST") == 0) {
      // get name of file for upload
      char *cert_file_name = get_value_from_query_list("file_name", queryList);

      if (cert_file_name == NULL) {
        status = "400 Bad Request";
        return_message = "URI did not contain required key \"file_name\".";
        goto end;
      }

      if (FCGX_GetParam("CONTENT_LENGTH", request->envp) != NULL) {
        int contentLength =
            strtol(FCGX_GetParam("CONTENT_LENGTH", request->envp), NULL, 10);
        syslog(LOG_INFO, "Got content length %d", contentLength);

        char *file_path = write_file_from_stream(contentLength, *request);
        if (file_path == NULL) {
          status = "422 Unprocessable Content";
          return_message = "Upload of temporary cert file failed.";
          goto end;
        }
        g_callback((fcgi_handle)request, POST, cert_file_name, file_path);
      }

    } else if (strcmp(method, "DELETE") == 0) {
      char *cert_file_name = get_value_from_query_list("file_name", queryList);

      if (cert_file_name == NULL) {
        status = "400 Bad Request";
        return_message = "URI did not contain required key \"file_name\".";
        goto end;
      }
      g_callback((fcgi_handle)request, DELETE, cert_file_name, NULL);
    }
  } else {
    status = "405 Method Not Allowed";
    return_message = "The used request message is not allowed";
  }
end:
  FCGX_FPrintF(request->out,
               "Status: %s\r\n"
               "Content-type: text/html\r\n\r\n"
               "%s",
               status,
               return_message);
  FCGX_Finish_r(request);
}

static void *
handle_fcgi(__attribute__((unused)) void *arg)
{
  syslog(LOG_INFO, "Entered handle_fcgi");
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
fcgi_start(fcgi_request_callback cb)
{
  openlog(NULL, LOG_PID, LOG_DAEMON);

  syslog(LOG_INFO, "Starting FCGI handler");
  g_callback = cb;

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

  // Create a thread for request handling
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

  if (tmp_file_dir != NULL) {
    // TODO actually remove the dir as well
    g_free(tmp_file_dir);
  }

  if (g_socket != -1) {
    shutdown(g_socket, SHUT_RD);
    g_unlink(g_socket_path);
  }
  g_thread_join(g_thread);
}
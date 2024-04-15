#include "fcgi_server.h"
#include "log.h"
#include <fcgi_config.h>
#include <fcgi_stdio.h>
#include <glib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sysexits.h>
#include <unistd.h>

#define FCGI_SOCKET_NAME "FCGI_SOCKET_NAME"

static const char* g_socket_path = NULL;
static int g_socket = -1;
static GThread* g_thread = NULL;

static void* handle_fcgi(void* request_callback) {
    GThreadPool* workers = g_thread_pool_new((GFunc)request_callback, NULL, -1, FALSE, NULL);
    while (workers) {
        FCGX_Request* request = g_malloc0(sizeof(FCGX_Request));
        FCGX_InitRequest(request, g_socket, FCGI_FAIL_ACCEPT_ON_INTR);
        if (FCGX_Accept_r(request) < 0) {
            log_info("FCGX_Accept_r: %s", strerror(errno));
            g_free(request);
            break;
        }
        g_thread_pool_push(workers, request, NULL);
    }
    log_info("Stopping FCGI server");
    g_thread_pool_free(workers, true, false);
    return NULL;
}

int fcgi_start(fcgi_request_callback request_callback) {
    log_debug("Starting FCGI server");

    g_socket_path = getenv(FCGI_SOCKET_NAME);
    if (!g_socket_path) {
        log_error("Failed to get environment variable FCGI_SOCKET_NAME");
        return EX_SOFTWARE;
    }

    if (FCGX_Init() != 0) {
        log_error("FCGX_Init failed: %s", strerror(errno));
        return EX_SOFTWARE;
    }

    if ((g_socket = FCGX_OpenSocket(g_socket_path, 5)) < 0) {
        log_error("FCGX_OpenSocket failed: %s", strerror(errno));
        return EX_SOFTWARE;
    }
    chmod(g_socket_path, S_IRWXU | S_IRWXG | S_IRWXO);

    /* Create a thread for request handling */
    if ((g_thread = g_thread_new("fcgi_server", &handle_fcgi, request_callback)) == NULL) {
        log_error("Failed to launch FCGI server thread");
        return EX_SOFTWARE;
    }

    log_debug("Launched FCGI server thread.");
    return EX_OK;
}

void fcgi_stop(void) {
    log_debug("Stopping FCGI server.");
    FCGX_ShutdownPending();

    if (g_socket != -1) {
        log_debug("Closing and removing FCGI socket.");
        if (shutdown(g_socket, SHUT_RD) != 0) {
            log_warning("Could not shutdown socket, err: %s", strerror(errno));
        }
        if (unlink(g_socket_path) != 0) {
            log_warning("Could not unlink socket, err: %s", strerror(errno));
        }
    }
    log_debug("Joining FCGI server thread.");
    g_thread_join(g_thread);

    g_socket_path = NULL;
    g_socket = -1;
    g_thread = NULL;
    log_debug("FCGI server has stopped.");
}

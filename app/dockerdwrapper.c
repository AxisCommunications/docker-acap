/**
 * Copyright (C) 2021, Axis Communications AB, Lund, Sweden
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

#define _GNU_SOURCE  // For sigabbrev_np()
#include "app_paths.h"
#include "fcgi_server.h"
#include "http_request.h"
#include "log.h"
#include "sd_disk_storage.h"
#include "tls.h"
#include <arpa/inet.h>
#include <axsdk/axparameter.h>
#include <errno.h>
#include <glib.h>
#include <mntent.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sysexits.h>
#include <unistd.h>

#define PARAM_APPLICATION_LOG_LEVEL "ApplicationLogLevel"
#define PARAM_DOCKERD_LOG_LEVEL     "DockerdLogLevel"
#define PARAM_IPC_SOCKET            "IPCSocket"
#define PARAM_SD_CARD_SUPPORT       "SDCardSupport"
#define PARAM_TCP_SOCKET            "TCPSocket"
#define PARAM_USE_TLS               "UseTLS"
#define PARAM_STATUS                "Status"

typedef enum {
    STATUS_NOT_STARTED = 0,  // Index in the array, not the actual status code
    STATUS_RUNNING,
    STATUS_DOCKERD_STOPPED,
    STATUS_DOCKERD_RUNTIME_ERROR,
    STATUS_TLS_CERT_MISSING,
    STATUS_NO_SOCKET,
    STATUS_NO_SD_CARD,
    STATUS_SD_CARD_WRONG_FS,
    STATUS_SD_CARD_WRONG_PERMISSION,
    STATUS_CODE_COUNT,
} status_code_t;

static const char* const status_code_strs[STATUS_CODE_COUNT] = {"-1 NOT STARTED",
                                                                "0 RUNNING",
                                                                "1 DOCKERD STOPPED",
                                                                "2 DOCKERD RUNTIME ERROR",
                                                                "3 TLS CERT MISSING",
                                                                "4 NO SOCKET",
                                                                "5 NO SD CARD",
                                                                "6 SD CARD WRONG FS",
                                                                "7 SD CARD WRONG PERMISSION"};

struct settings {
    char* data_root;
    bool use_tls;
    bool use_tcp_socket;
    bool use_ipc_socket;
};

struct app_state {
    volatile int allow_dockerd_to_start_atomic;
    char* sd_card_area;
    AXParameter* param_handle;
};

static bool dockerd_allowed_to_start(const struct app_state* app_state) {
    return g_atomic_int_get(&app_state->allow_dockerd_to_start_atomic);
}

static void allow_dockerd_to_start(struct app_state* app_state, bool new_value) {
    g_atomic_int_set(&app_state->allow_dockerd_to_start_atomic, new_value);
}

// If process exited by a signal, code will be -1.
// If process exited with an exit code, signal will be 0.
struct exit_cause {
    int code;
    int signal;
};

// Loop run on the main process
static GMainLoop* loop = NULL;

// Exit code of this program. Set using 'quit_program()'.
#define EX_KEEP_RUNNING -1
static int application_exit_code = EX_KEEP_RUNNING;

static pid_t rootlesskit_pid = 0;

static const char* params_that_restart_dockerd[] = {PARAM_APPLICATION_LOG_LEVEL,
                                                    PARAM_DOCKERD_LOG_LEVEL,
                                                    PARAM_IPC_SOCKET,
                                                    PARAM_SD_CARD_SUPPORT,
                                                    PARAM_TCP_SOCKET,
                                                    PARAM_USE_TLS,
                                                    NULL};

#define main_loop_run()                                        \
    do {                                                       \
        log_debug("g_main_loop_run called by %s", __func__);   \
        g_main_loop_run(loop);                                 \
        log_debug("g_main_loop_run returned by %s", __func__); \
    } while (0)

#define main_loop_quit()                                      \
    do {                                                      \
        log_debug("g_main_loop_quit called by %s", __func__); \
        g_main_loop_quit(loop);                               \
    } while (0)

#define main_loop_unref()                                      \
    do {                                                       \
        log_debug("g_main_loop_unref called by %s", __func__); \
        g_main_loop_unref(loop);                               \
    } while (0)

static void quit_program(int exit_code) {
    application_exit_code = exit_code;
    main_loop_quit();
}

static bool with_compose(void) {
    return strcmp(APP_NAME, "dockerdwrapperwithcompose") == 0;
}

static char* xdg_runtime_directory(void) {
    return g_strdup_printf("/var/run/user/%d", getuid());
}

static char* xdg_runtime_file(const char* filename) {
    g_autofree char* xdg_runtime_dir = xdg_runtime_directory();
    return g_strdup_printf("%s/%s", xdg_runtime_dir, filename);
}

static void remove_docker_pid_file(void) {
    g_autofree char* pid_path = xdg_runtime_file("docker.pid");
    unlink(pid_path);
}

static bool set_xdg_directory_permisssions(mode_t mode) {
    g_autofree char* xdg_runtime_dir = xdg_runtime_directory();
    if (chmod(xdg_runtime_dir, mode) != 0) {
        log_error("Failed to set permissions on %s: %s", xdg_runtime_dir, strerror(errno));
        return false;
    }
    return true;
}

static bool let_other_apps_use_our_ipc_socket(void) {
    const mode_t group_read_and_exec_perms = 0750;
    return set_xdg_directory_permisssions(group_read_and_exec_perms);
}

static bool prevent_others_from_using_our_ipc_socket(void) {
    const mode_t user_read_and_exec_perms = 0700;
    return set_xdg_directory_permisssions(user_read_and_exec_perms);
}

/**
 * @brief Signals handling
 *
 * @param signal_num Signal number.
 */
static void handle_signals(__attribute__((unused)) int signal_num) {
    switch (signal_num) {
        case SIGINT:
        case SIGTERM:
        case SIGQUIT:
            quit_program(EX_OK);
    }
}

/**
 * @brief Initialize signals
 */
static void init_signals(void) {
    struct sigaction sa;

    sa.sa_flags = 0;

    sigemptyset(&sa.sa_mask);
    sa.sa_handler = handle_signals;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
}

/**
 * @brief Checks if the given process is alive.
 *
 * @return True if alive. False if dead or exited.
 */
static bool is_process_alive(int pid) {
    int status;
    pid_t return_pid = waitpid(pid, &status, WNOHANG);
    if (return_pid == -1) {
        // Report errors as dead.
        return false;
    } else if (return_pid == rootlesskit_pid) {
        // Child is already exited, so not alive.
        return false;
    }
    return true;
}

static bool
set_parameter_value(AXParameter* param_handle, const char* parameter_name, const char* value) {
    log_debug("About to set %s to %s", parameter_name, value);
    GError* error = NULL;
    bool res = ax_parameter_set(param_handle, parameter_name, value, true, &error);
    if (!res) {
        log_error("Failed to write parameter value of %s to %s. Error: %s",
                  parameter_name,
                  value,
                  error->message);
    }
    g_clear_error(&error);
    return res;
}

static void set_status_parameter(AXParameter* param_handle, status_code_t status) {
    set_parameter_value(param_handle, PARAM_STATUS, status_code_strs[status]);
}

/**
 * @brief Fetch the value of the parameter as a string
 *
 * @return The value of the parameter as string if successful, NULL otherwise
 */
static char* get_parameter_value(AXParameter* param_handle, const char* parameter_name) {
    GError* error = NULL;
    char* parameter_value = NULL;

    if (!ax_parameter_get(param_handle, parameter_name, &parameter_value, &error)) {
        log_error("Failed to fetch parameter value of %s. Error: %s",
                  parameter_name,
                  error->message);

        free(parameter_value);
        parameter_value = NULL;
    }

    g_clear_error(&error);
    return parameter_value;
}

/**
 * @brief Retrieve the file system type of the device containing this path.
 *
 * @return The file system type as a string (ext4/ext3/vfat etc...) if
 * successful, NULL otherwise.
 */
static char* get_filesystem_of_path(const char* path) {
    char buf[PATH_MAX];
    struct stat sd_card_stat;
    int stat_result = stat(path, &sd_card_stat);
    if (stat_result != 0) {
        log_error("Cannot store data on the SD card, no storage exists at %s", path);
        return NULL;
    }

    FILE* fp;
    dev_t dev;

    dev = sd_card_stat.st_dev;

    if ((fp = setmntent("/proc/mounts", "r")) == NULL) {
        return NULL;
    }

    struct mntent mnt;
    while (getmntent_r(fp, &mnt, buf, PATH_MAX)) {
        if (stat(mnt.mnt_dir, &sd_card_stat) != 0) {
            continue;
        }

        if (sd_card_stat.st_dev == dev) {
            endmntent(fp);
            char* return_value = strdup(mnt.mnt_type);
            return return_value;
        }
    }

    endmntent(fp);

    // Should never reach here.
    errno = EINVAL;
    return NULL;
}

// Set up the SD card. Call set_status_parameter() and return false on error.
static bool setup_sdcard(AXParameter* param_handle, const char* data_root) {
    g_autofree char* sd_file_system = NULL;
    g_autofree char* create_droot_command = g_strdup_printf("mkdir -p %s", data_root);

    int res = system(create_droot_command);
    if (res != 0) {
        log_error("Failed to create data_root folder at: %s. Error code: %d", data_root, res);
        set_status_parameter(param_handle, STATUS_SD_CARD_WRONG_PERMISSION);
        return false;
    }

    // Confirm that the SD card is usable
    sd_file_system = get_filesystem_of_path(data_root);
    if (sd_file_system == NULL) {
        log_error("Couldn't identify the file system of the SD card at %s", data_root);
        set_status_parameter(param_handle, STATUS_NO_SD_CARD);
        return false;
    }

    if (strcmp(sd_file_system, "vfat") == 0 || strcmp(sd_file_system, "exfat") == 0) {
        log_error(
            "The SD card at %s uses file system %s which does not support "
            "Unix file permissions. Please reformat to a file system that "
            "support Unix file permissions, such as ext4 or xfs.",
            data_root,
            sd_file_system);
        set_status_parameter(param_handle, STATUS_SD_CARD_WRONG_FS);
        return false;
    }

    if (access(data_root, F_OK) == 0 && access(data_root, W_OK) != 0) {
        log_error(
            "The application user does not have write permissions to the SD "
            "card directory at %s. Please change the directory permissions or "
            "remove the directory.",
            data_root);
        set_status_parameter(param_handle, STATUS_SD_CARD_WRONG_PERMISSION);
        return false;
    }

    return true;
}

static bool
is_parameter_equal_to(AXParameter* param_handle, const char* name, const char* value_to_equal) {
    g_autofree char* value = get_parameter_value(param_handle, name);
    return value && strcmp(value, value_to_equal) == 0;
}

// A parameter of type "bool:no,yes" is guaranteed to contain one of those
// strings, but user code is still needed to interpret it as a Boolean type.
static bool is_parameter_yes(AXParameter* param_handle, const char* name) {
    return is_parameter_equal_to(param_handle, name, "yes");
}

static bool is_app_log_level_debug(AXParameter* param_handle) {
    return is_parameter_equal_to(param_handle, PARAM_APPLICATION_LOG_LEVEL, "debug");
}

// Return data root matching the current SDCardSupport selection.
// Call set_status_parameter() and return NULL on error.
//
// If SDCardSupport is "yes", data root will be located on the proved SD card
// area. Passing NULL as SD card area signals that the SD card is not available.
static char* prepare_data_root(AXParameter* param_handle, const char* sd_card_area) {
    if (is_parameter_yes(param_handle, PARAM_SD_CARD_SUPPORT)) {
        if (!sd_card_area) {
            log_warning("SD card was requested, but no SD card is available at the moment.");
            set_status_parameter(param_handle, STATUS_NO_SD_CARD);
            return NULL;
        }
        char* data_root = g_strdup_printf("%s/data", sd_card_area);
        if (!setup_sdcard(param_handle, data_root)) {
            free(data_root);
            return NULL;
        }
        return data_root;
    } else {
        return g_strdup_printf("%s/data", APP_LOCALDATA);  // Use app-localdata if no SD Card
    }
}

// Read UseTLS parameter and verify that TLS files are present. Call set_status_parameter() and
// return false on error.
static gboolean get_and_verify_tls_selection(AXParameter* param_handle, bool* use_tls_ret) {
    const bool use_tls = is_parameter_yes(param_handle, PARAM_USE_TLS);

    if (use_tls && tls_missing_certs()) {
        tls_log_missing_cert_warnings();
        set_status_parameter(param_handle, STATUS_TLS_CERT_MISSING);
        return false;
    }

    *use_tls_ret = use_tls;
    return true;
}

// Read and verify consistency of settings. Call set_status_parameter() or quit_program() and return
// false on error.
static bool read_settings(struct settings* settings, const struct app_state* app_state) {
    AXParameter* param_handle = app_state->param_handle;
    settings->use_tcp_socket = is_parameter_yes(param_handle, PARAM_TCP_SOCKET);

    if (!settings->use_tcp_socket)
        // Even if the user has selected UseTLS we do not need to check the certs
        // when TCP won't be used. If the setting is changed we will loop through
        // this function again.
        settings->use_tls = false;
    else if (!get_and_verify_tls_selection(param_handle, &settings->use_tls))
        return false;

    settings->use_ipc_socket = is_parameter_yes(param_handle, PARAM_IPC_SOCKET);

    if (!settings->use_ipc_socket && !settings->use_tcp_socket) {
        log_error(
            "At least one of IPC socket or TCP socket must be set to \"yes\". "
            "dockerd will not be started.");
        set_status_parameter(param_handle, STATUS_NO_SOCKET);
        return false;
    }

    if (settings->use_ipc_socket && with_compose() && !let_other_apps_use_our_ipc_socket()) {
        quit_program(EX_SOFTWARE);
        return false;
    }

    if (!(settings->data_root = prepare_data_root(param_handle, app_state->sd_card_area)))
        return false;

    return true;
}

static struct exit_cause child_process_exit_cause(int status, GError** error) {
    struct exit_cause result;
    result.code = -1;
    result.signal = 0;

    if (g_spawn_check_wait_status(status, error) || (*error)->domain == G_SPAWN_EXIT_ERROR)
        result.code = *error ? (*error)->code : 0;
    else if ((*error)->domain == G_SPAWN_ERROR && (*error)->code == G_SPAWN_ERROR_FAILED)
        result.signal = status;

    return result;
}

static void log_child_process_exit_cause(const char* name, GPid pid, int status) {
    GError* error = NULL;
    struct exit_cause exit_cause = child_process_exit_cause(status, &error);

    char msg[128];
    const char* end = msg + sizeof(msg);
    char* ptr = msg + g_snprintf(msg, end - msg, "Child process %s (%d)", name, pid);
    if (exit_cause.code >= 0)
        g_snprintf(ptr, end - ptr, " exited with exit code %d", exit_cause.code);
    else if (exit_cause.signal > 0)
        g_snprintf(ptr, end - ptr, " was killed by signal %d", exit_cause.signal);
    else
        g_snprintf(ptr, end - ptr, " terminated in an unexpected way: %s", error->message);
    g_clear_error(&error);
    log_debug("%s", msg);
}

static bool child_process_exited_with_error(int status) {
    GError* error = NULL;
    struct exit_cause exit_cause = child_process_exit_cause(status, &error);
    g_clear_error(&error);
    return exit_cause.code > 0;
}

static void
check_child_process_exit_code_and_clean_up(GPid pid, gint status, gpointer app_state_void_ptr) {
    log_child_process_exit_cause("rootlesskit", pid, status);

    struct app_state* app_state = app_state_void_ptr;

    bool runtime_error = child_process_exited_with_error(status);
    allow_dockerd_to_start(app_state, !runtime_error);
    status_code_t s = runtime_error ? STATUS_DOCKERD_RUNTIME_ERROR : STATUS_DOCKERD_STOPPED;
    set_status_parameter(app_state->param_handle, s);

    rootlesskit_pid = 0;
    g_spawn_close_pid(pid);

    remove_docker_pid_file();  // Might have been left behind if dockerd crashed.

    prevent_others_from_using_our_ipc_socket();

    main_loop_quit();  // Trigger a restart of dockerd from main()
}

// Return a command line with space-delimited argument based on the current settings.
static const char* build_daemon_args(const struct settings* settings, AXParameter* param_handle) {
    static gchar args[1024];  // Pointer to args returned to caller on success.
    const char* args_end = args + sizeof(args);
    char* args_wr = args;  // Points to location of next write

    const char* data_root = settings->data_root;
    const bool use_tls = settings->use_tls;
    const bool use_tcp_socket = settings->use_tcp_socket;
    const bool use_ipc_socket = settings->use_ipc_socket;

    gsize msg_len = 128;
    gchar msg[msg_len];

    g_autofree char* log_level = get_parameter_value(param_handle, PARAM_DOCKERD_LOG_LEVEL);

    // get host ip
    char host_buffer[256];
    char* IPbuffer;
    struct hostent* host_entry;
    gethostname(host_buffer, sizeof(host_buffer));
    host_entry = gethostbyname(host_buffer);
    IPbuffer = inet_ntoa(*((struct in_addr*)host_entry->h_addr_list[0]));

    // construct the rootlesskit command
    args_wr += g_snprintf(args_wr,
                          args_end - args_wr,
                          "%s %s %s %s %s %s %s %s %s",
                          "rootlesskit",
                          "--subid-source=static",
                          "--net=slirp4netns",
                          "--disable-host-loopback",
                          "--copy-up=/etc",
                          "--copy-up=/run",
                          "--propagation=rslave",
                          "--port-driver slirp4netns",
                          /* don't use same range as company proxy */
                          "--cidr=10.0.3.0/24");

    if (strcmp(log_level, "debug") == 0) {
        args_wr += g_snprintf(args_wr, args_end - args_wr, " %s", "--debug");
    }

    const uint port = use_tls ? 2376 : 2375;
    args_wr += g_snprintf(args_wr, args_end - args_wr, " -p %s:%d:%d/tcp", IPbuffer, port, port);

    // add dockerd command
    args_wr += g_snprintf(args_wr,
                          args_end - args_wr,
                          " dockerd %s",
                          "--config-file " APP_LOCALDATA "/" DAEMON_JSON);

    g_strlcpy(msg, "Starting dockerd", msg_len);

    args_wr += g_snprintf(args_wr, args_end - args_wr, " --log-level=%s", log_level);

    if (use_ipc_socket) {
        g_strlcat(msg, " with IPC socket and", msg_len);
        // The socket should reside in the user directory and have same group as user.
        // If omitted, dockerd will log a warning about the 'docker' group not being find.
        // However, rootlesskit maps the user's primary group to the root group, so "--group 0"
        // means the socket will belong to the user's primary group.
        g_autofree char* ipc_socket = xdg_runtime_file("docker.sock");
        args_wr += g_snprintf(args_wr, args_end - args_wr, " --group 0 -H unix://%s", ipc_socket);
    } else {
        g_strlcat(msg, " without IPC socket and", msg_len);
    }

    if (use_tcp_socket) {
        g_strlcat(msg, " with TCP socket", msg_len);
        g_strlcat(msg, use_tls ? " in TLS mode" : " in unsecured mode", msg_len);
        const uint port = use_tls ? 2376 : 2375;
        args_wr += g_snprintf(args_wr, args_end - args_wr, " -H tcp://0.0.0.0:%d", port);
        const char* tls_arg = use_tls ? "--tlsverify=true" : "--tls=false";
        args_wr += g_snprintf(args_wr, args_end - args_wr, " %s", tls_arg);
        if (use_tls)
            args_wr += g_snprintf(args_wr, args_end - args_wr, " %s", tls_file_dockerd_args());
    } else {
        g_strlcat(msg, " without TCP socket", msg_len);
    }

    g_autofree char* data_root_msg = g_strdup_printf(" using %s as storage.", data_root);
    g_strlcat(msg, data_root_msg, msg_len);
    args_wr += g_snprintf(args_wr, args_end - args_wr, " --data-root %s", data_root);

    log_info("%s", msg);
    return args;
}

// Start dockerd. On success, call set_status_parameter(STATUS_RUNNING) and on error,
// call set_status_parameter(STATUS_NOT_STARTED).
static bool start_dockerd(const struct settings* settings, struct app_state* app_state) {
    AXParameter* param_handle = app_state->param_handle;
    GError* error = NULL;
    bool result = false;
    bool return_value = false;

    const char* args = build_daemon_args(settings, param_handle);

    log_debug("Sending daemon start command: %s", args);
    char** args_split = g_strsplit(args, " ", 0);
    result = g_spawn_async(NULL,
                           args_split,
                           NULL,
                           G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                           NULL,
                           NULL,
                           &rootlesskit_pid,
                           &error);
    if (!result) {
        log_error("Starting dockerd failed: execv returned: %d, error: %s", result, error->message);
        set_status_parameter(param_handle, STATUS_NOT_STARTED);
        goto end;
    }
    log_debug("Child process rootlesskit (%d) was started.", rootlesskit_pid);

    g_child_watch_add(rootlesskit_pid, check_child_process_exit_code_and_clean_up, app_state);

    set_status_parameter(param_handle, STATUS_RUNNING);
    return_value = true;

end:
    g_strfreev(args_split);
    g_clear_error(&error);
    return return_value;
}

static void read_settings_and_start_dockerd(struct app_state* app_state) {
    struct settings settings = {0};

    if (read_settings(&settings, app_state))
        start_dockerd(&settings, app_state);

    free(settings.data_root);
}

static bool send_signal(const char* name, GPid pid, int sig) {
    log_debug("Sending SIG%s to %s (%d)", sigabbrev_np(sig), name, pid);
    if (kill(pid, sig) != 0) {
        log_error("Failed to send %s to %s (%d)", sigdescr_np(sig), name, pid);
        return FALSE;
    }
    return TRUE;
}

// Check if dockerd is still running. Launch this function using g_timeout_add_seconds() and pass a
// pointer to a counter starting at 1. When dockerd has terminated, the counter will be set to zero.
// Otherwise, it will be increased, and SIGTERM will be sent on the 20th call.
static gboolean monitor_dockerd_termination(void* time_since_sigterm_void_ptr) {
    // dockerd usually sends SIGTERM to containers after 10 s, so we must wait a bit longer.
    const int time_to_wait_before_sigkill = 20;
    int* time_since_sigterm = (int*)time_since_sigterm_void_ptr;
    if (!rootlesskit_pid) {
        log_debug("rootlesskit exited after %d s", *time_since_sigterm);
        *time_since_sigterm = 0;  // Tell caller that timer has ended.
        g_main_loop_quit(loop);   // Release caller from its main loop.
        return FALSE;             // Tell GLib that timer shall end.
    } else {
        log_debug("rootlesskit (%d) still running %d s after SIGTERM",
                  rootlesskit_pid,
                  *time_since_sigterm);
        (*time_since_sigterm)++;
        if (*time_since_sigterm > time_to_wait_before_sigkill)
            // Send SIGKILL but still wait for the process exit callback to clear the pid variable.
            send_signal("rootlesskit", rootlesskit_pid, SIGKILL);
        return TRUE;  // Tell GLib to call timer again.
    }
}

// Send SIGTERM to dockerd, wait for it to terminate.
// Send SIGKILL if that fails, but still wait for it to terminate.
static void stop_dockerd(void) {
    if (!is_process_alive(rootlesskit_pid))
        return;

    send_signal("rootlesskit", rootlesskit_pid, SIGTERM);

    int time_since_sigterm = 1;
    g_timeout_add_seconds(1, monitor_dockerd_termination, &time_since_sigterm);
    while (time_since_sigterm != 0) {  // Loop until the timer callback has stopped running
        g_main_loop_run(loop);
    }
    log_info("Stopped dockerd.");
}

// Meant to be used as a one-shot call from g_timeout_add_seconds()
static gboolean quit_main_loop(void*) {
    main_loop_quit();
    return FALSE;
}

// Meant to be used as an AXParameter callback
static void restart_dockerd_when_parameter_changed(const gchar* name,
                                                   const gchar* value,
                                                   gpointer app_state_void_ptr) {
    const gchar* parname = name += strlen("root." APP_NAME ".");

    log_info("%s changed to %s", parname, value);

    struct app_state* app_state = app_state_void_ptr;

    // If dockerd has failed before, this parameter change may have resolved the problem.
    allow_dockerd_to_start(app_state, true);

    // Trigger a restart of dockerd from main(), but delay it 1 second.
    // When there are multiple AXParameter callbacks in a queue, such as
    // during the first parameter change after installation, any parameter
    // usage, even outside a callback, will cause a 20 second deadlock per
    // queued callback.
    g_timeout_add_seconds(1, quit_main_loop, NULL);
}

static AXParameter* setup_axparameter(struct app_state* app_state) {
    bool success = false;
    GError* error = NULL;
    AXParameter* ax_parameter = ax_parameter_new(APP_NAME, &error);
    if (ax_parameter == NULL) {
        log_error("Error when creating AXParameter: %s", error->message);
        goto end;
    }

    for (const char** param = params_that_restart_dockerd; *param; param++) {
        if (!ax_parameter_register_callback(ax_parameter,
                                            *param,
                                            restart_dockerd_when_parameter_changed,
                                            app_state,
                                            &error)) {
            log_error("Could not register %s callback. Error: %s", *param, error->message);
            goto end;
        }
    }

    success = true;

end:
    g_clear_error(&error);
    if (!success && ax_parameter != NULL) {
        ax_parameter_free(ax_parameter);
        ax_parameter = NULL;
    }
    return ax_parameter;
}

static void sd_card_callback(const char* sd_card_area, void* app_state_void_ptr) {
    struct app_state* app_state = app_state_void_ptr;
    const bool using_sd_card = is_parameter_yes(app_state->param_handle, PARAM_SD_CARD_SUPPORT);
    if (using_sd_card && !sd_card_area) {
        stop_dockerd();  // Block here until dockerd has stopped using the SD card.
        set_status_parameter(app_state->param_handle, STATUS_NO_SD_CARD);
    }
    app_state->sd_card_area = sd_card_area ? strdup(sd_card_area) : NULL;
    if (using_sd_card)
        main_loop_quit();  // Trigger a restart of dockerd from main()
}

static void restart_dockerd_after_file_upload(struct app_state* app_state) {
    // If dockerd has failed before, this file upload may have resolved the problem.
    allow_dockerd_to_start(app_state, true);

    main_loop_quit();
}

// Stop the application and start it from an SSH prompt with
// $ ./dockerdwrapper --stdout
// in order to get log messages written to console rather than to syslog.
static void parse_command_line(int argc, char** argv, struct log_settings* log_settings) {
    log_settings->destination =
        (argc == 2 && strcmp(argv[1], "--stdout") == 0) ? log_dest_stdout : log_dest_syslog;
}

static bool set_env_variable(const char* env_var, const char* value) {
    log_debug("Setting env: %s=%s", env_var, value);
    if (setenv(env_var, value, 1) != 0) {
        log_error("Error setting env variable %s to %s", env_var, value);
        return false;
    }
    return true;
}

static bool set_env_variables(void) {
    uid_t uid = getuid();
    g_autofree char* path =
        g_strdup_printf("/bin:/usr/bin:%s:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin",
                        APP_DIRECTORY);
    g_autofree char* xdg_runtime_dir = xdg_runtime_directory();

    return set_env_variable("PATH", path) && set_env_variable("HOME", APP_DIRECTORY) &&
           set_env_variable("XDG_RUNTIME_DIR", xdg_runtime_dir);
}

int main(int argc, char** argv) {
    struct app_state app_state = {0};
    struct log_settings log_settings = {0};

    loop = g_main_loop_new(NULL, FALSE);

    parse_command_line(argc, argv, &log_settings);
    log_init(&log_settings);

    allow_dockerd_to_start(&app_state, true);

    app_state.param_handle = setup_axparameter(&app_state);
    if (!app_state.param_handle) {
        log_error("Error in setup_axparameter");
        return EX_SOFTWARE;
    }

    log_debug_set(is_app_log_level_debug(app_state.param_handle));

    if (!set_env_variables()) {
        log_error("Failed to set environment variables");
        return EX_SOFTWARE;
    }

    init_signals();

    struct restart_dockerd_context restart_dockerd_context;
    restart_dockerd_context.restart_dockerd = restart_dockerd_after_file_upload;
    restart_dockerd_context.app_state = &app_state;
    int fcgi_error = fcgi_start(http_request_callback, &restart_dockerd_context);
    if (fcgi_error)
        return fcgi_error;

    struct sd_disk_storage* sd_disk_storage = sd_disk_storage_init(sd_card_callback, &app_state);

    while (application_exit_code == EX_KEEP_RUNNING) {
        if (!rootlesskit_pid && dockerd_allowed_to_start(&app_state))
            read_settings_and_start_dockerd(&app_state);

        main_loop_run();

        log_debug_set(is_app_log_level_debug(app_state.param_handle));

        stop_dockerd();
    }

    sd_disk_storage_free(sd_disk_storage);

    fcgi_stop();

    set_status_parameter(app_state.param_handle, STATUS_NOT_STARTED);
    ax_parameter_free(app_state.param_handle);

    free(app_state.sd_card_area);

    main_loop_unref();

    log_debug("Application exited with exit code %d", application_exit_code);
    return application_exit_code;
}

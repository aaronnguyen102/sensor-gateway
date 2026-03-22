/**
 * @file    main.c
 * @brief   Entry point for the Sensor Gateway application.
 * @author  Nguyen Si Phu
 *
 * Orchestrates the init/shutdown sequence for all subsystems.
 * No business logic here — each module is self-contained.
 *
 * Init order  (dependency-driven):  Log → Storage → API → TCP
 * Shutdown order (reverse / LIFO):  TCP → API → Storage → Log
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "connection_manager.h"
#include "storage_manager.h"
#include "log_process.h"
#include "api_server.h"

/* Shared shutdown flag — read by all threads, written only by signal handler.
 * volatile: prevents compiler from caching the value in a register.
 * sig_atomic_t: POSIX guarantees atomic read/write in signal context. */
volatile sig_atomic_t g_running   = 1;

/* Listening socket fd — file-scoped so the signal handler can close it. */
static int            g_server_fd = -1;

/**
 * Signal handler for SIGINT / SIGTERM.
 *
 * Only performs async-signal-safe operations:
 *   - Writes to a volatile sig_atomic_t flag.
 *   - Calls close() on the listening socket.
 *
 * Closing the socket unblocks accept() in the main thread.
 * We intentionally do NOT set SA_RESTART so that accept() returns
 * EINTR as a secondary wake-up mechanism (defense in depth).
 */
static void shutdown_handler(int sig)
{
    (void)sig;
    g_running = 0;

    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;   /* Guard against double-close */
    }
}

int main(void)
{
    /* ── Register signal handlers ─────────────────────────── */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = shutdown_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;              /* No SA_RESTART — let accept() return EINTR */

    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("[Main] sigaction");
        return EXIT_FAILURE;
    }
    sigaction(SIGTERM, &sa, NULL); /* Also handle 'kill <pid>' / systemd stop */

    /* ── Init sequence ────────────────────────────────────── */

    if (log_init() < 0) {
        fprintf(stderr, "[Main] Failed to initialize log.\n");
        return EXIT_FAILURE;
    }

    log_write(LOG_INFO, "Main", "Sensor Gateway starting...");

    if (storage_init() < 0) {
        log_write(LOG_ERROR, "Main", "Failed to initialize storage.");
        log_close();
        return EXIT_FAILURE;
    }

    if (api_server_start() < 0) {
        log_write(LOG_ERROR, "Main", "Failed to start API server.");
        storage_close();
        log_close();
        return EXIT_FAILURE;
    }

    g_server_fd = start_server();
    if (g_server_fd < 0) {
        log_write(LOG_ERROR, "Main", "Failed to start TCP server.");
        api_server_stop();
        storage_close();
        log_close();
        return EXIT_FAILURE;
    }

    log_write(LOG_INFO, "Main",
              "Gateway ready. TCP port %d, HTTP API port %d.",
              SERVER_PORT, API_PORT);

    /* Blocks until g_running becomes 0 (signal received) */
    accept_loop(g_server_fd);

    /* ── Shutdown sequence (reverse of init) ──────────────── */
    log_write(LOG_INFO, "Main", "Shutting down...");

    stop_server(g_server_fd);     /* Wait for client threads to finish    */
    api_server_stop();            /* Stop HTTP before closing DB          */
    storage_close();              /* Flush & close SQLite                 */

    log_write(LOG_INFO, "Main", "Gateway stopped cleanly.");
    log_close();                  /* Flush & close log file last          */

    return EXIT_SUCCESS;
}

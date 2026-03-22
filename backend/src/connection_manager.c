/**
 * @file    connection_manager.c
 * @brief   TCP server: thread-per-client model with graceful shutdown.
 * @author  Nguyen Si Phu
 *
 * Each client connection is handled by a detached thread that reads
 * fixed-size binary packets, validates them, and stores valid readings.
 *
 * Shutdown strategy:
 *   - Signal handler sets g_running = 0 and closes the listening socket.
 *   - accept() unblocks via EINTR or EBADF.
 *   - Client threads wake up when SO_RCVTIMEO expires (2s) and check g_running.
 *   - stop_server() polls the atomic client count with a 5s timeout.
 */

#include "connection_manager.h"
#include "data_manager.h"
#include "storage_manager.h"
#include "log_process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>

extern volatile sig_atomic_t g_running;

static atomic_int g_client_count = 0;

/* ── Per-client thread ────────────────────────────────────── */

static void *handle_client(void *arg)
{
    ClientInfo *client = (ClientInfo *)arg;

    log_write(LOG_INFO, "ConnMgr",
              "Thread %lu: sensor connected %s:%d (fd=%d)",
              (unsigned long)pthread_self(),
              client->ip, client->port, client->fd);

    /* 2s receive timeout — allows periodic g_running checks during shutdown. */
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    if (setsockopt(client->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        log_write(LOG_WARNING, "ConnMgr",
                  "[%s] setsockopt(SO_RCVTIMEO) failed: %s — continuing without timeout",
                  client->ip, strerror(errno));
    }

    uint8_t buf[PACKET_SIZE];
    ssize_t total, n;
    n = 0;

    while (g_running) {
        total = 0;

        /* Accumulate exactly PACKET_SIZE bytes (may arrive in fragments). */
        while (total < PACKET_SIZE) {
            n = recv(client->fd, buf + total, PACKET_SIZE - total, 0);

            if (n > 0) { total += n; continue; }
            if (n == 0) goto done;                         /* Clean disconnect */

            /* n < 0 */
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (!g_running) goto done;
                continue;                                  /* Timeout — retry  */
            }
            if (errno == EINTR) {
                if (!g_running) goto done;
                continue;                                  /* Signal — retry   */
            }
            goto done;                                     /* Real error       */
        }

        /* Full packet received — validate and store */
        const SensorPacket *pkt = (const SensorPacket *)buf;
        if (validate_packet(pkt)) {
            uint16_t sid = ntohs(pkt->sensor_id);
            if (storage_insert_reading(sid, pkt->temperature,
                                       pkt->humidity, pkt->pressure) == 0) {
                log_write(LOG_DEBUG, "ConnMgr",
                          "[%s] sensor_id=%u T=%.2f°C H=%.1f%% P=%.1fhPa → DB OK",
                          client->ip, sid, pkt->temperature,
                          pkt->humidity, pkt->pressure);
            } else {
                log_write(LOG_ERROR, "ConnMgr",
                          "[%s] sensor_id=%u → DB FAIL", client->ip, sid);
            }
        } else {
            log_write(LOG_WARNING, "ConnMgr",
                      "[%s] Invalid packet — skipped.", client->ip);
        }
    }

done:
    if (n == 0) {
        log_write(LOG_INFO, "ConnMgr",
                  "Sensor %s:%d disconnected (clean).",
                  client->ip, client->port);
    } else if (n < 0 && errno != EINTR && errno != EAGAIN) {
        log_write(LOG_ERROR, "ConnMgr",
                  "recv() error on %s:%d: %s",
                  client->ip, client->port, strerror(errno));
    } else {
        log_write(LOG_INFO, "ConnMgr",
                  "Sensor %s:%d — thread exiting (shutdown).",
                  client->ip, client->port);
    }

    close(client->fd);
    atomic_fetch_sub(&g_client_count, 1);
    log_write(LOG_INFO, "ConnMgr",
              "Active clients: %d/%d",
              atomic_load(&g_client_count), MAX_CLIENTS);

    free(client);
    return NULL;
}

/* ── Server setup ─────────────────────────────────────────── */

int start_server(void)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        log_write(LOG_ERROR, "ConnMgr", "socket() failed: %s", strerror(errno));
        return -1;
    }

    /* Allow immediate port reuse after restart. */
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_write(LOG_ERROR, "ConnMgr", "setsockopt() failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_write(LOG_ERROR, "ConnMgr", "bind() failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, BACKLOG) < 0) {
        log_write(LOG_ERROR, "ConnMgr", "listen() failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    log_write(LOG_INFO, "ConnMgr",
              "Listening on port %d (max %d clients, fd=%d)",
              SERVER_PORT, MAX_CLIENTS, server_fd);
    return server_fd;
}

/* ── Accept loop ──────────────────────────────────────────── */

void accept_loop(int server_fd)
{
    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR || errno == EBADF) break;   /* Shutdown signal */
            log_write(LOG_ERROR, "ConnMgr", "accept() failed: %s", strerror(errno));
            continue;
        }

        if (atomic_load(&g_client_count) >= MAX_CLIENTS) {
            log_write(LOG_WARNING, "ConnMgr",
                      "Max clients (%d) reached. Rejecting %s.",
                      MAX_CLIENTS, inet_ntoa(client_addr.sin_addr));
            close(client_fd);
            continue;
        }

        ClientInfo *client = malloc(sizeof(ClientInfo));
        if (!client) {
            log_write(LOG_ERROR, "ConnMgr", "malloc() failed: %s", strerror(errno));
            close(client_fd);
            continue;
        }

        client->fd   = client_fd;
        client->port = ntohs(client_addr.sin_port);
        inet_ntop(AF_INET, &client_addr.sin_addr, client->ip, INET_ADDRSTRLEN);

        atomic_fetch_add(&g_client_count, 1);
        log_write(LOG_INFO, "ConnMgr",
                  "New connection from %s:%d (active: %d/%d)",
                  client->ip, client->port,
                  atomic_load(&g_client_count), MAX_CLIENTS);

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, client) != 0) {
            log_write(LOG_ERROR, "ConnMgr",
                      "pthread_create() failed: %s", strerror(errno));
            close(client_fd);
            free(client);
            atomic_fetch_sub(&g_client_count, 1);
            continue;
        }
        pthread_detach(tid);
    }
}

/* ── Shutdown ─────────────────────────────────────────────── */

void stop_server(int server_fd)
{
    if (server_fd >= 0) {
        close(server_fd);
        log_write(LOG_INFO, "ConnMgr",
                  "Server socket closed (fd=%d). Remaining clients: %d",
                  server_fd, atomic_load(&g_client_count));
    }

    /* Poll until all client threads exit or 5s timeout. */
    if (atomic_load(&g_client_count) > 0) {
        log_write(LOG_INFO, "ConnMgr",
                  "Waiting for %d client thread(s) to finish...",
                  atomic_load(&g_client_count));

        int wait_ms = 0;
        const int max_wait_ms = 5000;
        while (atomic_load(&g_client_count) > 0 && wait_ms < max_wait_ms) {
            usleep(100000);   /* 100 ms */
            wait_ms += 100;
        }

        int remaining = atomic_load(&g_client_count);
        if (remaining > 0) {
            log_write(LOG_WARNING, "ConnMgr",
                      "Timeout: %d client thread(s) still running — proceeding.",
                      remaining);
        } else {
            log_write(LOG_INFO, "ConnMgr", "All client threads finished.");
        }
    }
}

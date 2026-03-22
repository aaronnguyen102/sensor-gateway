/**
 * @file    connection_manager.h
 * @brief   TCP server — accept connections, spawn per-client threads.
 * @author  Nguyen Si Phu
 */

#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include <stdint.h>
#include <stdatomic.h>
#include <netinet/in.h>     /* INET_ADDRSTRLEN */

#define MAX_CLIENTS     10
#define SERVER_PORT     8888
#define BACKLOG         5

/**
 * Per-client context, heap-allocated before pthread_create().
 * The spawned thread takes ownership and frees it on exit.
 */
typedef struct {
    int      fd;
    char     ip[INET_ADDRSTRLEN];
    uint16_t port;
} ClientInfo;

/**
 * Create the TCP listening socket: socket → bind → listen.
 * @return server_fd (>= 0) on success, -1 on failure.
 */
int  start_server(void);

/**
 * Accept loop — blocks until g_running becomes 0.
 * Each accepted connection spawns a detached thread.
 */
void accept_loop(int server_fd);

/**
 * Close the server socket and wait for active client threads to finish
 * (up to a 5-second timeout).
 */
void stop_server(int server_fd);

#endif /* CONNECTION_MANAGER_H */

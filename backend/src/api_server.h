/**
 * @file    api_server.h
 * @brief   REST API layer using libmicrohttpd.
 * @author  Nguyen Si Phu
 *
 * Provides HTTP endpoints for the frontend to query sensor data.
 * libmicrohttpd manages its own internal thread pool — no manual
 * thread management needed here.
 *
 * Endpoints:
 *   GET /api/sensors            — list of unique sensor IDs
 *   GET /api/sensors/{id}/data  — readings for a sensor (default limit=50)
 *
 * Lifecycle:
 *   Call api_server_start() AFTER  storage_init().
 *   Call api_server_stop()  BEFORE storage_close().
 */

#ifndef API_SERVER_H
#define API_SERVER_H

/* Port 8080: non-privileged (no root required), standard dev convention. */
#define API_PORT  8080

/**
 * Start the HTTP daemon on API_PORT.
 * @return 0 on success, -1 on failure.
 */
int api_server_start(void);

/**
 * Stop the HTTP daemon and wait for in-flight requests to complete.
 */
void api_server_stop(void);

#endif /* API_SERVER_H */

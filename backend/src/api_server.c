/**
 * @file    api_server.c
 * @brief   REST API implementation using libmicrohttpd.
 * @author  Nguyen Si Phu
 */

#include "api_server.h"
#include "storage_manager.h"
#include "log_process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microhttpd.h>

/* ── Private state ────────────────────────────────────────── */

static struct MHD_Daemon *g_httpd = NULL;

/* ── Helpers ──────────────────────────────────────────────── */

static enum MHD_Result
send_json_response(struct MHD_Connection *conn,
                   unsigned int status_code,
                   const char *json)
{
    struct MHD_Response *response;
    enum MHD_Result ret;

    response = MHD_create_response_from_buffer(
        strlen(json), (void *)json, MHD_RESPMEM_MUST_COPY);
    if (!response) return MHD_NO;

    MHD_add_response_header(response, "Content-Type", "application/json");
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");

    ret = MHD_queue_response(conn, status_code, response);
    MHD_destroy_response(response);
    return ret;
}

static enum MHD_Result
send_not_found(struct MHD_Connection *conn)
{
    return send_json_response(conn, MHD_HTTP_NOT_FOUND,
                              "{\"error\":\"Not found\"}");
}

/* ── Endpoint handlers ────────────────────────────────────── */

/** GET /api/sensors — return list of unique sensor IDs. */
static enum MHD_Result
handle_get_sensors(struct MHD_Connection *conn)
{
    char **sensors = NULL;
    int count = 0;

    if (storage_get_sensors(&sensors, &count) < 0) {
        return send_json_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                                  "{\"error\":\"Database query failed\"}");
    }

    char json[4096];
    int offset = 0;

    offset += snprintf(json + offset, sizeof(json) - offset,
                       "{\"sensors\":[");
    for (int i = 0; i < count; i++) {
        offset += snprintf(json + offset, sizeof(json) - offset,
                           "%s\"%s\"", (i > 0) ? "," : "", sensors[i]);
        free(sensors[i]);
    }
    free(sensors);

    snprintf(json + offset, sizeof(json) - offset, "]}");

    log_write(LOG_DEBUG, "API", "GET /api/sensors → %d sensors", count);
    return send_json_response(conn, MHD_HTTP_OK, json);
}

/** GET /api/sensors/{id}/data?limit=N — return sensor readings as JSON. */
static enum MHD_Result
handle_get_sensor_data(struct MHD_Connection *conn, const char *sensor_id)
{
    const char *limit_str = MHD_lookup_connection_value(
        conn, MHD_GET_ARGUMENT_KIND, "limit");

    int limit = 50;
    if (limit_str) {
        int parsed = atoi(limit_str);
        if (parsed > 0 && parsed <= 500) limit = parsed;
    }

    SensorReading *readings = NULL;
    int count = 0;

    if (storage_get_readings(sensor_id, limit, &readings, &count) < 0) {
        return send_json_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                                  "{\"error\":\"Database query failed\"}");
    }

    /* Dynamic buffer: ~150 bytes per reading (3 floats + timestamp + JSON). */
    size_t buf_size = (size_t)count * 150 + 256;
    char *json = malloc(buf_size);
    if (!json) {
        free(readings);
        return send_json_response(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                                  "{\"error\":\"Out of memory\"}");
    }

    int offset = 0;
    offset += snprintf(json + offset, buf_size - offset,
                       "{\"sensor_id\":\"%s\",\"readings\":[", sensor_id);

    for (int i = 0; i < count; i++) {
        offset += snprintf(json + offset, buf_size - offset,
                           "%s{\"timestamp\":\"%s\","
                           "\"temperature\":%.2f,"
                           "\"humidity\":%.2f,"
                           "\"pressure\":%.2f}",
                           (i > 0) ? "," : "",
                           readings[i].timestamp,
                           readings[i].temperature,
                           readings[i].humidity,
                           readings[i].pressure);
    }

    snprintf(json + offset, buf_size - offset, "]}");

    log_write(LOG_DEBUG, "API",
              "GET /api/sensors/%s/data?limit=%d → %d readings",
              sensor_id, limit, count);

    enum MHD_Result ret = send_json_response(conn, MHD_HTTP_OK, json);
    free(json);
    free(readings);
    return ret;
}

/* ── Request router ───────────────────────────────────────── */

static enum MHD_Result
answer_request(void *cls,
               struct MHD_Connection *connection,
               const char *url,
               const char *method,
               const char *version,
               const char *upload_data,
               size_t *upload_data_size,
               void **con_cls)
{
    (void)cls; (void)version; (void)upload_data;
    (void)upload_data_size; (void)con_cls;

    if (strcmp(method, "GET") != 0) {
        return send_json_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED,
                                  "{\"error\":\"Only GET supported\"}");
    }

    /* Route: /api/sensors */
    if (strcmp(url, "/api/sensors") == 0) {
        return handle_get_sensors(connection);
    }

    /* Route: /api/sensors/{id}/data */
    const char *prefix = "/api/sensors/";
    size_t prefix_len  = strlen(prefix);

    if (strncmp(url, prefix, prefix_len) == 0) {
        const char *id_start = url + prefix_len;
        const char *slash    = strchr(id_start, '/');

        if (slash && strcmp(slash, "/data") == 0) {
            size_t id_len = (size_t)(slash - id_start);
            if (id_len > 0 && id_len < 32) {
                char sensor_id[32];
                strncpy(sensor_id, id_start, id_len);
                sensor_id[id_len] = '\0';
                return handle_get_sensor_data(connection, sensor_id);
            }
        }
    }

    log_write(LOG_DEBUG, "API", "404 Not Found: %s %s", method, url);
    return send_not_found(connection);
}

/* ── Public API ───────────────────────────────────────────── */

int api_server_start(void)
{
    g_httpd = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD,
        API_PORT,
        NULL, NULL,
        &answer_request, NULL,
        MHD_OPTION_END
    );

    if (!g_httpd) {
        log_write(LOG_ERROR, "API",
                  "Failed to start HTTP server on port %d", API_PORT);
        return -1;
    }

    log_write(LOG_INFO, "API",
              "HTTP API server listening on port %d", API_PORT);
    return 0;
}

void api_server_stop(void)
{
    if (g_httpd) {
        MHD_stop_daemon(g_httpd);
        g_httpd = NULL;
        log_write(LOG_INFO, "API", "HTTP API server stopped.");
    }
}

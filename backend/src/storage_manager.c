/**
 * @file    storage_manager.c
 * @brief   SQLite storage layer with prepared statements and WAL mode.
 * @author  Nguyen Si Phu
 */

#include "storage_manager.h"
#include "log_process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sqlite3.h>

/* ── Private state ────────────────────────────────────────── */

static sqlite3          *g_db                = NULL;
static sqlite3_stmt     *g_stmt_insert       = NULL;
static sqlite3_stmt     *g_stmt_get_sensors  = NULL;
static sqlite3_stmt     *g_stmt_get_readings = NULL;
static pthread_mutex_t   g_db_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── SQL constants ────────────────────────────────────────── */

static const char *SQL_CREATE_TABLE =
    "CREATE TABLE IF NOT EXISTS readings ("
    "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  sensor_id   TEXT    NOT NULL,"
    "  timestamp   DATETIME DEFAULT CURRENT_TIMESTAMP,"
    "  temperature REAL    NOT NULL,"
    "  humidity    REAL    NOT NULL,"
    "  pressure    REAL    NOT NULL"
    ");";

static const char *SQL_INSERT =
    "INSERT INTO readings (sensor_id, temperature, humidity, pressure) "
    "VALUES (?, ?, ?, ?);";

static const char *SQL_GET_SENSORS =
    "SELECT DISTINCT sensor_id FROM readings ORDER BY sensor_id;";

/* Subquery: fetch N newest (DESC), then reverse to ASC for Chart.js. */
static const char *SQL_GET_READINGS =
    "SELECT timestamp, temperature, humidity, pressure FROM ("
    "  SELECT timestamp, temperature, humidity, pressure FROM readings"
    "  WHERE sensor_id = ?"
    "  ORDER BY timestamp DESC"
    "  LIMIT ?"
    ") ORDER BY timestamp ASC;";

/* ── Init / Close ─────────────────────────────────────────── */

int storage_init(void)
{
    int rc;

    rc = sqlite3_open_v2(
        DB_PATH, &g_db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        NULL
    );
    if (rc != SQLITE_OK) {
        log_write(LOG_ERROR, "StorageMgr", "sqlite3_open(%s) failed: %s",
                  DB_PATH, sqlite3_errmsg(g_db));
        return -1;
    }

    /* WAL mode: allows concurrent reads while writing. */
    char *err_msg = NULL;
    rc = sqlite3_exec(g_db, "PRAGMA journal_mode=WAL;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        log_write(LOG_WARNING, "StorageMgr", "WAL mode failed: %s", err_msg);
        sqlite3_free(err_msg);
    }

    rc = sqlite3_exec(g_db, SQL_CREATE_TABLE, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        log_write(LOG_ERROR, "StorageMgr", "CREATE TABLE failed: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(g_db);
        g_db = NULL;
        return -1;
    }

    /* Prepare all statements once — compile SQL to bytecode up front. */
    rc = sqlite3_prepare_v2(g_db, SQL_INSERT, -1, &g_stmt_insert, NULL);
    if (rc != SQLITE_OK) {
        log_write(LOG_ERROR, "StorageMgr", "prepare INSERT failed: %s",
                  sqlite3_errmsg(g_db));
        sqlite3_close(g_db);
        g_db = NULL;
        return -1;
    }

    rc = sqlite3_prepare_v2(g_db, SQL_GET_SENSORS, -1, &g_stmt_get_sensors, NULL);
    if (rc != SQLITE_OK) {
        log_write(LOG_ERROR, "StorageMgr", "prepare GET_SENSORS failed: %s",
                  sqlite3_errmsg(g_db));
        sqlite3_finalize(g_stmt_insert);
        sqlite3_close(g_db);
        g_db = NULL;
        return -1;
    }

    rc = sqlite3_prepare_v2(g_db, SQL_GET_READINGS, -1, &g_stmt_get_readings, NULL);
    if (rc != SQLITE_OK) {
        log_write(LOG_ERROR, "StorageMgr", "prepare GET_READINGS failed: %s",
                  sqlite3_errmsg(g_db));
        sqlite3_finalize(g_stmt_insert);
        sqlite3_finalize(g_stmt_get_sensors);
        sqlite3_close(g_db);
        g_db = NULL;
        return -1;
    }

    log_write(LOG_INFO, "StorageMgr", "Database '%s' ready (WAL mode).", DB_PATH);
    return 0;
}

void storage_close(void)
{
    pthread_mutex_lock(&g_db_mutex);

    if (g_stmt_insert)       { sqlite3_finalize(g_stmt_insert);       g_stmt_insert = NULL; }
    if (g_stmt_get_sensors)  { sqlite3_finalize(g_stmt_get_sensors);  g_stmt_get_sensors = NULL; }
    if (g_stmt_get_readings) { sqlite3_finalize(g_stmt_get_readings); g_stmt_get_readings = NULL; }
    if (g_db)                { sqlite3_close(g_db);                   g_db = NULL; }

    pthread_mutex_unlock(&g_db_mutex);
    pthread_mutex_destroy(&g_db_mutex);

    log_write(LOG_INFO, "StorageMgr", "Database closed.");
}

/* ── Write ────────────────────────────────────────────────── */

int storage_insert_reading(uint16_t sensor_id, float temperature,
                           float humidity, float pressure)
{
    if (!g_db || !g_stmt_insert) {
        log_write(LOG_ERROR, "StorageMgr", "Not initialized.");
        return -1;
    }

    char id_str[16];
    snprintf(id_str, sizeof(id_str), "SENSOR%03u", sensor_id);

    pthread_mutex_lock(&g_db_mutex);

    sqlite3_bind_text(g_stmt_insert,   1, id_str, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(g_stmt_insert, 2, (double)temperature);
    sqlite3_bind_double(g_stmt_insert, 3, (double)humidity);
    sqlite3_bind_double(g_stmt_insert, 4, (double)pressure);

    int rc = sqlite3_step(g_stmt_insert);
    sqlite3_reset(g_stmt_insert);

    pthread_mutex_unlock(&g_db_mutex);

    if (rc != SQLITE_DONE) {
        log_write(LOG_ERROR, "StorageMgr", "INSERT failed for %s: %s",
                  id_str, sqlite3_errmsg(g_db));
        return -1;
    }
    return 0;
}

/* ── Read ─────────────────────────────────────────────────── */

int storage_get_sensors(char ***sensors, int *count)
{
    if (!g_db || !g_stmt_get_sensors) {
        log_write(LOG_ERROR, "StorageMgr", "Not initialized.");
        return -1;
    }

    *sensors = NULL;
    *count   = 0;

    #define MAX_SENSORS 64
    char *tmp[MAX_SENSORS];
    int n = 0;

    pthread_mutex_lock(&g_db_mutex);

    while (sqlite3_step(g_stmt_get_sensors) == SQLITE_ROW && n < MAX_SENSORS) {
        const char *id = (const char *)sqlite3_column_text(g_stmt_get_sensors, 0);
        if (id) {
            tmp[n] = strdup(id);
            if (tmp[n]) n++;
        }
    }
    sqlite3_reset(g_stmt_get_sensors);

    pthread_mutex_unlock(&g_db_mutex);

    if (n == 0) return 0;

    *sensors = malloc(sizeof(char *) * n);
    if (!*sensors) {
        for (int i = 0; i < n; i++) free(tmp[i]);
        log_write(LOG_ERROR, "StorageMgr", "malloc failed in get_sensors.");
        return -1;
    }

    memcpy(*sensors, tmp, sizeof(char *) * n);
    *count = n;
    return 0;
}

int storage_get_readings(const char *sensor_id, int limit,
                         SensorReading **readings, int *count)
{
    if (!g_db || !g_stmt_get_readings) {
        log_write(LOG_ERROR, "StorageMgr", "Not initialized.");
        return -1;
    }

    *readings = NULL;
    *count    = 0;

    SensorReading *result = malloc(sizeof(SensorReading) * limit);
    if (!result) {
        log_write(LOG_ERROR, "StorageMgr", "malloc failed in get_readings.");
        return -1;
    }

    int n = 0;

    pthread_mutex_lock(&g_db_mutex);

    sqlite3_bind_text(g_stmt_get_readings, 1, sensor_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(g_stmt_get_readings,  2, limit);

    while (sqlite3_step(g_stmt_get_readings) == SQLITE_ROW && n < limit) {
        const char *ts = (const char *)sqlite3_column_text(g_stmt_get_readings, 0);
        double temp    = sqlite3_column_double(g_stmt_get_readings, 1);
        double hum     = sqlite3_column_double(g_stmt_get_readings, 2);
        double press   = sqlite3_column_double(g_stmt_get_readings, 3);

        if (ts) {
            strncpy(result[n].timestamp, ts, sizeof(result[n].timestamp) - 1);
            result[n].timestamp[sizeof(result[n].timestamp) - 1] = '\0';
        } else {
            result[n].timestamp[0] = '\0';
        }
        result[n].temperature = temp;
        result[n].humidity    = hum;
        result[n].pressure    = press;
        n++;
    }
    sqlite3_reset(g_stmt_get_readings);

    pthread_mutex_unlock(&g_db_mutex);

    if (n == 0) {
        free(result);
        return 0;
    }

    *readings = result;
    *count    = n;
    return 0;
}

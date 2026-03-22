/**
 * @file    storage_manager.h
 * @brief   Thread-safe SQLite interface for sensor data persistence.
 * @author  Nguyen Si Phu
 *
 * Thread-safety model:
 *   - Single sqlite3 connection shared across all threads.
 *   - Every operation is guarded by an internal pthread_mutex.
 *   - WAL mode is enabled for better read concurrency at the DB level,
 *     but shared prepared statements still require application-level locking.
 *
 * Lifecycle:
 *   main() calls storage_init()  at startup (after log_init).
 *   main() calls storage_close() at shutdown (before log_close).
 *   Client threads call storage_insert_reading() — internally synchronized.
 *   API thread calls storage_get_*() — internally synchronized.
 */

#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <stdint.h>

#define DB_PATH  "sensor_data.db"

/** Read result for the API layer. */
typedef struct {
    char   timestamp[32];   /* "YYYY-MM-DD HH:MM:SS" */
    double temperature;
    double humidity;
    double pressure;
} SensorReading;

int  storage_init(void);
void storage_close(void);

/**
 * Insert one sensor reading into the database.
 * Thread-safe: locks/unlocks the mutex internally.
 * @return 0 on success, -1 on failure.
 */
int storage_insert_reading(uint16_t sensor_id, float temperature,
                           float humidity, float pressure);

/**
 * Retrieve the list of unique sensor IDs from the database.
 * Caller must free each string and the array itself.
 * @return 0 on success, -1 on failure.
 */
int storage_get_sensors(char ***sensors, int *count);

/**
 * Retrieve the most recent N readings for a given sensor.
 * Results are sorted in ascending timestamp order (oldest → newest).
 * Caller must free the readings array.
 * @return 0 on success, -1 on failure.
 */
int storage_get_readings(const char *sensor_id, int limit,
                         SensorReading **readings, int *count);

#endif /* STORAGE_MANAGER_H */

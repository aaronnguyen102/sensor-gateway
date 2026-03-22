/**
 * @file    log_process.h
 * @brief   Thread-safe logging module — writes to gateway.log and stdout.
 * @author  Nguyen Si Phu
 *
 * Design choices:
 *   - Mutex-based serialization: simple and sufficient for ~5 log msgs/sec.
 *     For higher throughput, a lock-free ring buffer + writer thread would
 *     be more appropriate.
 *   - fflush() after every line: crash logs are the most valuable data in
 *     embedded/IoT systems — unflushed stdio buffers are lost on crash.
 *   - Millisecond timestamps via clock_gettime(CLOCK_REALTIME): multiple
 *     events may occur within the same second; ms precision preserves order.
 *
 * Format:
 *   [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL  ] [Module] Message
 *
 * Lifecycle:
 *   main() calls log_init()  FIRST  (before any other module).
 *   main() calls log_close() LAST   (after all modules shut down).
 *   Any thread may call log_write() freely — it is internally synchronized.
 */

#ifndef LOG_PROCESS_H
#define LOG_PROCESS_H

#define LOG_FILE  "gateway.log"

typedef enum {
    LOG_DEBUG,      /* Verbose developer diagnostics       */
    LOG_INFO,       /* Normal operations: start, connect   */
    LOG_WARNING,    /* Non-critical: bad checksum, limit   */
    LOG_ERROR       /* Critical: DB failure, socket error  */
} LogLevel;

/**
 * Open log file in append mode and initialize the mutex.
 * @return 0 on success, -1 on failure.
 */
int log_init(void);

/**
 * Write one log line (thread-safe).
 * Also prints to stdout for real-time terminal monitoring.
 * @param level   Severity level.
 * @param module  Caller module name (e.g., "ConnMgr").
 * @param fmt     printf-style format string.
 */
void log_write(LogLevel level, const char *module, const char *fmt, ...);

/**
 * Flush and close the log file, then destroy the mutex.
 * Safe to call even if log_init() was never called.
 */
void log_close(void);

#endif /* LOG_PROCESS_H */

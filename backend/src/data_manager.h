/**
 * @file    data_manager.h
 * @brief   Binary packet protocol definition and validation.
 * @author  Nguyen Si Phu
 *
 * Packet format — 19 bytes, __attribute__((packed)):
 *
 *   Offset  Field        Size  Description
 *   ──────  ───────────  ────  ──────────────────────────────────
 *   [0]     start1        1    0xAA — frame delimiter
 *   [1]     start2        1    0x55 — frame delimiter
 *   [2-3]   sensor_id     2    uint16_t, network byte order
 *   [4]     dlc           1    Data Length Code = 13
 *   [5]     data_type     1    0x01 = multi-sensor payload
 *   [6-9]   temperature   4    IEEE 754 float, °C
 *   [10-13] humidity      4    IEEE 754 float, %
 *   [14-17] pressure      4    IEEE 754 float, hPa
 *   [18]    checksum      1    XOR of bytes [2..17]
 *
 * Checksum covers sensor_id through pressure (16 bytes).
 * Start bytes are excluded (fixed values add no integrity value).
 */

#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include <stdint.h>

#define START_BYTE_1    0xAA
#define START_BYTE_2    0x55
#define PACKET_SIZE     19
#define DATA_TYPE_TEMP  0x01

/* Checksum range: bytes [2..17] = 16 bytes */
#define CHECKSUM_OFFSET 2
#define CHECKSUM_LEN    16

typedef struct {
    uint8_t  start1;        /* 0xAA                            */
    uint8_t  start2;        /* 0x55                            */
    uint16_t sensor_id;     /* Network byte order (big-endian) */
    uint8_t  dlc;           /* 13                              */
    uint8_t  data_type;     /* 0x01 = multi-sensor             */
    float    temperature;   /* °C, IEEE 754                    */
    float    humidity;      /* %, IEEE 754                     */
    float    pressure;      /* hPa, IEEE 754                   */
    uint8_t  checksum;      /* XOR of bytes [2..17]            */
} __attribute__((packed)) SensorPacket;

/**
 * Compute XOR checksum over a byte range.
 * Used by both the gateway (verify) and the simulator (build).
 */
uint8_t calculate_checksum(const uint8_t *data, int len);

/**
 * Validate a received packet (start bytes, DLC, checksum, value ranges).
 * @return 1 if valid, 0 if invalid.
 */
int validate_packet(const SensorPacket *pkt);

#endif /* DATA_MANAGER_H */

/**
 * @file    data_manager.c
 * @brief   Packet parsing and multi-layer validation.
 * @author  Nguyen Si Phu
 */

#include "data_manager.h"

#include <stdio.h>
#include <stdint.h>
#include <arpa/inet.h>   /* ntohs() */

uint8_t calculate_checksum(const uint8_t *data, int len)
{
    uint8_t result = 0;
    for (int i = 0; i < len; i++) {
        result ^= data[i];
    }
    return result;
}

/**
 * Four-layer validation:
 *   1. Start bytes  — frame synchronization
 *   2. DLC          — protocol version / payload length
 *   3. Checksum     — data integrity (XOR)
 *   4. Value ranges — physical plausibility (DHT22 / BMP280 specs)
 */
int validate_packet(const SensorPacket *pkt)
{
    /* Layer 1: Frame delimiters — detect byte misalignment or noise */
    if (pkt->start1 != START_BYTE_1 || pkt->start2 != START_BYTE_2) {
        fprintf(stderr, "[DataMgr] Invalid start bytes: 0x%02X 0x%02X\n",
                pkt->start1, pkt->start2);
        return 0;
    }

    /* Layer 2: DLC — 13 = data_type(1) + temp(4) + humidity(4) + pressure(4) */
    if (pkt->dlc != 13) {
        fprintf(stderr, "[DataMgr] Invalid DLC: %d (expected 13)\n", pkt->dlc);
        return 0;
    }

    /* Layer 3: Checksum — XOR bytes [2..17] */
    const uint8_t *raw     = (const uint8_t *)pkt;
    uint8_t        expected = calculate_checksum(raw + CHECKSUM_OFFSET, CHECKSUM_LEN);

    if (pkt->checksum != expected) {
        fprintf(stderr, "[DataMgr] Checksum fail sensor_id=%u: got 0x%02X expected 0x%02X\n",
                ntohs(pkt->sensor_id), pkt->checksum, expected);
        return 0;
    }

    /* Layer 4: Physical value ranges — reject impossible readings */
    if (pkt->temperature < -40.0f || pkt->temperature > 125.0f) {
        fprintf(stderr, "[DataMgr] Temperature out of range for sensor %u: %.2f°C\n",
                ntohs(pkt->sensor_id), pkt->temperature);
        return 0;
    }

    if (pkt->humidity < 0.0f || pkt->humidity > 100.0f) {
        fprintf(stderr, "[DataMgr] Humidity out of range for sensor %u: %.2f%%\n",
                ntohs(pkt->sensor_id), pkt->humidity);
        return 0;
    }

    if (pkt->pressure < 800.0f || pkt->pressure > 1200.0f) {
        fprintf(stderr, "[DataMgr] Pressure out of range for sensor %u: %.2fhPa\n",
                ntohs(pkt->sensor_id), pkt->pressure);
        return 0;
    }

    return 1;
}

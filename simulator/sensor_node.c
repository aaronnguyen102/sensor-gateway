/**
 * @file    sensor_node.c
 * @brief   Simulated sensor node — TCP client sending binary packets.
 * @author  Nguyen Si Phu
 *
 * Usage:   ./sensor_node <gateway_ip> <sensor_id>
 * Example: ./sensor_node 192.168.1.100 1
 *
 * Each instance simulates one independent sensor node.
 * Run multiple instances with different IDs to test multi-client.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "data_manager.h"

#define GATEWAY_PORT    8888
#define SEND_INTERVAL   2       /* Seconds between packets */

/* Simulation ranges based on DHT22 / BMP280 specs */
#define SIM_TEMP_MIN        20.0f
#define SIM_TEMP_MAX        35.0f
#define SIM_HUMIDITY_MIN    30.0f
#define SIM_HUMIDITY_MAX    70.0f
#define SIM_PRESSURE_MIN    1000.0f
#define SIM_PRESSURE_MAX    1025.0f

static float random_range(float min, float max)
{
    return min + ((float)rand() / (float)RAND_MAX) * (max - min);
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <gateway_ip> <sensor_id (1-65535)>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *gateway_ip = argv[1];
    uint16_t    sensor_id  = (uint16_t)atoi(argv[2]);

    if (sensor_id == 0) {
        fprintf(stderr, "[Sensor] sensor_id must be between 1 and 65535\n");
        return EXIT_FAILURE;
    }

    /* Per-sensor seed to avoid identical data across nodes. */
    srand((unsigned int)(time(NULL) ^ (uint32_t)sensor_id));

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in gw_addr;
    memset(&gw_addr, 0, sizeof(gw_addr));
    gw_addr.sin_family = AF_INET;
    gw_addr.sin_port   = htons(GATEWAY_PORT);

    if (inet_pton(AF_INET, gateway_ip, &gw_addr.sin_addr) <= 0) {
        fprintf(stderr, "[Sensor%03u] Invalid IP: %s\n", sensor_id, gateway_ip);
        close(sock);
        return EXIT_FAILURE;
    }

    if (connect(sock, (struct sockaddr *)&gw_addr, sizeof(gw_addr)) < 0) {
        perror("connect");
        close(sock);
        return EXIT_FAILURE;
    }
    printf("[Sensor%03u] Connected to %s:%d\n", sensor_id, gateway_ip, GATEWAY_PORT);

    while (1) {
        SensorPacket pkt;
        pkt.start1      = START_BYTE_1;
        pkt.start2      = START_BYTE_2;
        pkt.sensor_id   = htons(sensor_id);
        pkt.dlc         = 13;
        pkt.data_type   = DATA_TYPE_TEMP;
        pkt.temperature = random_range(SIM_TEMP_MIN, SIM_TEMP_MAX);
        pkt.humidity    = random_range(SIM_HUMIDITY_MIN, SIM_HUMIDITY_MAX);
        pkt.pressure    = random_range(SIM_PRESSURE_MIN, SIM_PRESSURE_MAX);

        const uint8_t *raw = (const uint8_t *)&pkt;
        pkt.checksum = calculate_checksum(raw + CHECKSUM_OFFSET, CHECKSUM_LEN);

        ssize_t sent = send(sock, &pkt, PACKET_SIZE, 0);
        if (sent < 0) {
            perror("[Sensor] send");
            break;
        }
        if (sent != PACKET_SIZE) {
            fprintf(stderr, "[Sensor%03u] Short send: %zd/%d bytes\n",
                    sensor_id, sent, PACKET_SIZE);
            break;
        }

        printf("[Sensor%03u] T=%.2f°C  H=%.1f%%  P=%.1fhPa  chk=0x%02X\n",
               sensor_id, pkt.temperature, pkt.humidity,
               pkt.pressure, pkt.checksum);

        sleep(SEND_INTERVAL);
    }

    close(sock);
    printf("[Sensor%03u] Disconnected.\n", sensor_id);
    return EXIT_SUCCESS;
}

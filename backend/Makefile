# Sensor Gateway — Makefile
# Run from: sensor-gateway/backend/
# Build:   make
# Clean:   make clean

CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -g
LDFLAGS = -lsqlite3 -lpthread -lmicrohttpd

SRCDIR  = src
SRCS    = $(SRCDIR)/main.c \
          $(SRCDIR)/connection_manager.c \
          $(SRCDIR)/data_manager.c \
          $(SRCDIR)/storage_manager.c \
          $(SRCDIR)/log_process.c \
          $(SRCDIR)/api_server.c

OBJS    = $(SRCS:.c=.o)
TARGET  = gateway

# Simulator lives outside backend/
SIM_SRC = ../simulator/sensor_node.c
SIM_BIN = sensor_node

.PHONY: all clean

all: $(TARGET) $(SIM_BIN)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -I$(SRCDIR) -c $< -o $@

# Simulator shares data_manager.c for packet format and checksum
$(SIM_BIN): $(SIM_SRC) $(SRCDIR)/data_manager.c
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $^

clean:
	rm -f $(OBJS) $(TARGET) $(SIM_BIN)
	rm -f sensor_data.db sensor_data.db-wal sensor_data.db-shm
	rm -f gateway.log

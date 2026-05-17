#ifndef LORAHAM_CLIENT_SLOT_H
#define LORAHAM_CLIENT_SLOT_H

#include "client_set.h"
#include "config_stream.h"

/* --- Unified client slot state ------------------------------------------ */

typedef struct {
    int fd;
    ClientOutputQueue output;
    ConfigStreamBuffer stream;
} ClientSlot;

void client_slot_init(ClientSlot *slot);
void client_slot_init_all(ClientSlot *slots, int count);
int client_slot_has_client(const ClientSlot *slot);
int client_slot_fd(const ClientSlot *slot);
void client_slot_set_fd(ClientSlot *slot, int fd);
void client_slot_reset_output(ClientSlot *slot);
void client_slot_reset_stream(ClientSlot *slot);
void client_slot_close(ClientSlot *slot);
void client_slot_close_all(ClientSlot *slots, int count);

#endif

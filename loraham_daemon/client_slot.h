#ifndef LORAHAM_CLIENT_SLOT_H
#define LORAHAM_CLIENT_SLOT_H

#include "client_output_queue.h"
#include "config_stream.h"
#include "event_loop.h"

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
int client_slot_add(ClientSlot *slots, int max_clients, int fd);
int client_slot_accept_with_output(int listen_fd, ClientSlot *slots, int max_clients);
int client_slot_ready(const ClientSlot *slot, const EventLoopReadySet *ready);
int client_slot_output_ready(const ClientSlot *slot, const EventLoopReadySet *ready);
void client_slot_add_to_event_loop_with_output(ClientSlot *slots, int max_clients, EventLoopSet *set);
void client_slot_flush_output(ClientSlot *slot);
void client_slot_flush_ready_outputs(ClientSlot *slots, int max_clients, const EventLoopReadySet *ready);
int client_slot_has_clients(ClientSlot *slots, int max_clients);
void client_slot_broadcast_bytes_queued(ClientSlot *slots, int max_clients, const uint8_t *buf, size_t len);
void client_slot_broadcast_queued(ClientSlot *slots, int max_clients, const char *msg);

#endif

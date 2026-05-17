#ifndef LORAHAM_CLIENT_SET_H
#define LORAHAM_CLIENT_SET_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "event_loop.h"

/* --- Client output queue --- */

#define CLIENT_OUTPUT_QUEUE_CAPACITY (64 * 1024)

typedef struct {
    uint8_t data[CLIENT_OUTPUT_QUEUE_CAPACITY];
    size_t len;
} ClientOutputQueue;

void client_output_queue_init(ClientOutputQueue *queue);
void client_output_queue_init_all(ClientOutputQueue *queues, int count);
void client_output_queue_reset(ClientOutputQueue *queue);
size_t client_output_queue_pending(const ClientOutputQueue *queue);
const uint8_t *client_output_queue_data(const ClientOutputQueue *queue);
ssize_t client_output_queue_flush_fd(int fd, ClientOutputQueue *queue);
int client_output_queue_append(ClientOutputQueue *queue, const uint8_t *buf, size_t len);
size_t client_output_queue_consume(ClientOutputQueue *queue, size_t len);

/* --- Client slot handling --- */

int client_set_add(int *clients, int max_clients, int fd);
int client_set_set_nonblocking(int fd);
int client_set_accept(int listen_fd, int *clients, int max_clients);
int client_set_accept_with_output(int listen_fd, int *clients, ClientOutputQueue *queues, int max_clients);
void client_set_close_slot(int *clients, int index);
void client_set_close_slot_with_output(int *clients, ClientOutputQueue *queues, int index);
void client_set_close_all(int *clients, int max_clients);
void client_set_close_all_with_output(int *clients, ClientOutputQueue *queues, int max_clients);
ssize_t client_set_read_slot(int *clients, int index, void *buf, size_t len);
ssize_t client_set_read_slot_with_output(int *clients, ClientOutputQueue *queues, int index, void *buf, size_t len);
void client_set_add_to_event_loop(int *clients, int max_clients, EventLoopSet *set);
void client_set_add_to_event_loop_with_output(int *clients, ClientOutputQueue *queues, int max_clients, EventLoopSet *set);
int client_set_slot_ready(int *clients, int index, const EventLoopReadySet *ready);
int client_set_output_ready(int *clients, ClientOutputQueue *queues, int index, const EventLoopReadySet *ready);
int client_set_has_clients(int *clients, int max_clients);

/* --- Client text broadcast --- */

void client_set_broadcast(int *clients, int max_clients, const char *msg);
void client_set_broadcast_bytes(int *clients, int max_clients, const uint8_t *buf, size_t len);
void client_set_flush_output_slot(int *clients, ClientOutputQueue *queues, int index);
void client_set_flush_outputs(int *clients, ClientOutputQueue *queues, int max_clients);
void client_set_flush_ready_outputs(int *clients, ClientOutputQueue *queues, int max_clients, const EventLoopReadySet *ready);
void client_set_broadcast_queued(int *clients, ClientOutputQueue *queues, int max_clients, const char *msg);
void client_set_broadcast_bytes_queued(int *clients, ClientOutputQueue *queues, int max_clients, const uint8_t *buf, size_t len);

#endif

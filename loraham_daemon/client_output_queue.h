#ifndef LORAHAM_CLIENT_OUTPUT_QUEUE_H
#define LORAHAM_CLIENT_OUTPUT_QUEUE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* --- Client output queue ------------------------------------------------- */

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

#endif

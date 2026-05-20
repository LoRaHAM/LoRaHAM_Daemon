#include "client_slot.h"
#include "client_set.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* --- Unified client slot state ------------------------------------------ */

void client_slot_init(ClientSlot *slot)
{
    if (!slot)
        return;

    slot->fd = 0;
    client_output_queue_init(&slot->output);
    config_stream_init(&slot->stream);
}

void client_slot_init_all(ClientSlot *slots, int count)
{
    if (!slots || count <= 0)
        return;

    for (int i = 0; i < count; i++)
        client_slot_init(&slots[i]);
}

int client_slot_has_client(const ClientSlot *slot)
{
    return slot && slot->fd > 0;
}

int client_slot_fd(const ClientSlot *slot)
{
    return slot ? slot->fd : 0;
}

void client_slot_set_fd(ClientSlot *slot, int fd)
{
    if (!slot)
        return;

    slot->fd = fd;
    client_output_queue_reset(&slot->output);
    config_stream_init(&slot->stream);
}

void client_slot_reset_output(ClientSlot *slot)
{
    if (!slot)
        return;

    client_output_queue_reset(&slot->output);
}

void client_slot_reset_stream(ClientSlot *slot)
{
    if (!slot)
        return;

    config_stream_init(&slot->stream);
}

void client_slot_close(ClientSlot *slot)
{
    if (!slot)
        return;

    if (slot->fd > 0)
        close(slot->fd);

    client_slot_init(slot);
}

void client_slot_close_all(ClientSlot *slots, int count)
{
    if (!slots || count <= 0)
        return;

    for (int i = 0; i < count; i++)
        client_slot_close(&slots[i]);
}


/* --- ClientSlot socket helpers ------------------------------------------ */

int client_slot_add(ClientSlot *slots, int max_clients, int fd)
{
    if (!slots)
        return 0;

    for (int i = 0; i < max_clients; i++) {
        if (!client_slot_has_client(&slots[i])) {
            client_slot_set_fd(&slots[i], fd);
            return 1;
        }
    }

    return 0;
}

int client_slot_accept_with_output(int listen_fd, ClientSlot *slots, int max_clients)
{
    int fd = accept(listen_fd, NULL, NULL);
    int saved_errno;

    if (fd < 0)
        return fd;

    if (client_set_set_nonblocking(fd) != 0) {
        saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    if (!client_slot_add(slots, max_clients, fd)) {
        close(fd);
        errno = EMFILE;
        return -1;
    }

    return fd;
}

int client_slot_ready(const ClientSlot *slot, const EventLoopReadySet *ready)
{
    return client_slot_has_client(slot) &&
           event_loop_ready_fd_read(ready, slot->fd);
}

int client_slot_output_ready(const ClientSlot *slot, const EventLoopReadySet *ready)
{
    return client_slot_has_client(slot) &&
           client_output_queue_pending(&slot->output) > 0 &&
           event_loop_ready_fd_write(ready, slot->fd);
}

void client_slot_add_to_event_loop_with_output(ClientSlot *slots,
                                               int max_clients,
                                               EventLoopSet *set)
{
    if (!slots)
        return;

    for (int i = 0; i < max_clients; i++) {
        if (client_slot_has_client(&slots[i])) {
            uint32_t events = EVENT_LOOP_EVENT_READ;

            if (client_output_queue_pending(&slots[i].output) > 0)
                events |= EVENT_LOOP_EVENT_WRITE;

            event_loop_add_fd_events(set, slots[i].fd, events);
        }
    }
}

void client_slot_flush_output(ClientSlot *slot)
{
    if (!slot)
        return;

    if (!client_slot_has_client(slot)) {
        client_output_queue_reset(&slot->output);
        return;
    }

    if (client_output_queue_flush_fd(slot->fd, &slot->output) < 0)
        client_slot_close(slot);
}

void client_slot_flush_ready_outputs(ClientSlot *slots,
                                     int max_clients,
                                     const EventLoopReadySet *ready)
{
    if (!slots)
        return;

    for (int i = 0; i < max_clients; i++) {
        if (client_slot_output_ready(&slots[i], ready))
            client_slot_flush_output(&slots[i]);
    }
}

int client_slot_has_clients(ClientSlot *slots, int max_clients)
{
    if (!slots)
        return 0;

    for (int i = 0; i < max_clients; i++) {
        if (client_slot_has_client(&slots[i]))
            return 1;
    }

    return 0;
}

void client_slot_broadcast_bytes_queued(ClientSlot *slots,
                                        int max_clients,
                                        const uint8_t *buf,
                                        size_t len)
{
    if (len == 0)
        return;

    if (!slots || !buf)
        return;

    for (int i = 0; i < max_clients; i++) {
        if (!client_slot_has_client(&slots[i])) {
            client_output_queue_reset(&slots[i].output);
            continue;
        }

        if (!client_output_queue_append(&slots[i].output, buf, len)) {
            client_slot_close(&slots[i]);
            continue;
        }

        client_slot_flush_output(&slots[i]);
    }
}

void client_slot_broadcast_queued(ClientSlot *slots,
                                  int max_clients,
                                  const char *msg)
{
    if (!msg)
        return;

    client_slot_broadcast_bytes_queued(slots, max_clients,
                                       (const uint8_t *)msg, strlen(msg));
}

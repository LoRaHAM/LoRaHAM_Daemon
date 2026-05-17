#include "client_slot.h"

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

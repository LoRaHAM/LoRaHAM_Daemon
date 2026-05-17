#include "event_loop.h"

/* --- backend-neutral event-loop wrapper --- */

void event_loop_init_select(EventLoopSet *set)
{
    set->backend = EVENT_LOOP_BACKEND_SELECT;
    event_loop_select_reset(&set->select_backend);
}

EventLoopBackend event_loop_backend(const EventLoopSet *set)
{
    return set->backend;
}

void event_loop_reset(EventLoopSet *set)
{
    set->backend = EVENT_LOOP_BACKEND_SELECT;
    event_loop_select_reset(&set->select_backend);
}

void event_loop_add_fd(EventLoopSet *set, int fd)
{
    event_loop_select_add_fd(&set->select_backend, fd);
}

int event_loop_has_fd(const EventLoopSet *set, int fd)
{
    return event_loop_select_has_fd(&set->select_backend, fd);
}

int event_loop_has_registered_fds(const EventLoopSet *set)
{
    return event_loop_select_fd_limit(&set->select_backend) > 0 ? 1 : 0;
}

int event_loop_ready_fd(const EventLoopReadySet *ready, int fd)
{
    return event_loop_select_ready_fd(ready, fd);
}

int event_loop_wait(const EventLoopSet *set,
                    EventLoopReadySet *ready,
                    int timeout_usec)
{
    return event_loop_select_wait(&set->select_backend, ready, timeout_usec);
}

#include "event_loop.h"

/* --- backend-neutral event-loop wrapper --- */

void event_loop_init_select(EventLoopSet *set)
{
    set->backend = EVENT_LOOP_BACKEND_SELECT;
    set->epoll_backend.epoll_fd = -1;
    set->epoll_backend.registered_fds = 0;
    event_loop_select_reset(&set->select_backend);
}

int event_loop_init_epoll(EventLoopSet *set)
{
    event_loop_select_reset(&set->select_backend);

    if (event_loop_epoll_init(&set->epoll_backend) != 0) {
        set->backend = EVENT_LOOP_BACKEND_SELECT;
        return -1;
    }

    set->backend = EVENT_LOOP_BACKEND_EPOLL;
    return 0;
}

int event_loop_init_default(EventLoopSet *set)
{
    if (event_loop_init_epoll(set) == 0)
        return 0;

    event_loop_init_select(set);
    return -1;
}

void event_loop_close(EventLoopSet *set)
{
    if (set->backend == EVENT_LOOP_BACKEND_EPOLL)
        event_loop_epoll_close(&set->epoll_backend);

    set->backend = EVENT_LOOP_BACKEND_SELECT;
}

EventLoopBackend event_loop_backend(const EventLoopSet *set)
{
    return set->backend;
}

void event_loop_reset(EventLoopSet *set)
{
    if (set->backend == EVENT_LOOP_BACKEND_EPOLL) {
        event_loop_epoll_reset(&set->epoll_backend);
        return;
    }

    event_loop_select_reset(&set->select_backend);
}

void event_loop_add_fd(EventLoopSet *set, int fd)
{
    if (set->backend == EVENT_LOOP_BACKEND_EPOLL) {
        (void)event_loop_epoll_add_fd(&set->epoll_backend, fd);
        return;
    }

    event_loop_select_add_fd(&set->select_backend, fd);
}

int event_loop_has_fd(const EventLoopSet *set, int fd)
{
    if (set->backend == EVENT_LOOP_BACKEND_EPOLL)
        return 0;

    return event_loop_select_has_fd(&set->select_backend, fd);
}

int event_loop_has_registered_fds(const EventLoopSet *set)
{
    if (set->backend == EVENT_LOOP_BACKEND_EPOLL)
        return event_loop_epoll_has_registered_fds(&set->epoll_backend);

    return event_loop_select_fd_limit(&set->select_backend) > 0 ? 1 : 0;
}

int event_loop_ready_fd(const EventLoopReadySet *ready, int fd)
{
    if (ready->backend == EVENT_LOOP_BACKEND_EPOLL)
        return event_loop_epoll_ready_fd(&ready->epoll_ready, fd);

    return event_loop_select_ready_fd(&ready->select_ready, fd);
}

int event_loop_wait(const EventLoopSet *set,
                    EventLoopReadySet *ready,
                    int timeout_usec)
{
    ready->backend = set->backend;

    if (set->backend == EVENT_LOOP_BACKEND_EPOLL)
        return event_loop_epoll_wait(&set->epoll_backend,
                                     &ready->epoll_ready,
                                     timeout_usec);

    return event_loop_select_wait(&set->select_backend,
                                  &ready->select_ready,
                                  timeout_usec);
}

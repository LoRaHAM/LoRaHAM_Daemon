#include "event_loop.h"

/* --- Event-loop wrapper ------------------------------------------------- */

int event_loop_init(EventLoopSet *set)
{
    if (event_loop_epoll_init(&set->epoll_backend) != 0) {
        set->backend = EVENT_LOOP_BACKEND_EPOLL;
        return -1;
    }

    set->backend = EVENT_LOOP_BACKEND_EPOLL;
    return 0;
}

void event_loop_close(EventLoopSet *set)
{
    event_loop_epoll_close(&set->epoll_backend);
    set->backend = EVENT_LOOP_BACKEND_EPOLL;
}

EventLoopBackend event_loop_backend(const EventLoopSet *set)
{
    return set->backend;
}

const char *event_loop_backend_name(EventLoopBackend backend)
{
    (void)backend;

    return "epoll";
}

void event_loop_reset(EventLoopSet *set)
{
    event_loop_epoll_reset(&set->epoll_backend);
}

void event_loop_add_fd(EventLoopSet *set, int fd)
{
    (void)event_loop_epoll_add_fd(&set->epoll_backend, fd);
}

void event_loop_add_fd_events(EventLoopSet *set, int fd, uint32_t events)
{
    (void)event_loop_epoll_add_fd_events(&set->epoll_backend, fd, events);
}

int event_loop_has_registered_fds(const EventLoopSet *set)
{
    return event_loop_epoll_has_registered_fds(&set->epoll_backend);
}

int event_loop_ready_fd(const EventLoopReadySet *ready, int fd)
{
    return event_loop_epoll_ready_fd(&ready->epoll_ready, fd);
}

int event_loop_ready_fd_read(const EventLoopReadySet *ready, int fd)
{
    return event_loop_epoll_ready_fd_read(&ready->epoll_ready, fd);
}

int event_loop_ready_fd_write(const EventLoopReadySet *ready, int fd)
{
    return event_loop_epoll_ready_fd_write(&ready->epoll_ready, fd);
}

int event_loop_wait(const EventLoopSet *set,
                    EventLoopReadySet *ready,
                    int timeout_usec)
{
    ready->backend = set->backend;

    return event_loop_epoll_wait(&set->epoll_backend,
                                 &ready->epoll_ready,
                                 timeout_usec);
}

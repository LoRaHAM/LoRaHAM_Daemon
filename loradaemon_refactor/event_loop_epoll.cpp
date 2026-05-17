#include "event_loop_epoll.h"

#include <unistd.h>

/* --- epoll backend ------------------------------------------------------ */

int event_loop_epoll_init(EventLoopEpollSet *set)
{
    set->registered_fds = 0;
    set->epoll_fd = epoll_create1(0);

    return set->epoll_fd >= 0 ? 0 : -1;
}

void event_loop_epoll_close(EventLoopEpollSet *set)
{
    if (set->epoll_fd >= 0)
        close(set->epoll_fd);

    set->epoll_fd = -1;
    set->registered_fds = 0;
}

void event_loop_epoll_reset(EventLoopEpollSet *set)
{
    event_loop_epoll_close(set);
    (void)event_loop_epoll_init(set);
}

int event_loop_epoll_add_fd_events(EventLoopEpollSet *set, int fd, uint32_t events)
{
    struct epoll_event ev;

    if (set->epoll_fd < 0 || fd < 0)
        return -1;

    if (events == 0)
        events = EPOLLIN;

    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(set->epoll_fd, EPOLL_CTL_ADD, fd, &ev) != 0)
        return -1;

    set->registered_fds++;

    return 0;
}

int event_loop_epoll_add_fd(EventLoopEpollSet *set, int fd)
{
    return event_loop_epoll_add_fd_events(set, fd, EPOLLIN);
}

int event_loop_epoll_has_registered_fds(const EventLoopEpollSet *set)
{
    return set->registered_fds > 0 ? 1 : 0;
}

static int event_loop_epoll_ready_fd_events(const EventLoopEpollReadySet *ready,
                                            int fd,
                                            uint32_t events)
{
    if (fd < 0)
        return 0;

    for (int i = 0; i < ready->count; i++) {
        if (ready->events[i].data.fd == fd &&
            (ready->events[i].events & events))
            return 1;
    }

    return 0;
}

int event_loop_epoll_ready_fd(const EventLoopEpollReadySet *ready, int fd)
{
    if (fd < 0)
        return 0;

    for (int i = 0; i < ready->count; i++) {
        if (ready->events[i].data.fd == fd)
            return 1;
    }

    return 0;
}

int event_loop_epoll_ready_fd_read(const EventLoopEpollReadySet *ready, int fd)
{
    return event_loop_epoll_ready_fd_events(ready, fd, EPOLLIN | EPOLLHUP | EPOLLERR);
}

int event_loop_epoll_ready_fd_write(const EventLoopEpollReadySet *ready, int fd)
{
    return event_loop_epoll_ready_fd_events(ready, fd, EPOLLOUT);
}

int event_loop_epoll_wait(const EventLoopEpollSet *set,
                          EventLoopEpollReadySet *ready,
                          int timeout_usec)
{
    int timeout_ms;

    if (set->epoll_fd < 0)
        return -1;

    timeout_ms = timeout_usec / 1000;
    if (timeout_usec > 0 && timeout_ms == 0)
        timeout_ms = 1;

    ready->count = epoll_wait(set->epoll_fd,
                              ready->events,
                              EVENT_LOOP_EPOLL_MAX_EVENTS,
                              timeout_ms);

    return ready->count;
}

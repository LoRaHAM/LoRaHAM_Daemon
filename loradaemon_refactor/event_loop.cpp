#include "event_loop.h"

#include <stddef.h>

/* --- select() fd-set wrapper --- */

void event_loop_select_reset(EventLoopSelectSet *set)
{
    FD_ZERO(&set->readfds);
    set->maxfd = 0;
}

void event_loop_select_add_fd(EventLoopSelectSet *set, int fd)
{
    if (fd < 0)
        return;

    FD_SET(fd, &set->readfds);

    if (fd >= set->maxfd)
        set->maxfd = fd + 1;
}

int event_loop_select_has_fd(const EventLoopSelectSet *set, int fd)
{
    if (fd < 0)
        return 0;

    return FD_ISSET(fd, &set->readfds) ? 1 : 0;
}

int event_loop_select_fd_limit(const EventLoopSelectSet *set)
{
    return set->maxfd;
}


int event_loop_select_ready_fd(const EventLoopReadySet *ready, int fd)
{
    if (fd < 0)
        return 0;

    return FD_ISSET(fd, &ready->readfds) ? 1 : 0;
}

int event_loop_select_wait(const EventLoopSelectSet *set,
                           EventLoopReadySet *ready,
                           int timeout_usec)
{
    struct timeval tv;

    tv.tv_sec = timeout_usec / 1000000;
    tv.tv_usec = timeout_usec % 1000000;

    ready->readfds = set->readfds;

    return select(set->maxfd, &ready->readfds, NULL, NULL, &tv);
}

/* --- backend-neutral event-loop wrapper --- */

void event_loop_reset(EventLoopSet *set)
{
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

#include "event_loop.h"

#include <errno.h>

/* --- Event-loop wrapper ------------------------------------------------- */

static void event_loop_record_registration_error(EventLoopSet *set)
{
    if (set && set->registration_errno == 0)
        set->registration_errno = errno ? errno : EIO;
}

int event_loop_init(EventLoopSet *set)
{
    if (!set) {
        errno = EINVAL;
        return -1;
    }

    set->registration_errno = 0;

    if (event_loop_epoll_init(&set->epoll_backend) != 0) {
        set->backend = EVENT_LOOP_BACKEND_EPOLL;
        return -1;
    }

    set->backend = EVENT_LOOP_BACKEND_EPOLL;
    return 0;
}

void event_loop_close(EventLoopSet *set)
{
    if (!set)
        return;

    event_loop_epoll_close(&set->epoll_backend);
    set->backend = EVENT_LOOP_BACKEND_EPOLL;
    set->registration_errno = 0;
}

EventLoopBackend event_loop_backend(const EventLoopSet *set)
{
    return set ? set->backend : EVENT_LOOP_BACKEND_EPOLL;
}

const char *event_loop_backend_name(EventLoopBackend backend)
{
    (void)backend;
    return "epoll";
}

void event_loop_reset(EventLoopSet *set)
{
    if (!set)
        return;

    event_loop_epoll_reset(&set->epoll_backend);
    set->registration_errno = 0;
}

void event_loop_add_fd(EventLoopSet *set, int fd)
{
    if (!set) {
        errno = EINVAL;
        return;
    }

    if (event_loop_epoll_add_fd(&set->epoll_backend, fd) != 0)
        event_loop_record_registration_error(set);
}

void event_loop_add_fd_events(EventLoopSet *set, int fd, uint32_t events)
{
    if (!set) {
        errno = EINVAL;
        return;
    }

    if (event_loop_epoll_add_fd_events(&set->epoll_backend, fd, events) != 0)
        event_loop_record_registration_error(set);
}

void event_loop_reconcile_begin(EventLoopSet *set)
{
    if (!set) {
        errno = EINVAL;
        return;
    }

    if (event_loop_epoll_reconcile_begin(&set->epoll_backend) != 0)
        event_loop_record_registration_error(set);
}

void event_loop_reconcile_fd(EventLoopSet *set,
                             const void *owner,
                             uint32_t generation,
                             int fd,
                             uint32_t events)
{
    if (!set) {
        errno = EINVAL;
        return;
    }

    if (event_loop_epoll_reconcile_fd(&set->epoll_backend,
                                      owner,
                                      generation,
                                      fd,
                                      events) != 0)
        event_loop_record_registration_error(set);
}

void event_loop_reconcile_end(EventLoopSet *set)
{
    if (!set) {
        errno = EINVAL;
        return;
    }

    if (event_loop_epoll_reconcile_end(&set->epoll_backend) != 0)
        event_loop_record_registration_error(set);
}

int event_loop_registration_failed(const EventLoopSet *set)
{
    return set && set->registration_errno != 0;
}

int event_loop_has_registered_fds(const EventLoopSet *set)
{
    return set && event_loop_epoll_has_registered_fds(&set->epoll_backend);
}

int event_loop_ready_fd(const EventLoopReadySet *ready, int fd)
{
    return ready && event_loop_epoll_ready_fd(&ready->epoll_ready, fd);
}

int event_loop_ready_fd_read(const EventLoopReadySet *ready, int fd)
{
    return ready && event_loop_epoll_ready_fd_read(&ready->epoll_ready, fd);
}

int event_loop_ready_fd_write(const EventLoopReadySet *ready, int fd)
{
    return ready && event_loop_epoll_ready_fd_write(&ready->epoll_ready, fd);
}

int event_loop_wait(const EventLoopSet *set,
                    EventLoopReadySet *ready,
                    int timeout_usec)
{
    if (!set || !ready) {
        errno = EINVAL;
        return -1;
    }

    if (set->registration_errno != 0) {
        errno = set->registration_errno;
        return -1;
    }

    ready->backend = set->backend;
    return event_loop_epoll_wait(&set->epoll_backend,
                                 &ready->epoll_ready,
                                 timeout_usec);
}

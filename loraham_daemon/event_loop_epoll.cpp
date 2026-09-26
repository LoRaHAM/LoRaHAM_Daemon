#include "event_loop_epoll.h"

#include <errno.h>
#include <stddef.h>
#include <sys/epoll.h>
#include <unistd.h>

/* --- epoll backend ------------------------------------------------------ */

static void event_loop_epoll_clear_registration(
    EventLoopEpollRegistration *registration)
{
    if (!registration)
        return;

    registration->owner = NULL;
    registration->generation = 0;
    registration->events = 0;
    registration->seen_epoch = 0;
    registration->fd = -1;
    registration->active = 0;
}

static void event_loop_epoll_clear_registrations(EventLoopEpollSet *set)
{
    if (!set)
        return;

    for (int i = 0; i < EVENT_LOOP_EPOLL_MAX_REGISTERED_FDS; i++)
        event_loop_epoll_clear_registration(&set->registrations[i]);

    set->registered_fds = 0;
    set->reconcile_epoch = 0;
    set->reconcile_active = 0;
}

static EventLoopEpollRegistration *
event_loop_epoll_find_registration(EventLoopEpollSet *set,
                                   const void *owner,
                                   uint32_t generation)
{
    if (!set)
        return NULL;

    for (int i = 0; i < EVENT_LOOP_EPOLL_MAX_REGISTERED_FDS; i++) {
        EventLoopEpollRegistration *registration = &set->registrations[i];

        if (registration->active &&
            registration->owner == owner &&
            registration->generation == generation)
            return registration;
    }

    return NULL;
}

static EventLoopEpollRegistration *
event_loop_epoll_find_free_registration(EventLoopEpollSet *set)
{
    if (!set)
        return NULL;

    for (int i = 0; i < EVENT_LOOP_EPOLL_MAX_REGISTERED_FDS; i++) {
        if (!set->registrations[i].active)
            return &set->registrations[i];
    }

    return NULL;
}

static int event_loop_epoll_fd_seen_current(
    const EventLoopEpollSet *set,
    int fd,
    int skip_index)
{
    if (!set || fd < 0)
        return 0;

    for (int i = 0; i < EVENT_LOOP_EPOLL_MAX_REGISTERED_FDS; i++) {
        const EventLoopEpollRegistration *registration =
            &set->registrations[i];

        if (i != skip_index &&
            registration->active &&
            registration->seen_epoch == set->reconcile_epoch &&
            registration->fd == fd)
            return 1;
    }

    return 0;
}

int event_loop_epoll_init(EventLoopEpollSet *set)
{
    if (!set) {
        errno = EINVAL;
        return -1;
    }

    event_loop_epoll_clear_registrations(set);
    set->epoll_fd = epoll_create1(0);
    return set->epoll_fd >= 0 ? 0 : -1;
}

void event_loop_epoll_close(EventLoopEpollSet *set)
{
    if (!set)
        return;

    if (set->epoll_fd >= 0)
        close(set->epoll_fd);

    set->epoll_fd = -1;
    event_loop_epoll_clear_registrations(set);
}

void event_loop_epoll_reset(EventLoopEpollSet *set)
{
    if (!set)
        return;

    if (set->epoll_fd >= 0) {
        for (int i = 0; i < EVENT_LOOP_EPOLL_MAX_REGISTERED_FDS; i++) {
            EventLoopEpollRegistration *registration =
                &set->registrations[i];

            if (registration->active && registration->fd >= 0)
                (void)epoll_ctl(set->epoll_fd, EPOLL_CTL_DEL,
                                registration->fd, NULL);
        }
    }

    event_loop_epoll_clear_registrations(set);
}

int event_loop_epoll_add_fd_events(EventLoopEpollSet *set,
                                   int fd,
                                   uint32_t events)
{
    EventLoopEpollRegistration *registration;
    struct epoll_event ev;

    if (!set || set->epoll_fd < 0 || fd < 0) {
        errno = EINVAL;
        return -1;
    }

    if (set->registered_fds >= EVENT_LOOP_EPOLL_MAX_REGISTERED_FDS) {
        errno = ENOSPC;
        return -1;
    }

    if (events == 0)
        events = EPOLLIN;

    registration = event_loop_epoll_find_free_registration(set);
    if (!registration) {
        errno = ENOSPC;
        return -1;
    }

    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(set->epoll_fd, EPOLL_CTL_ADD, fd, &ev) != 0)
        return -1;

    registration->owner = NULL;
    registration->generation = 0;
    registration->events = events;
    registration->seen_epoch = 0;
    registration->fd = fd;
    registration->active = 1;
    set->registered_fds++;

    return 0;
}

int event_loop_epoll_add_fd(EventLoopEpollSet *set, int fd)
{
    return event_loop_epoll_add_fd_events(set, fd, EPOLLIN);
}

int event_loop_epoll_reconcile_begin(EventLoopEpollSet *set)
{
    if (!set || set->epoll_fd < 0) {
        errno = EINVAL;
        return -1;
    }

    if (set->reconcile_active) {
        errno = EBUSY;
        return -1;
    }

    set->reconcile_epoch++;
    if (set->reconcile_epoch == 0) {
        for (int i = 0; i < EVENT_LOOP_EPOLL_MAX_REGISTERED_FDS; i++)
            set->registrations[i].seen_epoch = 0;

        set->reconcile_epoch = 1;
    }

    set->reconcile_active = 1;
    return 0;
}

int event_loop_epoll_reconcile_fd(EventLoopEpollSet *set,
                                  const void *owner,
                                  uint32_t generation,
                                  int fd,
                                  uint32_t events)
{
    EventLoopEpollRegistration *registration;
    struct epoll_event ev;

    if (!set || set->epoll_fd < 0 || !owner || fd < 0) {
        errno = EINVAL;
        return -1;
    }

    if (!set->reconcile_active) {
        errno = EINVAL;
        return -1;
    }

    if (events == 0)
        events = EPOLLIN;

    registration = event_loop_epoll_find_registration(set, owner, generation);
    if (registration) {
        if (registration->fd != fd) {
            errno = EINVAL;
            return -1;
        }

        if (registration->events != events) {
            ev.events = events;
            ev.data.fd = fd;

            if (epoll_ctl(set->epoll_fd, EPOLL_CTL_MOD, fd, &ev) != 0)
                return -1;

            registration->events = events;
        }

        registration->seen_epoch = set->reconcile_epoch;
        return 0;
    }

    if (set->registered_fds >= EVENT_LOOP_EPOLL_MAX_REGISTERED_FDS) {
        errno = ENOSPC;
        return -1;
    }

    registration = event_loop_epoll_find_free_registration(set);
    if (!registration) {
        errno = ENOSPC;
        return -1;
    }

    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(set->epoll_fd, EPOLL_CTL_ADD, fd, &ev) != 0)
        return -1;

    registration->owner = owner;
    registration->generation = generation;
    registration->events = events;
    registration->seen_epoch = set->reconcile_epoch;
    registration->fd = fd;
    registration->active = 1;
    set->registered_fds++;

    return 0;
}

int event_loop_epoll_reconcile_end(EventLoopEpollSet *set)
{
    int saved_errno = 0;

    if (!set || set->epoll_fd < 0 || !set->reconcile_active) {
        errno = EINVAL;
        return -1;
    }

    for (int i = 0; i < EVENT_LOOP_EPOLL_MAX_REGISTERED_FDS; i++) {
        EventLoopEpollRegistration *registration = &set->registrations[i];

        if (!registration->active ||
            !registration->owner ||
            registration->seen_epoch == set->reconcile_epoch)
            continue;

        if (!event_loop_epoll_fd_seen_current(set, registration->fd, i) &&
            registration->fd >= 0 &&
            epoll_ctl(set->epoll_fd, EPOLL_CTL_DEL,
                      registration->fd, NULL) != 0 &&
            errno != EBADF &&
            errno != ENOENT &&
            saved_errno == 0) {
            saved_errno = errno;
        }

        event_loop_epoll_clear_registration(registration);
        if (set->registered_fds > 0)
            set->registered_fds--;
    }

    set->reconcile_active = 0;

    if (saved_errno != 0) {
        errno = saved_errno;
        return -1;
    }

    return 0;
}

int event_loop_epoll_has_registered_fds(const EventLoopEpollSet *set)
{
    return set && set->registered_fds > 0;
}

static int event_loop_epoll_ready_fd_events(const EventLoopEpollReadySet *ready,
                                             int fd,
                                             uint32_t events)
{
    if (!ready || fd < 0)
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
    if (!ready || fd < 0)
        return 0;

    for (int i = 0; i < ready->count; i++) {
        if (ready->events[i].data.fd == fd)
            return 1;
    }

    return 0;
}

int event_loop_epoll_ready_fd_read(const EventLoopEpollReadySet *ready, int fd)
{
    return event_loop_epoll_ready_fd_events(ready, fd,
                                            EPOLLIN | EPOLLHUP | EPOLLERR);
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

    if (!set || !ready || set->epoll_fd < 0) {
        errno = EINVAL;
        return -1;
    }

    timeout_ms = timeout_usec / 1000;
    if (timeout_usec > 0 && timeout_ms == 0)
        timeout_ms = 1;

    ready->count = epoll_wait(set->epoll_fd,
                              ready->events,
                              EVENT_LOOP_EPOLL_MAX_EVENTS,
                              timeout_ms);
    return ready->count;
}

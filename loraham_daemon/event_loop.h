#ifndef LORAHAM_EVENT_LOOP_H
#define LORAHAM_EVENT_LOOP_H

#include "event_loop_epoll.h"

/* --- epoll-only event loop --- */
#define EVENT_LOOP_EVENT_READ  EPOLLIN
#define EVENT_LOOP_EVENT_WRITE EPOLLOUT

typedef enum {
    EVENT_LOOP_BACKEND_EPOLL = 1
} EventLoopBackend;

typedef struct {
    EventLoopBackend backend;
    EventLoopEpollSet epoll_backend;
    int registration_errno;
} EventLoopSet;

typedef struct {
    EventLoopBackend backend;
    EventLoopEpollReadySet epoll_ready;
} EventLoopReadySet;

int event_loop_init(EventLoopSet *set);
void event_loop_close(EventLoopSet *set);
EventLoopBackend event_loop_backend(const EventLoopSet *set);
const char *event_loop_backend_name(EventLoopBackend backend);
void event_loop_reset(EventLoopSet *set);
void event_loop_add_fd(EventLoopSet *set, int fd);
void event_loop_add_fd_events(EventLoopSet *set, int fd, uint32_t events);
void event_loop_reconcile_begin(EventLoopSet *set);
void event_loop_reconcile_fd(EventLoopSet *set,
                             const void *owner,
                             uint32_t generation,
                             int fd,
                             uint32_t events);
void event_loop_reconcile_end(EventLoopSet *set);
int event_loop_registration_failed(const EventLoopSet *set);
int event_loop_has_registered_fds(const EventLoopSet *set);
int event_loop_ready_fd(const EventLoopReadySet *ready, int fd);
int event_loop_ready_fd_read(const EventLoopReadySet *ready, int fd);
int event_loop_ready_fd_write(const EventLoopReadySet *ready, int fd);
int event_loop_wait(const EventLoopSet *set,
                    EventLoopReadySet *ready,
                    int timeout_usec);

#endif

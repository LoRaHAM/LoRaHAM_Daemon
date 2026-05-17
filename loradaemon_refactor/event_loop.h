#ifndef LORAHAM_EVENT_LOOP_H
#define LORAHAM_EVENT_LOOP_H

#include "event_loop_select.h"
#include "event_loop_epoll.h"

/* --- backend-neutral event loop --- */

typedef enum {
    EVENT_LOOP_BACKEND_SELECT = 0,
    EVENT_LOOP_BACKEND_EPOLL = 1
} EventLoopBackend;

typedef struct {
    EventLoopBackend backend;
    EventLoopSelectSet select_backend;
    EventLoopEpollSet epoll_backend;
} EventLoopSet;

typedef struct {
    EventLoopBackend backend;
    EventLoopSelectReadySet select_ready;
    EventLoopEpollReadySet epoll_ready;
} EventLoopReadySet;

void event_loop_init_select(EventLoopSet *set);
int event_loop_init_epoll(EventLoopSet *set);
int event_loop_init_default(EventLoopSet *set);
void event_loop_close(EventLoopSet *set);
EventLoopBackend event_loop_backend(const EventLoopSet *set);
void event_loop_reset(EventLoopSet *set);
void event_loop_add_fd(EventLoopSet *set, int fd);
int event_loop_has_fd(const EventLoopSet *set, int fd);
int event_loop_has_registered_fds(const EventLoopSet *set);
int event_loop_ready_fd(const EventLoopReadySet *ready, int fd);
int event_loop_wait(const EventLoopSet *set,
                    EventLoopReadySet *ready,
                    int timeout_usec);

#endif

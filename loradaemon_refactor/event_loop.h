#ifndef LORAHAM_EVENT_LOOP_H
#define LORAHAM_EVENT_LOOP_H

#include <sys/select.h>

/* --- select() fd-set wrapper --- */

typedef struct {
    fd_set readfds;
    int maxfd;
} EventLoopSelectSet;

typedef EventLoopSelectSet EventLoopSet;

typedef struct {
    fd_set readfds;
} EventLoopReadySet;

void event_loop_select_reset(EventLoopSelectSet *set);
void event_loop_select_add_fd(EventLoopSelectSet *set, int fd);
int event_loop_select_has_fd(const EventLoopSelectSet *set, int fd);
int event_loop_select_fd_limit(const EventLoopSelectSet *set);
int event_loop_select_ready_fd(const EventLoopReadySet *ready, int fd);
int event_loop_select_wait(const EventLoopSelectSet *set,
                           EventLoopReadySet *ready,
                           int timeout_usec);

void event_loop_reset(EventLoopSet *set);
void event_loop_add_fd(EventLoopSet *set, int fd);
int event_loop_has_fd(const EventLoopSet *set, int fd);
int event_loop_has_registered_fds(const EventLoopSet *set);
int event_loop_ready_fd(const EventLoopReadySet *ready, int fd);
int event_loop_wait(const EventLoopSet *set,
                    EventLoopReadySet *ready,
                    int timeout_usec);

#endif

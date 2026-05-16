#ifndef LORAHAM_EVENT_LOOP_H
#define LORAHAM_EVENT_LOOP_H

#include <sys/select.h>

/* --- select() fd-set wrapper --- */

typedef struct {
    fd_set readfds;
    int maxfd;
} EventLoopSelectSet;

void event_loop_select_reset(EventLoopSelectSet *set);
void event_loop_select_add_fd(EventLoopSelectSet *set, int fd);
int event_loop_select_has_fd(const EventLoopSelectSet *set, int fd);
int event_loop_select_wait(const EventLoopSelectSet *set,
                           fd_set *ready,
                           int timeout_usec);

#endif

#ifndef LORAHAM_EVENT_LOOP_EPOLL_H
#define LORAHAM_EVENT_LOOP_EPOLL_H

#include <stdint.h>
#include <sys/epoll.h>

/* --- epoll backend state --- */

#define EVENT_LOOP_EPOLL_MAX_EVENTS 64

typedef struct {
    int epoll_fd;
    int registered_fds;
} EventLoopEpollSet;

typedef struct {
    struct epoll_event events[EVENT_LOOP_EPOLL_MAX_EVENTS];
    int count;
} EventLoopEpollReadySet;

int event_loop_epoll_init(EventLoopEpollSet *set);
void event_loop_epoll_close(EventLoopEpollSet *set);
void event_loop_epoll_reset(EventLoopEpollSet *set);
int event_loop_epoll_add_fd(EventLoopEpollSet *set, int fd);
int event_loop_epoll_add_fd_events(EventLoopEpollSet *set, int fd, uint32_t events);
int event_loop_epoll_has_registered_fds(const EventLoopEpollSet *set);
int event_loop_epoll_ready_fd(const EventLoopEpollReadySet *ready, int fd);
int event_loop_epoll_ready_fd_read(const EventLoopEpollReadySet *ready, int fd);
int event_loop_epoll_ready_fd_write(const EventLoopEpollReadySet *ready, int fd);
int event_loop_epoll_wait(const EventLoopEpollSet *set,
                          EventLoopEpollReadySet *ready,
                          int timeout_usec);

#endif

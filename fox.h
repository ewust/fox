
#ifndef FOX_H
#define FOX_H

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <string.h>
#include "openflow.h"

struct fox_state;

struct handler_list {
    struct handler_list *next;
    void (*func)(struct fox_state *state,
                 void *payload);
};

struct fox_state {
    char                *name;
    struct event_base   *base;
    struct bufferevent  *controller_bev;
    struct event        *echo_timer;
    struct event        *echo_timeout;
    uint32_t            echo_period_ms;

    void (*controller_join_cb)(struct fox_state *state);

    struct handler_list *msg_handler[256];

    void                *user_ptr;
};

void cleanup_state(struct fox_state *state);

#endif

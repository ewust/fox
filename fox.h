
#ifndef FOX_H
#define FOX_H

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <string.h>
#include "openflow.h"

struct fox_state {
    char                *name;
    struct event_base   *base;
    struct bufferevent  *controller_bev;

    void (*controller_join_cb)(struct fox_state *state);

    void (*msg_handler[256])(struct fox_state *state,
                             void *payload);
};

void cleanup_state(struct fox_state *state);

#endif

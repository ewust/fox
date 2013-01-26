#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <event2/event.h>
#include <event2/bufferevent.h>
#include "fox.h"


int controller_connect(struct fox_state *state, char *switch_ip, 
                       uint16_t switch_port);

void controller_connect_cb(struct bufferevent *bev, short events,
                           void *user_data);

void controller_read_cb(struct bufferevent *bev, void *user_data);

void controller_handle_msg(struct fox_state *state, struct ofp_header *ofhdr,
                           char *payload);

void controller_register_handler(struct fox_state *state, uint8_t type, 
                                void (*func)(struct fox_state *state, 
                                             struct ofp_header *ofhdr,
                                             char *payload));

void controller_send_hello(struct fox_state *state);

#endif

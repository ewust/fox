#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <event2/event.h>
#include <event2/bufferevent.h>
#include "fox.h"


void controller_init_echo(struct fox_state *state);

int controller_connect(struct fox_state *state, char *switch_ip, 
                       uint16_t switch_port);

void controller_connect_cb(struct bufferevent *bev, short events,
                           void *user_data);

void controller_read_cb(struct bufferevent *bev, void *user_data);

void controller_handle_msg(struct fox_state *state, struct ofp_header *ofhdr,
                           void *payload);

void controller_handle_error_msg(struct fox_state *state,
                                 struct ofp_error_msg *err_msg);

void controller_handle_features(struct fox_state *state,
                                struct ofp_switch_features *features);

void controller_register_handler(struct fox_state *state, uint8_t type, 
                                void (*func)(struct fox_state *state, 
                                             void *payload));

void controller_send_hello(struct fox_state *state);

void controller_send_echo_request(struct fox_state *state);

void controller_send_features_request(struct fox_state *state);

void controller_handle_echo_reply(struct fox_state *state);

#endif

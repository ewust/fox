#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include "fox.h"
#include "controller.h"
#include "telex.h"

void telex_handle_mod_flow(struct telex_state *state,
                           struct telex_mod_flow *flow)
{
    // Pass
    char src_ip[INET_ADDRSTRLEN];
    char dst_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &flow->src_ip, src_ip, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &flow->dst_ip, dst_ip, INET_ADDRSTRLEN);

    LogInfo(state->name, "block command: %s:%d <-> %s:%d",
            src_ip, ntohs(flow->src_port), dst_ip, ntohs(flow->dst_port));
}

void telex_read_cb(struct bufferevent *bev, void *ctx)
{
    struct telex_state *state = ctx;
    struct evbuffer *input;

    input = bufferevent_get_input(bev);

    while (evbuffer_get_length(input) > 0) {
        size_t buf_len;
        struct telex_mod_flow flow;
        buf_len = evbuffer_get_length(input);

        if (buf_len < sizeof(flow)) {
            return;
        }

        evbuffer_remove(input, &flow, sizeof(flow));

        telex_handle_mod_flow(state, &flow);
    }
}

void telex_accept_cb(struct evconnlistener *listener,
                     evutil_socket_t fd, struct sockaddr *address,
                     int socklen, void *ctx)
{
    struct telex_state *state = ctx;

    struct bufferevent *bev = bufferevent_socket_new(
                state->base, fd, BEV_OPT_CLOSE_ON_FREE);
    struct sockaddr_in *sin = (struct sockaddr_in *)address;
    char src_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sin->sin_addr.s_addr, src_ip, INET_ADDRSTRLEN);
    LogDebug(state->name, "Received connection: %s:%d",
             src_ip, ntohs(sin->sin_port));
    // TODO: add error event CB
    bufferevent_setcb(bev, telex_read_cb, NULL, NULL, state);
    bufferevent_enable(bev, EV_READ);
}


int telex_init_listener(struct telex_state *state)
{
    struct sockaddr_in sin;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr("127.0.0.1");
    sin.sin_port = htons(2603);

    state->listener = evconnlistener_new_bind(state->base, 
                            telex_accept_cb, state,
                            LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1,
                            (struct sockaddr*)&sin, sizeof(sin));
    if (!state->listener) {
        LogError("telex", "Error binding");
        return -1;
    }
    // TODO: accept err cb
    //evconnlistener_set_error_cb(state->listener, accept_error_cb);

    return 0;
}

/* TODO: take configuration */
int telex_init(struct event_base *base)
{ 
    struct fox_state *controller;
    struct telex_state *state;

    state = malloc(sizeof(*state));
    if (state == NULL) {
        LogError("telex", "Unable to malloc %d bytes", sizeof(*state));
        return -1;
    }
    memset(state, 0, sizeof(*state));

    state->base = base;
    state->name = "Telex";

    state->controllers[0] = controller_new(base, "10.1.0.1", 6633, 30*1000);
    if (state->controllers[0] == NULL) {
        return -1;
    }
    state->controllers[0]->user_ptr = state;
    // TODO: register handlers for some openflow callbacks
    //controller_register_handler(state, OFPT_ECHO_REPLY, echo_cb);

    if (telex_init_listener(state)) {
        return -1;
    }

    return 0; 
}

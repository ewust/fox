#include <event2/event.h>
#include <event2/bufferevent.h>
#include <assert.h>
#include <stdlib.h>
#include "fox.h"
#include "controller.h"
#include "logger.h"
#include "openflow.h"



/*
* Create a bufferevent for connecting to a controller
*/
int controller_connect(struct fox_state *state, char *switch_ip,
                       uint16_t switch_port)
{
    struct sockaddr_in sin;

    state->controller_bev = bufferevent_socket_new(state->base, -1,
        BEV_OPT_CLOSE_ON_FREE|BEV_OPT_DEFER_CALLBACKS);
    if (!state->controller_bev) {
        LogError(state->name, "Could not create remote bufferevent socket");
        return -1;
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr(switch_ip);
    sin.sin_port = htons(switch_port);

    bufferevent_setcb(state->controller_bev, controller_read_cb, NULL, controller_connect_cb, state);

    if (bufferevent_socket_connect(state->controller_bev,
        (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        /* Error starting connection */
        LogError(state->name, "Error starting connectiong");
        cleanup_state(state);
        return -1;
    }

    return 0;
}   

/*
*/
void controller_connect_cb(struct bufferevent *bev, short events, void *user_data)
{
    struct fox_state *state = user_data;
    
    assert(state->controller_bev == bev);

    if (events & BEV_EVENT_CONNECTED) {
        LogInfo(state->name, "Connected to controller");

        bufferevent_enable(state->controller_bev, EV_READ);

        controller_send_hello(state);

    } else if (events & BEV_EVENT_ERROR) {
        LogError(state->name, "Error connecting to controller");
    } else {
        LogError(state->name, "Unknown event %d", events);
    }

    // Or is a join only after you get data/stats from it?
    if (state->controller_join_cb) {
        state->controller_join_cb(state);
    }
}

void controller_read_cb(struct bufferevent *bev, void *user_data)
{
    struct fox_state *state = user_data;
    struct evbuffer *buf;
    size_t buf_len;
    struct ofp_header ofhdr;
    char *payload = NULL;

    assert(state->controller_bev == bev);

    buf = bufferevent_get_input(bev);  
    buf_len = evbuffer_get_length(buf);

    LogTrace(state->name, "Received %d bytes...", buf_len);

    /* Must have at least one header's worth before we'll read */
    if (buf_len < sizeof(ofhdr)) {
        return;
    }

    evbuffer_copyout(buf, &ofhdr, sizeof(ofhdr));

    LogTrace(state->name, "Header tells us we want %d bytes", ntohs(ofhdr.length));
    /* Check if we've received the whole message */
    if (buf_len < ntohs(ofhdr.length)) {
        return;
    }

    LogTrace(state->name, "Received message type %d", ofhdr.type);

    payload = malloc(ntohs(ofhdr.length));
    if (payload == NULL) {
        LogError(state->name, "Error: could not malloc %d bytes",
                 ntohs(ofhdr.length) - sizeof(ofhdr));
        return;
    }

    evbuffer_drain(buf, sizeof(ofhdr));
    evbuffer_remove(buf, payload, ntohs(ofhdr.length) - sizeof(ofhdr));

    controller_handle_msg(state, &ofhdr, payload);

    free(payload);
}

void controller_handle_msg(struct fox_state *state, struct ofp_header *ofhdr,
                           void *payload)
{
    switch (ofhdr->type) {
    case OFPT_HELLO:
        LogTrace(state->name, "Received hello message");
        controller_send_echo_request(state);
        break;
    case OFPT_ECHO_REQUEST:
        LogInfo(state->name, "Echo request");
        break;
    case OFPT_ECHO_REPLY:
        LogInfo(state->name, "Echo reply");
        break;
    case OFPT_ERROR:
        controller_handle_error_msg(state, payload);
        break; 
    default:
        LogInfo(state->name, "Unknown/unimplemented type %d", ofhdr->type);
        break;
    }

    /* Issue user callback if they want it */
    if (state->msg_handler[ofhdr->type] != NULL) {
        state->msg_handler[ofhdr->type](state, payload);
    }
  
}

void controller_handle_error_msg(struct fox_state *state,
                                 struct ofp_error_msg *err_msg)
{
    LogError(state->name, "Error type %d code %d", err_msg->type,
             err_msg->code);
}

/* TODO: make this a list
*/
void controller_register_handler(struct fox_state *state, uint8_t type, 
                                void (*func)(struct fox_state *state, 
                                             void *payload))
{
    state->msg_handler[type] = func;
}

void controller_send_hello(struct fox_state *state)
{
    struct ofp_hello hello_msg;
    hello_msg.header.version = OFP_VERSION;
    hello_msg.header.type = OFPT_HELLO;
    hello_msg.header.length = htons(sizeof(hello_msg));
    hello_msg.header.xid = htonl(0); 

    bufferevent_write(state->controller_bev, &hello_msg, sizeof(hello_msg)); 
}

void controller_send_echo_request(struct fox_state *state)
{
    struct ofp_header echo_req;
    echo_req.version = OFP_VERSION;
    echo_req.type = OFPT_ECHO_REQUEST;
    echo_req.length = htons(sizeof(echo_req));
    echo_req.xid = htonl(0);

    bufferevent_write(state->controller_bev, &echo_req, sizeof(echo_req));
}

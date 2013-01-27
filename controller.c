#include <event2/event.h>
#include <event2/bufferevent.h>
#include <assert.h>
#include <stdlib.h>
#include "fox.h"
#include "controller.h"
#include "logger.h"
#include "openflow.h"


/* only supports connecting to a controller.
* TODO: support listen and SSL
*/
struct fox_state *controller_new(struct event_base *base, char *ip,
                                 uint16_t port, uint32_t echo_period_ms)
{
    struct fox_state *state;

    state = malloc(sizeof(*state));
    if (state == NULL) {
        LogError("controller", "Could not malloc state");
        return NULL;
    }
    memset(state, 0, sizeof(*state));

    state->echo_period_ms = echo_period_ms;
    state->name = ip;
    state->base = base;

    controller_init_echo(state);

    if (controller_connect(state, ip, port)) {
        return NULL;
    }

    return state;
}

void controller_echo_timeout(evutil_socket_t fd, short what, void *arg)
{
    struct fox_state *state = arg;

    LogError(state->name, "Timout on echo request");
}


void controller_echo_cb(evutil_socket_t fd, short what, void *arg)
{
    struct fox_state *state = arg;
    struct timeval tv = {1, 0};

    LogDebug(state->name, "sending echo request");

    controller_send_echo_request(state);

    /* Setup a timeout on the response */
    evtimer_add(state->echo_timeout, &tv); 

}

void controller_init_echo(struct fox_state *state)
{
    struct timeval tv;

    tv.tv_sec = state->echo_period_ms / 1000;
    tv.tv_usec = (state->echo_period_ms % 1000) * 1000;

    state->echo_timer = evtimer_new(state->base, controller_echo_cb, state);
    state->echo_timeout = evtimer_new(state->base, controller_echo_timeout, state);

    evtimer_add(state->echo_timer, &tv);
}

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

    while (evbuffer_get_length(buf) > 0) {
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
                     ntohs(ofhdr.length));
            return;
        }

        evbuffer_remove(buf, payload, ntohs(ofhdr.length));

        controller_handle_msg(state, &ofhdr, payload);

        free(payload);
    }
}

void controller_handle_msg(struct fox_state *state, struct ofp_header *ofhdr,
                           void *payload)
{
    switch (ofhdr->type) {
    case OFPT_HELLO:
        LogDebug(state->name, "Received hello message");
        controller_send_echo_request(state);
        controller_send_features_request(state);
        break;
    case OFPT_ECHO_REQUEST:
        LogDebug(state->name, "Echo request");
        break;
    case OFPT_ECHO_REPLY:
        LogDebug(state->name, "Echo reply");
        controller_handle_echo_reply(state);
        break;
    case OFPT_FEATURES_REPLY:
        LogDebug(state->name, "Feature reply");
        controller_handle_features(state, payload);
        break;

    case OFPT_ERROR:
        controller_handle_error_msg(state, payload);
        break; 
    default:
        LogWarn(state->name, "Unknown/unimplemented type %d", ofhdr->type);
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
    LogError(state->name, "Error type %d code %d", ntohs(err_msg->type),
             ntohs(err_msg->code));
}

int get_port_speed(uint32_t port_feature)
{
    if (port_feature & (OFPPF_10MB_HD | OFPPF_10MB_FD))
        return 10;
    else if (port_feature & (OFPPF_100MB_HD | OFPPF_100MB_FD))
        return 100;
    else if (port_feature & (OFPPF_1GB_HD | OFPPF_1GB_FD))
        return 1000;
    else if (port_feature & (OFPPF_10GB_FD))
        return 10000;
    else
        return -1;
}

void controller_handle_features(struct fox_state *state,
                                struct ofp_switch_features *features)
{
    size_t num_ports;
    int i;

    LogTrace(state->name, "header type: %d", features->header.type);

    assert(features->header.type == OFPT_FEATURES_REPLY);

    LogInfo(state->name, "%016x Features:", features->datapath_id);
    LogInfo(state->name, "  max buffer size: %d pkts",
            ntohl(features->n_buffers));
    LogInfo(state->name, "  tables         : %d",
            features->n_tables);
    LogInfo(state->name, "  capabilities   : %08x",
            ntohl(features->capabilities));
    LogInfo(state->name, "  actions        : %08x", 
            ntohl(features->actions));
    
    num_ports = ntohs(features->header.length) - sizeof(*features);
    assert((num_ports % sizeof(struct ofp_phy_port)) == 0);
    num_ports /= sizeof(struct ofp_phy_port);
 
    for (i=0; i<num_ports; i++) {
        struct ofp_phy_port *port = &features->ports[i];
        int speed = get_port_speed(ntohl(port->curr));
        LogInfo(state->name, "  Port %s: %d mbps",
                port->name, speed);
    }
}

/* TODO: make this a list
*/
void controller_register_handler(struct fox_state *state, uint8_t type, 
                                void (*func)(struct fox_state *state, 
                                             void *payload))
{
    state->msg_handler[type] = func;
}

void controller_send_hdr(struct fox_state *state, void *payload, size_t len)
{
    struct ofp_header *hdr = payload;
    hdr->version = OFP_VERSION;
    hdr->length = htons(len);
    hdr->xid = htonl(0);

    // TODO: check bufferevent_write return value
    bufferevent_write(state->controller_bev, payload, len);
}

void controller_send_hello(struct fox_state *state)
{
    struct ofp_hello hello_msg;
    hello_msg.header.type = OFPT_HELLO;

    controller_send_hdr(state, &hello_msg, sizeof(hello_msg));
}

void controller_send_echo_request(struct fox_state *state)
{
    struct ofp_header echo_req;
    echo_req.type = OFPT_ECHO_REQUEST;

    controller_send_hdr(state, &echo_req, sizeof(echo_req));
}

void controller_send_features_request(struct fox_state *state)
{
    struct ofp_header feature_req;
    feature_req.type = OFPT_FEATURES_REQUEST;

    controller_send_hdr(state, &feature_req, sizeof(feature_req));
}

/* TODO: check xid */
void controller_handle_echo_reply(struct fox_state *state)
{
    struct timeval tv;

    LogDebug(state->name, "Got echo reply, ms: %d", state->echo_period_ms);

    tv.tv_sec = state->echo_period_ms / 1000;
    tv.tv_usec = (state->echo_period_ms % 1000) * 1000;

    evtimer_del(state->echo_timeout);

    evtimer_add(state->echo_timer, &tv);
}

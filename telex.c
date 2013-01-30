#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include <assert.h>
#include <linux/if_ether.h>
#include "fox.h"
#include "controller.h"
#include "telex.h"

void telex_generate_mod_flow(struct telex_state *state, uint32_t src_ip,
                             uint32_t dst_ip, uint16_t src_port,
                             uint16_t dst_port, int add)
{
    struct ofp_flow_mod *ofmod;

    size_t mod_len = sizeof(*ofmod);

    ofmod = malloc(sizeof(*ofmod) + sizeof(struct ofp_action_output));

    memset(ofmod, 0, sizeof(ofmod));

    ofmod->header.type = OFPT_FLOW_MOD;
    ofmod->match.wildcards = htonl(OFPFW_ALL & 
                                   ~OFPFW_NW_SRC_MASK & ~OFPFW_NW_DST_MASK &
                                   ~OFPFW_TP_SRC & ~OFPFW_TP_DST &
                                   ~OFPFW_NW_PROTO & ~OFPFW_DL_TYPE);
    ofmod->match.in_port = htons(0);
    ofmod->match.dl_type = htons(ETH_P_IP); 
    ofmod->match.nw_src = src_ip;
    ofmod->match.nw_dst = dst_ip;
    ofmod->match.nw_proto = IPPROTO_TCP;
    ofmod->match.tp_src = src_port;
    ofmod->match.tp_dst = dst_port;

    ofmod->buffer_id = htonl(UINT32_MAX);
    ofmod->idle_timeout = htons(TELEX_IDLE_FLOW_TIMEOUT);
    ofmod->hard_timeout = htons(OFP_FLOW_PERMANENT);
    ofmod->priority = htons(OFP_DEFAULT_PRIORITY + 100);
    ofmod->flags = htons(OFPFF_SEND_FLOW_REM);
    ofmod->out_port = htons(OFPP_NONE);

    if (add) {
        struct ofp_action_output *action;
        
        mod_len += sizeof(*action);

        action = (struct ofp_action_output *)&ofmod->actions[0];
        memset(action, 0, sizeof(*action));
        action->type = htons(OFPAT_OUTPUT);
        action->len = htons(sizeof(*action));
        action->max_len = htons(1500); // MTU?
        action->port = htons(OFPP_CONTROLLER);

        ofmod->command = htons(OFPFC_ADD);
    } else {
        ofmod->command = htons(OFPFC_DELETE);
    }

    LogDebug(state->name, "mod_len: %d", mod_len);

    controller_send_hdr(state->controllers[0], ofmod, mod_len);
}

void telex_handle_mod_flow(struct telex_state *state,
                           struct telex_mod_flow *flow)
{
    char src_ip[INET_ADDRSTRLEN];
    char dst_ip[INET_ADDRSTRLEN];

    inet_ntop(AF_INET, &flow->src_ip, src_ip, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &flow->dst_ip, dst_ip, INET_ADDRSTRLEN);

    LogDebug(state->name, "block command(%d): %s:%d <-> %s:%d",
             flow->action, src_ip, ntohs(flow->src_port), dst_ip,
             ntohs(flow->dst_port));
 
    telex_generate_mod_flow(state, flow->src_ip, flow->dst_ip,
                            flow->src_port, flow->dst_port,
                            flow->action == TELEX_MOD_BLOCK ||
                            flow->action == TELEX_MOD_BLOCK_BIDIRECTIONAL);
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

void telex_error_cb(struct bufferevent *bev, short events, void *ctx)
{
    struct telex_state *state = ctx;
    evutil_socket_t fd = bufferevent_getfd(bev);
    struct sockaddr_in sin;
    socklen_t sin_size = sizeof(sin);
    char src_ip[INET_ADDRSTRLEN];

    assert(state != NULL);

    if (getpeername(fd, (struct sockaddr *)&sin, &sin_size) != 0) {
        LogError(state->name, "(%d) Could not getsockname for fd %d",
                 errno, fd);
        perror("   ");
        return;
    }

    inet_ntop(sin.sin_family, &sin.sin_addr, src_ip, INET_ADDRSTRLEN);

    if (events & BEV_EVENT_ERROR) {
        LogError(state->name, "Error from bufferevent (%s:%d):",
                 src_ip, ntohs(sin.sin_port));
        perror("    ");
    }
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        LogDebug(state->name, "%s:%d disconnected", src_ip,
                ntohs(sin.sin_port));
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
    LogDebug(state->name, "%s:%d connected",
             src_ip, ntohs(sin->sin_port));

    bufferevent_setcb(bev, telex_read_cb, NULL, telex_error_cb, state);
    bufferevent_enable(bev, EV_READ);
}

void telex_accept_error_cb(struct evconnlistener *listener, void *ctx)
{
    struct event_base *base = evconnlistener_get_base(listener);
    int err = EVUTIL_SOCKET_ERROR();

    LogError("telex", "Accept error %d (%s)", err,
             evutil_socket_error_to_string(err));
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

    evconnlistener_set_error_cb(state->listener, telex_accept_error_cb);

    return 0;
}

void telex_flow_removed_cb(struct fox_state *state, void *payload)
{
    struct ofp_flow_removed *removed = payload;
    char src_ip[INET_ADDRSTRLEN];
    char dst_ip[INET_ADDRSTRLEN];
    char *reason;

    inet_ntop(AF_INET, &removed->match.nw_src, src_ip, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &removed->match.nw_dst, dst_ip, INET_ADDRSTRLEN);
    
    switch (removed->reason) {
    case OFPRR_IDLE_TIMEOUT:    reason = "idle timeout"; break;
    case OFPRR_HARD_TIMEOUT:    reason = "hard timeout"; break;
    case OFPRR_DELETE:          reason = "delete";       break;
    default:                    reason = "unknown";
    }

    LogInfo(state->name, "Flow removed: %s:%d -> %s:%d reason: %s (%d)", 
            src_ip, ntohs(removed->match.tp_src),
            dst_ip, ntohs(removed->match.tp_dst), reason, removed->reason);
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

    state->controllers[0] = controller_new(base, 
                                    "10.1.0.1", 6633, 90*1000, 1);
    state->controllers[1] = controller_new(base, 
                                    "10.1.0.5", 6633, 90*1000, 0);
    if (state->controllers[0] == NULL || state->controllers[1] == NULL) {
        return -1;
    }
    state->controllers[0]->user_ptr = state;
    state->controllers[1]->user_ptr = state;

    /* Openflow only sends flow removed by connecting to us, nevermind that
     * we already have a connection open with them. */
    controller_register_handler(state->controllers[1], OFPT_FLOW_REMOVED,
                                telex_flow_removed_cb);


    if (telex_init_listener(state)) {
        return -1;
    }

    return 0; 
}

#ifndef TELEX_H
#define TELEX_H

#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include "fox.h"

#define MAX_SWITCHES    10

struct telex_state {
    char                    *name;
    struct event_base       *base;
    struct evconnlistener   *listener;
    struct fox_state        *controllers[MAX_SWITCHES]; 
};

#define TELEX_MOD_BLOCK               0x01
#define TELEX_MOD_UNBLOCK             0x02
#define TELEX_MOD_BLOCK_BIDIRECTIONAL 0x03
#define TELEX_MOD_UNBLOCK_BIDIRECTIONAL 0x04

#define TELEX_IDLE_FLOW_TIMEOUT         15*60

struct telex_mod_flow 
{
  uint8_t     action;  
  uint32_t    src_ip;
  uint32_t    dst_ip;
  uint16_t    src_port;
  uint16_t    dst_port;
} __attribute__((__packed__));

#endif

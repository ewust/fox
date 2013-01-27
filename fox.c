#include <event2/event.h>
#include <stdio.h>
#include <stdlib.h>
#include "fox.h"
#include "controller.h"
#include "logger.h"


void cleanup_state(struct fox_state *state)
{
    if (state->controller_bev) {
        bufferevent_free(state->controller_bev);
    }
}

void echo_cb(struct fox_state *state, void *payload)
{
    LogInfo(state->name, "main got an echo callback!");
}

int main(char *argv[], int argc)
{
    struct event_base *base;
    struct fox_state *state;

    LogOutputStream(stdout);
    LogOutputLevel(LOG_TRACE);

    base = event_base_new();

    state = controller_new(base, "10.1.0.1", 6633, 30*1000);
   
    event_base_dispatch(state->base);
    

    return 0;
}

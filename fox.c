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

int main(char *argv[], int argc)
{
    struct event_base *base;
    struct fox_state *state;

    LogOutputStream(stdout);
    LogOutputLevel(LOG_TRACE);

    state = malloc(sizeof(*state));
    if (state == NULL) {
        LogError("main", "Could not malloc state");
        return -1;
    }
    memset(state, 0, sizeof(*state));

    
    state->name = "test";
    state->base = event_base_new();

    if (controller_connect(state, "10.1.0.1", 6633)) {
        return -1;
    }
    
    event_base_dispatch(state->base);
    

    return 0;
}

fox
===

Lightweight Openflow Controller, built on top of libevent.

Why?
---

Sure, there's nox/pox, Beacon, Maestro, but those can be either complicated,
written in non-performant languages, or both. 

What I really wanted was a simple tool that got out of my way and pushed
the logic for the applications to the application. Writing an application,
especially once you understand OpenFlow, should be fairly simple and
straightforward. Fox does not provide all the complicated features provided
by other OpenFlow controllers, but it does allow you to get up and running
(hopefully performantly!) in a matter of hours.


Using fox
---------

create an app that uses fox by:
1. Creating a new a c file (app.c) that has an app\_init function that 
    takes a struct event_base *base and returns TK
2. Initialize whatever event-driven stuff your app needs, create an openflow
    controller and register callbacks with it using controller_new and 
    controller_register_cb.
3. Add your app\_init in the main function in fox.c

TODO: clean up this process:
    define app_init(base) in whatever apps/your_app.c
    make (your_app) && ./your_app


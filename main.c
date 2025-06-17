#include "eventq.h"

int CALL main(int argc, char **argv) {
    platform_init();

    app_state state1 = {0}, state2 = {0};
    eventq_start(&state1);
    eventq_start(&state2);

    platform_sleep(1000);
    eventq_enqueue(&state1.queue, &state1.echo_sqe, APP_EVENT_TYPE_ECHO, NULL, 0);
    eventq_enqueue(&state1.queue, &state1.timer_sqe, APP_EVENT_TYPE_START_TIMER, NULL, 100);

    platform_sleep(1000);
    eventq_enqueue(&state1.queue, &state1.echo_sqe, APP_EVENT_TYPE_ECHO, NULL, 0);

    eventq_enqueue(&state1.queue, &state1.create_socket_sqe, APP_EVENT_TYPE_CREATE_SOCKET, NULL, 1);
    platform_sleep(100);
    eventq_enqueue(&state2.queue, &state2.create_socket_sqe, APP_EVENT_TYPE_CREATE_SOCKET, NULL, 0);
    platform_sleep(1000);

    eventq_stop(&state1);
    eventq_stop(&state2);

    return 0;
}

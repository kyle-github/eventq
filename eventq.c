/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

//
// Minimal platform abstractions.
//

#ifdef _WIN32
#include <windows.h>
#define CALL __cdecl
typedef HANDLE platform_thread;
#define PLATFORM_THREAD(FuncName, CtxVarName) DWORD WINAPI FuncName(_In_ void* CtxVarName)
#define PLATFORM_THREAD_RETURN(Status) return (DWORD)(Status)
void platform_thread_create(platform_thread* thread, LPTHREAD_START_ROUTINE func, void* ctx) {
    *thread = CreateThread(NULL, 0, func, ctx, 0, NULL);
}
void platform_thread_destroy(platform_thread thread) {
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
}
#else
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#define CALL
typedef pthread_t platform_thread;
#define PLATFORM_THREAD(FuncName, CtxVarName) void* FuncName(void* CtxVarName)
#define PLATFORM_THREAD_RETURN(Status) return NULL
void platform_thread_create(platform_thread* thread, void *(*func)(void *), void* ctx) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_create(thread, &attr, func, ctx);
    pthread_attr_destroy(&attr);
}
void platform_thread_destroy(platform_thread thread) {
    pthread_equal(thread, pthread_self());
    pthread_join(thread, NULL);
}
#endif

//
// Different implementations of the event queue interface.
//

#if EC_IOCP

typedef HANDLE eventq;
typedef struct eventq_sqe {
    OVERLAPPED;
    uint32_t type;
} eventq_sqe;
typedef OVERLAPPED_ENTRY eventq_cqe;

bool eventq_initialize(eventq* queue) {
    *queue = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    return *queue != NULL;
}
void eventq_cleanup(eventq queue) {
    CloseHandle(queue);
}
bool eventq_sqe_initialize(eventq queue, eventq_sqe* sqe) {
    return true;
}
void eventq_sqe_cleanup(eventq queue, eventq_sqe* sqe) {
}
void eventq_enqueue(eventq queue, eventq_sqe* sqe, uint32_t type, void* user_data, uint32_t status) {
    memset(sqe, 0, sizeof(*sqe));
    sqe->type = type;
    PostQueuedCompletionStatus(queue, status, (ULONG_PTR)user_data, (OVERLAPPED*)sqe);
}
uint32_t eventq_dequeue(eventq queue, eventq_cqe* events, uint32_t count, uint32_t wait_time) {
    uint32_t out_count;
    GetQueuedCompletionStatusEx(queue, events, count, &out_count, wait_time, FALSE); // TODO - How to handle errors?
    return out_count;
}
uint32_t eventq_cqe_get_type(eventq_cqe* cqe) {
    return ((eventq_sqe*)cqe->lpOverlapped)->type;
}
void* eventq_cqe_get_user_data(eventq_cqe* cqe) {
    return (void*)cqe->lpCompletionKey;
}
uint32_t eventq_cqe_get_status(eventq_cqe* cqe) {
    return cqe->dwNumberOfBytesTransferred;
}

#elif EC_IO_URING

#include <liburing.h>

typedef struct io_uring eventq;
typedef struct eventq_sqe {
    uint32_t type;
    void* user_data;
    uint32_t status;
} eventq_sqe;
typedef struct io_uring_cqe* eventq_cqe;

bool eventq_initialize(eventq* queue) {
    io_uring_queue_init(4, queue, 0); // TODO - Can this fail?
    return true;
}
void eventq_cleanup(eventq queue) {
    // TODO - How to cleanup?
}
bool eventq_sqe_initialize(eventq queue, eventq_sqe* sqe) {
}
void eventq_sqe_cleanup(eventq queue, eventq_sqe* sqe) {
}
void eventq_enqueue(eventq queue, eventq_sqe* sqe, uint32_t type, void* user_data, uint32_t status) {
    sqe->type = type;
    sqe->user_data = user_data;
    sqe->status = status;
    struct io_uring_sqe *io_sqe = io_uring_get_sqe(queue);
    io_uring_prep_nop(io_sqe); // TODO - Check args
    io_uring_sqe_set_data(io_sqe, sqe);
    io_uring_submit(queue); // TODO - Extract to separate function?
}
uint32_t eventq_dequeue(eventq queue, eventq_cqe* events, uint32_t count, uint32_t wait_time) {
    io_uring_wait_cqe(queue, events); // TODO - multiple return and wait_time
    return 1;
}
uint32_t eventq_cqe_get_type(eventq_cqe* cqe) {
    return ((eventq_sqe*)cqe->user_data)->type;
}
void* eventq_cqe_get_user_data(eventq_cqe* cqe) {
    return ((eventq_sqe*)cqe->user_data)->user_data;
}
uint32_t eventq_cqe_get_status(eventq_cqe* cqe) {
    return ((eventq_sqe*)cqe->user_data)->status;
}

#elif EC_EPOLL

#include <sys/epoll.h>
#include <sys/eventfd.h>

typedef int eventq;
typedef struct eventq_sqe {
    int fd;
    uint32_t type;
    void* user_data;
    uint32_t status;
} eventq_sqe;
typedef struct epoll_event eventq_cqe;

bool eventq_initialize(eventq* queue) {
    *queue = epoll_create1(EPOLL_CLOEXEC);
    return *queue != -1;
}
void eventq_cleanup(eventq queue) {
    close(queue);
}
bool eventq_sqe_initialize(eventq queue, eventq_sqe* sqe) {
    sqe->fd = eventfd(0, EFD_CLOEXEC);
    if (sqe->fd == -1) return false;
    struct epoll_event event = { .events = EPOLLIN | EPOLLET, .data = { .ptr = sqe } };
    if (epoll_ctl(queue, EPOLL_CTL_ADD, sqe->fd, &event) != 0) {
        close(sqe->fd);
        return false;
    }
    return true;
}
void eventq_sqe_cleanup(eventq queue, eventq_sqe* sqe) {
    epoll_ctl(queue, EPOLL_CTL_DEL, sqe->fd, NULL);
    close(sqe->fd);
}
void eventq_enqueue(eventq queue, eventq_sqe* sqe, uint32_t type, void* user_data, uint32_t status) {
    sqe->type = type;
    sqe->user_data = user_data;
    sqe->status = status;
    eventfd_write(sqe->fd, 1);
}
uint32_t eventq_dequeue(eventq queue, eventq_cqe* events, uint32_t count, uint32_t wait_time) {
    const int timeout = wait_time == UINT32_MAX ? -1 : (int)wait_time;
    int result;
    do {
        result = epoll_wait(queue, events, count, timeout);
    } while ((result == -1L) && (errno == EINTR));
    return (uint32_t)result;
}
uint32_t eventq_cqe_get_type(eventq_cqe* cqe) {
    return ((eventq_sqe*)cqe->data.ptr)->type;
}
void* eventq_cqe_get_user_data(eventq_cqe* cqe) {
    return ((eventq_sqe*)cqe->data.ptr)->user_data;
}
uint32_t eventq_cqe_get_status(eventq_cqe* cqe) {
    return ((eventq_sqe*)cqe->data.ptr)->status;
}

#elif EC_KQUEUE

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

typedef int eventq;
typedef struct eventq_sqe {
    uint32_t type;
    void* user_data;
    uint32_t status;
} eventq_sqe;
typedef struct kevent eventq_cqe;

bool eventq_initialize(eventq* queue) {
    *queue = kqueue();
    return *queue != -1;
}
void eventq_cleanup(eventq queue) {
    close(queue);
}
bool eventq_sqe_initialize(eventq queue, eventq_sqe* sqe) {
    return true;
}
void eventq_sqe_cleanup(eventq queue, eventq_sqe* sqe) {
}
void eventq_enqueue(eventq queue, eventq_sqe* sqe, uint32_t type, void* user_data, uint32_t status) {
    struct kevent event = {0};
    sqe->type = type;
    sqe->user_data = user_data;
    sqe->status = status;
    EV_SET(&event, queue, EVFILT_USER, EV_ADD | EV_CLEAR, NOTE_TRIGGER, 0, sqe);
    kevent(queue, &event, 1, NULL, 0, NULL);
}
#define CXPLAT_NANOSEC_PER_MS       (1000000)
#define CXPLAT_MS_PER_SECOND        (1000)
uint32_t eventq_dequeue(eventq queue, eventq_cqe* events, uint32_t count, uint32_t wait_time) {
    struct timespec timeout = {0, 0};
    if (wait_time != UINT32_MAX) {
        timeout.tv_sec += (wait_time / CXPLAT_MS_PER_SECOND);
        timeout.tv_nsec += ((wait_time % CXPLAT_MS_PER_SECOND) * CXPLAT_NANOSEC_PER_MS);
    }
    int result;
    do {
        result = kevent(queue, NULL, 0, events, count, wait_time == UINT32_MAX ? NULL : &timeout);
    } while ((result == -1L) && (errno == EINTR));
    return (uint32_t)result;
}
uint32_t eventq_cqe_get_type(eventq_cqe* cqe) {
    return ((eventq_sqe*)cqe->udata)->type;
}
void* eventq_cqe_get_user_data(eventq_cqe* cqe) {
    return ((eventq_sqe*)cqe->udata)->user_data;
}
uint32_t eventq_cqe_get_status(eventq_cqe* cqe) {
    return ((eventq_sqe*)cqe->udata)->status;
}

#else

#error "Must define an execution context"

#endif

//
// Minimal abstraction layer for the "platform" that represents all execution
// logic underneath the application.
//

#define APP_EVENT_TYPE_START 0x8000

uint32_t platform_wait_time = UINT32_MAX;
uint32_t platform_get_wait_time(void) { return platform_wait_time; }
void platform_process_timeout(void) { printf("Timeout\n"); platform_wait_time = UINT32_MAX; }
void platform_process_event(eventq_cqe* cqe) { printf("Event %u\n", eventq_cqe_get_type(cqe)); }
void platform_sleep(uint32_t milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000);
#endif
}

//
// Custom app specific functions and state.
//

bool running = true;
platform_thread thread;
eventq queue;
eventq_sqe shutdown_sqe;
eventq_sqe echo_sqe;
eventq_sqe timer_sqe;

#define APP_EVENT_TYPE_SHUTDOWN     (APP_EVENT_TYPE_START + 0)
#define APP_EVENT_TYPE_ECHO         (APP_EVENT_TYPE_START + 1)
#define APP_EVENT_TYPE_START_TIMER  (APP_EVENT_TYPE_START + 2)

void process_app_event(eventq_cqe* cqe) {
    switch (eventq_cqe_get_type(cqe)) {
    case APP_EVENT_TYPE_SHUTDOWN:
        printf("Shutdown event received\n");
        running = false;
        break;
    case APP_EVENT_TYPE_ECHO:
        printf("Echo event received\n");
        break;
    case APP_EVENT_TYPE_START_TIMER:
        platform_wait_time = eventq_cqe_get_status(cqe);
        printf("Starting %u ms timer\n", eventq_cqe_get_status(cqe));
        break;
    default:
        break;
    }
}

PLATFORM_THREAD(main_loop, context) {
    printf("Main loop start\n");
    eventq_cqe events[8];
    while (running) {
        uint32_t wait_time = platform_get_wait_time();
        uint32_t count = wait_time == 0 ? 0 : eventq_dequeue(queue, events, 8, wait_time);
        if (count == 0) {
            platform_process_timeout();
        } else {
            for (uint32_t i = 0; i < count; ++i) {
                if (eventq_cqe_get_type(&events[i]) < 0x8000) {
                    platform_process_event(&events[i]);
                } else {
                    process_app_event(&events[i]);
                }
            }
        }
    }
    printf("Main loop end\n");
    PLATFORM_THREAD_RETURN(0);
}

void start_main_loop() {
    printf("Starting main loop\n");
    eventq_initialize(&queue);
    eventq_sqe_initialize(queue, &shutdown_sqe);
    eventq_sqe_initialize(queue, &echo_sqe);
    eventq_sqe_initialize(queue, &timer_sqe);
    platform_thread_create(&thread, main_loop, NULL);
}

void stop_main_loop() {
    printf("Stopping main loop\n");
    eventq_enqueue(queue, &shutdown_sqe, APP_EVENT_TYPE_SHUTDOWN, NULL, 0);
    platform_thread_destroy(thread);
    eventq_sqe_cleanup(queue, &shutdown_sqe);
    eventq_sqe_cleanup(queue, &echo_sqe);
    eventq_sqe_cleanup(queue, &timer_sqe);
    eventq_cleanup(queue);
}

int CALL main(int argc, char **argv) {
    start_main_loop();

    platform_sleep(1000);
    printf("Sending echo event\n");
    eventq_enqueue(queue, &echo_sqe, APP_EVENT_TYPE_ECHO, NULL, 0);
    printf("Sending timer event\n");
    eventq_enqueue(queue, &timer_sqe, APP_EVENT_TYPE_START_TIMER, NULL, 100);

    platform_sleep(1000);
    printf("Sending echo event\n");
    eventq_enqueue(queue, &echo_sqe, APP_EVENT_TYPE_ECHO, NULL, 0);

    stop_main_loop();
    return 0;
}

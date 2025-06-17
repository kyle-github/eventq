/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>


#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
#include <WinSock2.h>
#include <windows.h>

#define CALL __cdecl

typedef HANDLE platform_thread;

typedef DWORD WINAPI (*thread_func_t)(_In_ void *ctx);

#define PLATFORM_THREAD(FuncName, CtxVarName) DWORD WINAPI FuncName(_In_ void *CtxVarName)
#define PLATFORM_THREAD_RETURN(Status) return (DWORD)(Status)



#else

typedef void *(*thread_func_t)(void *ctx);

#define CALL
extern

#endif

typedef enum PLATFORM_EVENT_TYPE {
    PLATFORM_EVENT_TYPE_SOCKET_RECEIVE,
    PLATFORM_EVENT_TYPE_SOCKET_SEND,
} PLATFORM_EVENT_TYPE;


typedef struct app_state app_state;


extern void eventq_start(app_state* state);
extern void eventq_stop(app_state* state);

/* threads */


extern void platform_thread_create(platform_thread *thread, thread_func_t func, void *ctx);
extern void platform_thread_destroy(platform_thread thread);

/* Misc platform wrappers */
extern void platform_sleep(uint32_t milliseconds);

// windowserver mach ipc protocol definitions
#pragma once
#ifndef _WINDOWSERVER_H_
#define _WINDOWSERVER_H_

#include <bootstrap.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WS_BOOTSTRAP_NAME       "com.apple.WindowServer"

#define WS_MSG_CREATE_WINDOW    5001
#define WS_MSG_UPDATE_WINDOW    5002
#define WS_MSG_DESTROY_WINDOW   5003
#define WS_MSG_SET_TITLE        5004

#define WS_EVENT_NONE           0
#define WS_EVENT_MOUSE_MOVE     1
#define WS_EVENT_MOUSE_DOWN     2
#define WS_EVENT_MOUSE_UP       3
#define WS_EVENT_KEY_DOWN       4
#define WS_EVENT_KEY_UP         5
#define WS_EVENT_WINDOW_CLOSE   6
#define WS_EVENT_WINDOW_FOCUS   7

#define WS_MAX_TITLE_LEN        64
#define WS_MAX_WINDOWS          16

typedef struct __attribute__((packed)) {
    unsigned int        msgh_bits;
    unsigned int        msgh_size;
    mach_port_t         msgh_remote_port;
    mach_port_t         msgh_local_port;
    mach_port_t         msgh_voucher_port;
    unsigned int        msgh_id;
    unsigned int        msgh_reserved1;
    unsigned int        msgh_reserved2;

    int                 width;
    int                 height;
    char                title[WS_MAX_TITLE_LEN];
    unsigned int        flags;
} ws_req_create_t;

typedef struct __attribute__((packed)) {
    unsigned int        msgh_bits;
    unsigned int        msgh_size;
    mach_port_t         msgh_remote_port;
    mach_port_t         msgh_local_port;
    mach_port_t         msgh_voucher_port;
    unsigned int        msgh_id;
    unsigned int        msgh_reserved1;
    unsigned int        msgh_reserved2;

    int                 ret_code;
    unsigned int        window_id;
    mach_port_t         event_port;
    unsigned long long  surface_addr;
} ws_rep_create_t;

typedef struct {
    unsigned int msgh_descriptor_count;
} mach_msg_body_t;

typedef struct __attribute__((packed)) {
    unsigned long long          address;
    unsigned char               deallocate;
    unsigned char               copy;
    unsigned char               pad1;
    unsigned char               type;
    unsigned int                size;
} mach_msg_ool_descriptor_t;

typedef struct __attribute__((packed)) {
    unsigned int        msgh_bits;
    unsigned int        msgh_size;
    mach_port_t         msgh_remote_port;
    mach_port_t         msgh_local_port;
    mach_port_t         msgh_voucher_port;
    unsigned int        msgh_id;
    unsigned int        msgh_reserved1;
    unsigned int        msgh_reserved2;

    unsigned int        window_id;
    int                 dirty_x;
    int                 dirty_y;
    int                 dirty_w;
    int                 dirty_h;
} ws_msg_update_t;

typedef struct __attribute__((packed)) {
    unsigned int        msgh_bits;
    unsigned int        msgh_size;
    mach_port_t         msgh_remote_port;
    mach_port_t         msgh_local_port;
    mach_port_t         msgh_voucher_port;
    unsigned int        msgh_id;
    unsigned int        msgh_reserved1;
    unsigned int        msgh_reserved2;

    unsigned int        window_id;
} ws_msg_destroy_t;

typedef struct __attribute__((packed)) {
    unsigned int        msgh_bits;
    unsigned int        msgh_size;
    mach_port_t         msgh_remote_port;
    mach_port_t         msgh_local_port;
    mach_port_t         msgh_voucher_port;
    unsigned int        msgh_id;
    unsigned int        msgh_reserved1;
    unsigned int        msgh_reserved2;

    unsigned int        event_type;
    int                 mouse_x;
    int                 mouse_y;
    unsigned int        button;
    unsigned int        key;
    unsigned int        modifiers;
} ws_event_msg_t;

typedef struct {
    unsigned int        type;
    int                 x;
    int                 y;
    unsigned int        button;
    unsigned int        key;
    unsigned int        modifiers;
} ws_event_t;

#ifdef __cplusplus
}
#endif

#endif

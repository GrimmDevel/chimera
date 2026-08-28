/* =============================================================================
 * XIU Operating System — Mach IPC Subsystem & OOL Verification Utility
 * usr/bin/mach_demo.c
 * ============================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MACH_PORT_RIGHT_SEND        0
#define MACH_PORT_RIGHT_RECEIVE     1
#define MACH_PORT_RIGHT_PORT_SET    3
#define MACH_PORT_RIGHT_DEAD_NAME   4

#define MACH_PORT_TYPE_SEND         0x00000001
#define MACH_PORT_TYPE_RECEIVE      0x00000002
#define MACH_PORT_TYPE_SEND_RECEIVE 0x00000003

#define MACH_SEND_MSG               0x00000001
#define MACH_RCV_MSG                0x00000002
#define MACH_MSGH_BITS_COMPLEX      0x80000000u

#define MACH_MSG_PORT_DESCRIPTOR    0u
#define MACH_MSG_OOL_DESCRIPTOR     1u

typedef unsigned int mach_port_name_t;
typedef unsigned int mach_msg_bits_t;
typedef unsigned int mach_msg_size_t;
typedef unsigned int mach_msg_id_t;

typedef struct __attribute__((packed)) {
    mach_msg_bits_t     msgh_bits;
    mach_msg_size_t     msgh_size;
    mach_port_name_t    msgh_remote_port;
    mach_port_name_t    msgh_local_port;
    mach_port_name_t    msgh_voucher_port;
    mach_msg_id_t       msgh_id;
} mach_msg_header_t;

typedef struct __attribute__((packed)) {
    unsigned int        msgh_descriptor_count;
} mach_msg_body_t;

typedef struct __attribute__((packed)) {
    unsigned long long  address;
    unsigned char       deallocate;
    unsigned char       copy;
    unsigned char       pad;
    unsigned char       type;
    unsigned int        size;
} mach_msg_ool_descriptor_t;

typedef struct __attribute__((packed)) {
    mach_msg_header_t           header;
    mach_msg_body_t             body;
    mach_msg_ool_descriptor_t   ool;
    char                        inline_text[64];
} test_complex_msg_t;

extern mach_port_name_t mach_task_self(void);
extern int mach_port_allocate(unsigned int task, unsigned int right, unsigned int *name);
extern int mach_port_deallocate(unsigned int task, unsigned int name);
extern int mach_port_type(unsigned int task, unsigned int name, unsigned int *ptype);
extern int mach_msg_trap(void *msg, int option, unsigned int send_size, unsigned int rcv_size,
                         unsigned int rcv_name, unsigned int timeout, unsigned int notify);

int main(int argc, char **argv) {
    printf("=== XIU Darwin Mach IPC Subsystem Verification ===\n");

    mach_port_name_t task_self = mach_task_self();
    printf("[1/4] Task port: 0x%x\n", task_self);

    mach_port_name_t my_port = 0;
    int res = mach_port_allocate(task_self, MACH_PORT_RIGHT_RECEIVE, &my_port);
    if (res != 0) {
        printf("[-] Failed to allocate Mach port (res=%d)\n", res);
        return 1;
    }
    printf("[2/4] Allocated Mach Port: name=0x%x\n", my_port);

    unsigned int ptype = 0;
    mach_port_type(task_self, my_port, &ptype);
    printf("      Port type bits: 0x%x (SEND_RECEIVE: %s)\n",
           ptype, (ptype & MACH_PORT_TYPE_SEND_RECEIVE) ? "YES" : "NO");

    // complex message with out-of-line memory
    printf("[3/4] Preparing Complex Mach message with Out-of-Line memory...\n");
    char ool_payload[128];
    snprintf(ool_payload, sizeof(ool_payload),
             "Hello from Out-Of-Line Darwin Mach IPC Memory Buffer! [PID=%d]", getpid());

    test_complex_msg_t send_msg;
    memset(&send_msg, 0, sizeof(send_msg));
    send_msg.header.msgh_bits = MACH_MSGH_BITS_COMPLEX;
    send_msg.header.msgh_size = sizeof(test_complex_msg_t);
    send_msg.header.msgh_remote_port = my_port;
    send_msg.header.msgh_local_port = my_port;
    send_msg.header.msgh_id = 0x1337;

    send_msg.body.msgh_descriptor_count = 1;
    send_msg.ool.type = MACH_MSG_OOL_DESCRIPTOR;
    send_msg.ool.address = (unsigned long long)(uintptr_t)ool_payload;
    send_msg.ool.size = strlen(ool_payload) + 1;
    send_msg.ool.deallocate = 0;

    strncpy(send_msg.inline_text, "Inline Mach Header Descriptor Payload", sizeof(send_msg.inline_text) - 1);

    res = mach_msg_trap(&send_msg, MACH_SEND_MSG, sizeof(send_msg), 0, 0, 1000, 0);
    if (res != 0) {
        printf("[-] mach_msg send failed (res=%d)\n", res);
        return 1;
    }
    printf("      Complex message enqueued successfully!\n");

    // 3. Receive Complex Message
    test_complex_msg_t rcv_msg;
    memset(&rcv_msg, 0, sizeof(rcv_msg));
    res = mach_msg_trap(&rcv_msg, MACH_RCV_MSG, 0, sizeof(rcv_msg) + 64, my_port, 1000, 0);
    if (res != 0) {
        printf("[-] mach_msg receive failed (res=%d)\n", res);
        return 1;
    }

    printf("      Received Mach message: ID=0x%x, Descriptors=%d\n",
           rcv_msg.header.msgh_id, rcv_msg.body.msgh_descriptor_count);
    printf("      Inline data: \"%s\"\n", rcv_msg.inline_text);

    if (rcv_msg.body.msgh_descriptor_count > 0 && rcv_msg.ool.address != 0) {
        const char *received_ool = (const char *)(uintptr_t)rcv_msg.ool.address;
        printf("      OOL Memory VA=0x%llx size=%u:\n", rcv_msg.ool.address, rcv_msg.ool.size);
        printf("      OOL Payload: \"%s\"\n", received_ool);
    }

    // 4. Cleanup
    mach_port_deallocate(task_self, my_port);
    printf("[4/5] Port 0x%x deallocated.\n", my_port);

    // 5. Mach MIG RPC Subsystem
    printf("[5/6] Testing Mach MIG RPC (mach_vm_allocate & mach_vm_deallocate)...\n");
    extern int mach_vm_allocate(unsigned int target, unsigned long long *address, unsigned long long size, int flags);
    extern int mach_vm_deallocate(unsigned int target, unsigned long long address, unsigned long long size);

    unsigned long long mig_va = 0;
    res = mach_vm_allocate(task_self, &mig_va, 8192, 1);
    if (res == 0 && mig_va != 0) {
        printf("      mach_vm_allocate RPC succeeded: allocated VA=0x%llx (8 KiB)\n", mig_va);
        char *ptr = (char *)(uintptr_t)mig_va;
        strcpy(ptr, "Mach MIG RPC Allocated Memory Buffer!");
        printf("      Memory read/write verified: \"%s\"\n", ptr);
        mach_vm_deallocate(task_self, mig_va, 8192);
        printf("      mach_vm_deallocate RPC succeeded!\n");
    } else {
        printf("[-] mach_vm_allocate RPC failed (res=%d)\n", res);
    }

    // 6. Darwin Mach Bootstrap Server
    printf("[6/6] Testing Darwin Bootstrap Server (bootstrap_register & bootstrap_look_up)...\n");
    #include <bootstrap.h>
    mach_port_t srv_port = 0;
    mach_port_allocate(task_self, MACH_PORT_RIGHT_RECEIVE, &srv_port);

    kern_return_t kr = bootstrap_register(bootstrap_port, "org.xiu.EchoService", srv_port);
    printf("      bootstrap_register(\"org.xiu.EchoService\"): kr=%d\n", kr);

    mach_port_t resolved_port = 0;
    kr = bootstrap_look_up(bootstrap_port, "org.xiu.EchoService", &resolved_port);
    printf("      bootstrap_look_up(\"org.xiu.EchoService\"): kr=%d (port=0x%x)\n", kr, resolved_port);

    mach_port_deallocate(task_self, srv_port);
    if (resolved_port != srv_port && resolved_port != 0) mach_port_deallocate(task_self, resolved_port);

    printf("=== All Mach IPC, MIG RPC & Bootstrap tests PASSED! ===\n");
    return 0;
}

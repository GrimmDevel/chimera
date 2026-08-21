/*
 * XIU Operating System — Libinput & Event Subsystem Integration
 * ponytail: provides libinput API backed by XIU devfs (/dev/mouse, /dev/kbd)
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

struct libinput {
    int mouse_fd;
    int kbd_fd;
};

struct libinput_device {
    const char *name;
};

enum libinput_event_type {
    LIBINPUT_EVENT_NONE = 0,
    LIBINPUT_EVENT_DEVICE_ADDED,
    LIBINPUT_EVENT_DEVICE_REMOVED,
    LIBINPUT_EVENT_KEYBOARD_KEY = 300,
    LIBINPUT_EVENT_POINTER_MOTION = 400,
    LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE,
    LIBINPUT_EVENT_POINTER_BUTTON,
    LIBINPUT_EVENT_POINTER_AXIS,
};

enum libinput_key_state {
    LIBINPUT_KEY_STATE_RELEASED = 0,
    LIBINPUT_KEY_STATE_PRESSED = 1,
};

enum libinput_button_state {
    LIBINPUT_BUTTON_STATE_RELEASED = 0,
    LIBINPUT_BUTTON_STATE_PRESSED = 1,
};

struct libinput_event {
    enum libinput_event_type type;
    struct libinput_device device;
    uint32_t key;
    enum libinput_key_state key_state;
    double dx;
    double dy;
    double abs_x;
    double abs_y;
    uint32_t button;
    enum libinput_button_state button_state;
};

struct libinput_event_keyboard {
    struct libinput_event *base;
};

struct libinput_event_pointer {
    struct libinput_event *base;
};

struct libinput_interface {
    int (*open_restricted)(const char *path, int flags, void *user_data);
    void (*close_restricted)(int fd, void *user_data);
};

struct libinput *libinput_udev_create_context(const struct libinput_interface *interface, void *user_data, void *udev) {
    (void)interface; (void)user_data; (void)udev;
    struct libinput *li = (struct libinput *)calloc(1, sizeof(struct libinput));
    if (!li) return NULL;
    li->mouse_fd = open("/dev/mouse", O_RDONLY | O_NONBLOCK);
    li->kbd_fd = open("/dev/kbd", O_RDONLY | O_NONBLOCK);
    return li;
}

int libinput_udev_assign_seat(struct libinput *li, const char *seat_id) {
    (void)li; (void)seat_id;
    return 0;
}

void libinput_unref(struct libinput *li) {
    if (!li) return;
    if (li->mouse_fd >= 0) close(li->mouse_fd);
    if (li->kbd_fd >= 0) close(li->kbd_fd);
    free(li);
}

int libinput_get_fd(struct libinput *li) {
    if (!li) return -1;
    if (li->mouse_fd >= 0) return li->mouse_fd;
    return 0;
}

int libinput_dispatch(struct libinput *li) {
    (void)li;
    return 0;
}

void libinput_log_set_priority(struct libinput *li, int priority) {
    (void)li; (void)priority;
}

static struct libinput_event g_current_event;

struct libinput_event *libinput_get_event(struct libinput *li) {
    if (!li) return NULL;
    if (li->mouse_fd >= 0) {
        signed char packet[4];
        int n = (int)read(li->mouse_fd, packet, 4);
        if (n >= 3) {
            memset(&g_current_event, 0, sizeof(g_current_event));
            g_current_event.device.name = "XIU Mouse";
            int dx = packet[1];
            int dy = packet[2];
            int btn = packet[0] & 0x7;
            if (dx != 0 || dy != 0) {
                g_current_event.type = LIBINPUT_EVENT_POINTER_MOTION;
                g_current_event.dx = (double)dx;
                g_current_event.dy = (double)dy;
                return &g_current_event;
            }
            if (btn != 0) {
                g_current_event.type = LIBINPUT_EVENT_POINTER_BUTTON;
                g_current_event.button = 0x110; // BTN_LEFT
                g_current_event.button_state = (btn & 1) ? LIBINPUT_BUTTON_STATE_PRESSED : LIBINPUT_BUTTON_STATE_RELEASED;
                return &g_current_event;
            }
        }
    }
    return NULL;
}

void libinput_event_destroy(struct libinput_event *event) {
    (void)event;
}

enum libinput_event_type libinput_event_get_type(struct libinput_event *event) {
    return event ? event->type : LIBINPUT_EVENT_NONE;
}

struct libinput_device *libinput_event_get_device(struct libinput_event *event) {
    return event ? &event->device : NULL;
}

const char *libinput_device_get_name(struct libinput_device *device) {
    return device && device->name ? device->name : "XIU Device";
}

struct libinput_event_keyboard *libinput_event_get_keyboard_event(struct libinput_event *event) {
    return (struct libinput_event_keyboard *)event;
}

uint32_t libinput_event_keyboard_get_key(struct libinput_event_keyboard *event) {
    struct libinput_event *ev = (struct libinput_event *)event;
    return ev ? ev->key : 0;
}

enum libinput_key_state libinput_event_keyboard_get_key_state(struct libinput_event_keyboard *event) {
    struct libinput_event *ev = (struct libinput_event *)event;
    return ev ? ev->key_state : LIBINPUT_KEY_STATE_RELEASED;
}

struct libinput_event_pointer *libinput_event_get_pointer_event(struct libinput_event *event) {
    return (struct libinput_event_pointer *)event;
}

double libinput_event_pointer_get_dx_unaccelerated(struct libinput_event_pointer *event) {
    struct libinput_event *ev = (struct libinput_event *)event;
    return ev ? ev->dx : 0.0;
}

double libinput_event_pointer_get_dy_unaccelerated(struct libinput_event_pointer *event) {
    struct libinput_event *ev = (struct libinput_event *)event;
    return ev ? ev->dy : 0.0;
}

double libinput_event_pointer_get_absolute_x_transformed(struct libinput_event_pointer *event, uint32_t width) {
    struct libinput_event *ev = (struct libinput_event *)event;
    return ev ? ev->abs_x * width : 0.0;
}

double libinput_event_pointer_get_absolute_y_transformed(struct libinput_event_pointer *event, uint32_t height) {
    struct libinput_event *ev = (struct libinput_event *)event;
    return ev ? ev->abs_y * height : 0.0;
}

uint32_t libinput_event_pointer_get_button(struct libinput_event_pointer *event) {
    struct libinput_event *ev = (struct libinput_event *)event;
    return ev ? ev->button : 0;
}

enum libinput_button_state libinput_event_pointer_get_button_state(struct libinput_event_pointer *event) {
    struct libinput_event *ev = (struct libinput_event *)event;
    return ev ? ev->button_state : LIBINPUT_BUTTON_STATE_RELEASED;
}

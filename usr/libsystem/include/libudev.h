#ifndef _LIBUDEV_H_
#define _LIBUDEV_H_

struct udev;
struct udev_device;

static inline struct udev *udev_new(void) { return (struct udev *)1; }
static inline struct udev *udev_unref(struct udev *udev) { (void)udev; return (struct udev *)0; }
static inline struct udev_device *udev_device_ref(struct udev_device *dev) { return dev; }
static inline struct udev_device *udev_device_unref(struct udev_device *dev) { (void)dev; return (struct udev_device *)0; }

#endif

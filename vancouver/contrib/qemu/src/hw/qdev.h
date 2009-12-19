#ifndef QDEV_H
#define QDEV_H

#include "hw.h"
#include "sys-queue.h"

typedef struct DeviceInfo DeviceInfo;

typedef struct DeviceProperty DeviceProperty;

typedef struct BusState BusState;

typedef struct BusInfo BusInfo;

/* This structure should not be accessed directly.  We declare it here
   so that it can be embedded in individual device state structures.  */
struct DeviceState {
    DeviceInfo *info;
    BusState *parent_bus;
    DeviceProperty *props;
    int num_gpio_out;
    qemu_irq *gpio_out;
    int num_gpio_in;
    qemu_irq *gpio_in;
    LIST_HEAD(, BusState) child_bus;
    NICInfo *nd;
    LIST_ENTRY(DeviceState) sibling;
};

typedef void (*bus_dev_printfn)(Monitor *mon, DeviceState *dev, int indent);
struct BusInfo {
    const char *name;
    size_t size;
    bus_dev_printfn print_dev;
};

struct BusState {
    DeviceState *parent;
    BusInfo *info;
    const char *name;
    LIST_HEAD(, DeviceState) children;
    LIST_ENTRY(BusState) sibling;
};

/*** Board API.  This should go away once we have a machine config file.  ***/

DeviceState *qdev_create(BusState *bus, const char *name);
void qdev_init(DeviceState *dev);
void qdev_free(DeviceState *dev);

/* Set properties between creation and init.  */
void qdev_set_prop_int(DeviceState *dev, const char *name, uint64_t value);
void qdev_set_prop_dev(DeviceState *dev, const char *name, DeviceState *value);
void qdev_set_prop_ptr(DeviceState *dev, const char *name, void *value);
void qdev_set_netdev(DeviceState *dev, NICInfo *nd);

qemu_irq qdev_get_gpio_in(DeviceState *dev, int n);
void qdev_connect_gpio_out(DeviceState *dev, int n, qemu_irq pin);

BusState *qdev_get_child_bus(DeviceState *dev, const char *name);

/*** Device API.  ***/

typedef enum {
    PROP_TYPE_INT,
    PROP_TYPE_PTR,
    PROP_TYPE_DEV
} DevicePropType;

typedef struct {
    const char *name;
    DevicePropType type;
} DevicePropList;

typedef void (*qdev_initfn)(DeviceState *dev, DeviceInfo *info);
typedef void (*SCSIAttachFn)(DeviceState *host, BlockDriverState *bdrv,
              int unit);

struct DeviceInfo {
    const char *name;
    size_t size;
    DevicePropList *props;

    /* Private to qdev / bus.  */
    qdev_initfn init;
    BusInfo *bus_info;
    struct DeviceInfo *next;
};

void qdev_register(DeviceInfo *info);

/* Register device properties.  */
/* GPIO inputs also double as IRQ sinks.  */
void qdev_init_gpio_in(DeviceState *dev, qemu_irq_handler handler, int n);
void qdev_init_gpio_out(DeviceState *dev, qemu_irq *pins, int n);

void scsi_bus_new(DeviceState *host, SCSIAttachFn attach);

CharDriverState *qdev_init_chardev(DeviceState *dev);

BusState *qdev_get_parent_bus(DeviceState *dev);
uint64_t qdev_get_prop_int(DeviceState *dev, const char *name, uint64_t def);
DeviceState *qdev_get_prop_dev(DeviceState *dev, const char *name);
/* FIXME: Remove opaque pointer properties.  */
void *qdev_get_prop_ptr(DeviceState *dev, const char *name);

/* Convery from a base type to a parent type, with compile time checking.  */
#ifdef __GNUC__
#define DO_UPCAST(type, field, dev) ( __extension__ ( { \
    char __attribute__((unused)) offset_must_be_zero[ \
        -offsetof(type, field)]; \
    container_of(dev, type, field);}))
#else
#define DO_UPCAST(type, field, dev) container_of(dev, type, field)
#endif

/*** BUS API. ***/

BusState *qbus_create(BusInfo *info, DeviceState *parent, const char *name);

#define FROM_QBUS(type, dev) DO_UPCAST(type, qbus, dev)

/*** monitor commands ***/

void do_info_qtree(Monitor *mon);

#endif

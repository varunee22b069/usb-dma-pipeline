#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/kfifo.h>
#include <linux/uaccess.h>  
#include <linux/types.h>
#include <linux/poll.h>

#define USB_VENDOR_ID (0x22d9)
#define USB_PRODUCT_ID (0x2764)

#define BUFFER_SIZE 2048

enum state{FILLING,FULL,PROCESSING,PROCESSED};

struct buffer {
    enum state state;
    unsigned char *data;
};

struct buffer_data {
    struct usb_device *udev;
    struct urb *urb_in;
    struct buffer* buffer[2];
    uint8_t bulk_in_endpointAddr;
    int pipe;
    size_t bulk_in_size;
    unsigned char *buf_write_ptr;
};

struct usbdev_data{
    struct usb_interface *interface;
    struct device *dev;
    struct cdev cdev;
    struct buffer_data *bdata;  
};

extern wait_queue_head_t wq;

int dev_init(struct usbdev_data*);
void dev_exit(struct usbdev_data*);
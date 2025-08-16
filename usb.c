#include "project.h"

DECLARE_WAIT_QUEUE_HEAD(wq);
static void submit_next_urb(struct buffer_data*);

static int usb_probe(struct usb_interface *interface, const struct usb_device_id *id){
    struct buffer_data *bdata = kzalloc(sizeof(struct buffer_data), GFP_KERNEL);
    struct usbdev_data *usbData = kzalloc(sizeof(struct usbdev_data), GFP_KERNEL);
    struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *endpoint;
    unsigned char *raw_buffer;

    if (!bdata || !usbData) return -ENOMEM;

    iface_desc = interface->cur_altsetting;

    usbData->bdata = bdata;
    usbData->dev = &interface->dev;
    usb_set_intfdata(interface, usbData);
    bdata->udev = interface_to_usbdev(interface);

    if (dev_init(usbData) < 0)
        goto init_err;

    for (int i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
        endpoint = &iface_desc->endpoint[i].desc;
        if (usb_endpoint_is_bulk_in(endpoint)) {
            bdata->bulk_in_endpointAddr = endpoint->bEndpointAddress;
            bdata->bulk_in_size = usb_endpoint_maxp(endpoint);
            break;
        }
    }

    bdata->pipe = usb_rcvbulkpipe(bdata->udev, bdata->bulk_in_endpointAddr);
    bdata->urb_in = usb_alloc_urb(0, GFP_KERNEL);
    if (!bdata->urb_in) {
        dev_err(usbData->dev, "failed to allocate urb");
        goto urb_err;
    }

    raw_buffer = usb_alloc_coherent(bdata->udev, 2 * BUFFER_SIZE, GFP_KERNEL, &bdata->urb_in->transfer_dma);
    if (!raw_buffer) {
        dev_err(usbData->dev, "failed to allocate dma buffer");
        goto dma_err;
    }

    for (int i = 0; i < 2; i++) {
        bdata->buffer[i] = kzalloc(sizeof(struct buffer), GFP_KERNEL);
        if (!bdata->buffer[i]) goto buf_alloc_err;
    }

    bdata->buffer[0]->data = raw_buffer;
    bdata->buffer[0]->state = FILLING;

    bdata->buffer[1]->data = raw_buffer + BUFFER_SIZE;
    bdata->buffer[1]->state = PROCESSED;

    bdata->buf_write_ptr = bdata->buffer[0]->data;

    submit_next_urb(bdata);
    dev_info(usbData->dev, "USB Driver Probed: vendor ID: 0x%02x, Product ID: 0x%02x\n", id->idVendor, id->idProduct);
    return 0;

buf_alloc_err:
    usb_free_coherent(bdata->udev, 2 * BUFFER_SIZE, raw_buffer, bdata->urb_in->transfer_dma);
dma_err:
    usb_free_urb(bdata->urb_in);
urb_err:
    dev_exit(usbData);
init_err:
    kfree(bdata);
    kfree(usbData);
    return -ENOMEM;
}

static void bulk_read_callback(struct urb *urb){
    struct buffer_data *bdata = urb->context;
    unsigned char *buf0 = bdata->buffer[0]->data;
    unsigned char *buf1 = bdata->buffer[1]->data;
    unsigned char *ptr = bdata->buf_write_ptr;

    if (ptr < buf1) {
        submit_next_urb(bdata);
    } else if (ptr == buf1) {
        WRITE_ONCE(bdata->buffer[0]->state, FULL);
        wait_event_interruptible(wq, READ_ONCE(bdata->buffer[1]->state) == PROCESSED);
        wake_up_interruptible(&wq);
        bdata->buffer[1]->state = FILLING;
        submit_next_urb(bdata);
    } else if (ptr > buf1 && ptr < buf1 + BUFFER_SIZE) {
        submit_next_urb(bdata);
    } else {
        WRITE_ONCE(bdata->buffer[1]->state, FULL);
        wait_event_interruptible(wq, READ_ONCE(bdata->buffer[0]->state) == PROCESSED);
        wake_up_interruptible(&wq);
        bdata->buffer[0]->state = FILLING;
        bdata->buf_write_ptr = buf0;
        submit_next_urb(bdata);
    }
}

static void submit_next_urb(struct buffer_data *bdata){
    usb_fill_bulk_urb(
        bdata->urb_in,
        bdata->udev,
        bdata->pipe,
        bdata->buf_write_ptr,
        bdata->bulk_in_size,
        bulk_read_callback,
        bdata
    );

    usb_submit_urb(bdata->urb_in, GFP_ATOMIC);

    if ((READ_ONCE(bdata->buffer[0]->state) == FULL && bdata->buf_write_ptr < bdata->buffer[1]->data) ||
        (READ_ONCE(bdata->buffer[1]->state) == FULL && bdata->buf_write_ptr >= bdata->buffer[1]->data)) {
        return;
    }

    bdata->buf_write_ptr += bdata->bulk_in_size;
}

static void usb_disconnect(struct usb_interface *interface){
    struct usbdev_data *usbData = usb_get_intfdata(interface);
    struct buffer_data *bdata = usbData->bdata;

    usb_free_coherent(
        bdata->udev,
        2 * BUFFER_SIZE,
        bdata->buffer[0]->data,  // shared memory base
        bdata->urb_in->transfer_dma);

    for (int i = 0; i < 2; i++) {
        kfree(bdata->buffer[i]);
    }

    usb_free_urb(bdata->urb_in);
    usb_set_intfdata(interface, NULL);
    dev_exit(usbData);
    kfree(bdata);
    kfree(usbData);
    dev_info(&interface->dev, "USB Driver Disconnected");
}

const struct usb_device_id usb_table[] = {
    {USB_DEVICE(USB_VENDOR_ID, USB_PRODUCT_ID)},
    { }
};

struct usb_driver dma_usb_driver = {
    .name = "DMA USB Driver",
    .probe = usb_probe,
    .disconnect = usb_disconnect,
    .id_table = usb_table,
};

static int __init usb_init(void){
    int res = usb_register(&dma_usb_driver);
    if (res < 0)
        pr_err("failed to register usb device!");
    return res;
}

static void __exit usb_exit(void){
    usb_deregister(&dma_usb_driver);
}

MODULE_DEVICE_TABLE(usb, usb_table);

module_init(usb_init);
module_exit(usb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Augustus");
MODULE_DESCRIPTION("Character Device For USB Driver");
MODULE_VERSION("0.0");

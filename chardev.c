#include "project.h"

#define USB_IOCTL_MAGIC 'U'
#define USB_BUFA_PROCESSED _IO(USB_IOCTL_MAGIC, 1)
#define USB_BUFB_PROCESSED _IO(USB_IOCTL_MAGIC, 2)

static struct class *dev_class = NULL;
static dev_t dev;
extern struct usb_driver dma_usb_driver;

static int usb_open(struct inode *, struct file *);
static int usb_mmap(struct file *, struct vm_area_struct*);
static unsigned int usb_poll(struct file*, struct poll_table_struct*);
static int usb_release(struct inode*, struct file*);
static long usb_ioctl(struct file*, unsigned int, unsigned long);

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = usb_open,
    .llseek = no_llseek,
    .release = usb_release,
    .mmap = usb_mmap,
    .poll = usb_poll,
    .unlocked_ioctl = usb_ioctl
};

int dev_init(struct usbdev_data *usbData){
    if (alloc_chrdev_region(&dev, 0, 1, "usb_dev") < 0) return -1;

    dev_class = class_create("usb_dev");
    if (IS_ERR(dev_class)) {
        dev_err(usbData->dev, "failed to create device class");
        goto class_err;
    }

    cdev_init(&usbData->cdev, &fops);
    if (cdev_add(&usbData->cdev, dev, 1) < 0) {
        dev_err(usbData->dev, "failed to add char device");
        goto add_err;
    }

    if (IS_ERR(device_create(dev_class, NULL, dev, NULL, "usb_dev"))) {
        dev_err(usbData->dev, "failed to create device");
        goto device_err;
    }

    return 0;

device_err:
    device_destroy(dev_class, dev);
add_err:
    class_destroy(dev_class);
class_err:
    cdev_del(&usbData->cdev);
    unregister_chrdev_region(dev, 1);
    return -1;
}

static int usb_open(struct inode *inode, struct file *file){
    struct usbdev_data *usbData = container_of(inode->i_cdev, struct usbdev_data, cdev);
    file->private_data = usbData;
    dev_info(usbData->dev, "Device Open");
    return 0;
}

static int usb_mmap(struct file *file, struct vm_area_struct* vma){
    struct usbdev_data *data = file->private_data; 
    unsigned long pfn = data->bdata->urb_in->transfer_dma >> PAGE_SHIFT;
    unsigned long size = vma->vm_end - vma->vm_start;

    if (size > 2 * BUFFER_SIZE)
        return -EINVAL;

    if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot))
        return -EAGAIN;

    return 0;
}

static unsigned int usb_poll(struct file *file, struct poll_table_struct *wait){
    struct usbdev_data *usbData = file->private_data;
    unsigned int mask = 0;

    poll_wait(file, &wq, wait);

    if (READ_ONCE(usbData->bdata->buffer[0]->state) == FULL ||
        READ_ONCE(usbData->bdata->buffer[1]->state) == FULL)
        mask |= POLLRDNORM;

    return mask;
}

static long usb_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
    struct usbdev_data *usbData = file->private_data;

    switch (cmd) {
        case USB_BUFA_PROCESSED:
            usbData->bdata->buffer[0]->state = PROCESSED;
            break;
        case USB_BUFB_PROCESSED:
            usbData->bdata->buffer[1]->state = PROCESSED;
            break;
        default:
            dev_err(usbData->dev, "Invalid IOCTL argument");
            return -EINVAL;
    }

    return 0;
}

static int usb_release(struct inode *inode, struct file *file){
    struct usbdev_data *usbData = file->private_data;
    dev_info(usbData->dev, "Device Closed");
    return 0;
}

void dev_exit(struct usbdev_data *usbData){
    device_destroy(dev_class, dev);
    class_destroy(dev_class);
    cdev_del(&usbData->cdev);
    unregister_chrdev_region(dev, 1);
    dev_info(usbData->dev, "Device Driver removed");
}

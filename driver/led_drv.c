#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>

#define DEVICE_NAME "led_drv"

static dev_t led_devno;
static struct cdev led_cdev;
static struct class *led_class;

static int led_open(struct inode *inode, struct file *file)
{
    pr_info("led_drv: open\n");
    return 0;
}

static int led_release(struct inode *inode, struct file *file)
{
    pr_info("led_drv: release\n");
    return 0;
}

static const struct file_operations led_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .release = led_release,
};

static int __init led_drv_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&led_devno, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("led_drv: alloc_chrdev_region failed\n");
        return ret;
    }

    cdev_init(&led_cdev, &led_fops);
    led_cdev.owner = THIS_MODULE;

    ret = cdev_add(&led_cdev, led_devno, 1);
    if (ret < 0) {
        pr_err("led_drv: cdev_add failed\n");
        unregister_chrdev_region(led_devno, 1);
        return ret;
    }

    led_class = class_create(THIS_MODULE, DEVICE_NAME);
    if (IS_ERR(led_class)) {
        pr_err("led_drv: class_create failed\n");
        cdev_del(&led_cdev);
        unregister_chrdev_region(led_devno, 1);
        return PTR_ERR(led_class);
    }

    device_create(led_class, NULL, led_devno, NULL, DEVICE_NAME);

    pr_info("led_drv: init, major=%d, minor=%d\n", MAJOR(led_devno), MINOR(led_devno));
    return 0;
}

static void __exit led_drv_exit(void)
{
    device_destroy(led_class, led_devno);
    class_destroy(led_class);
    cdev_del(&led_cdev);
    unregister_chrdev_region(led_devno, 1);

    pr_info("led_drv: exit\n");
}

module_init(led_drv_init);
module_exit(led_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("cgh-c");
MODULE_DESCRIPTION("char device skeleton for led driver");

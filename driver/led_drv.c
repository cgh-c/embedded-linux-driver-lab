#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>

#define DEVICE_NAME "led_drv"

struct led_drv_data {
    dev_t devno;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct gpio_desc *led_gpiod;
};

static struct led_drv_data *g_led;

static int led_open(struct inode *inode, struct file *file)
{
    file->private_data = g_led;
    pr_info("led_drv: open\n");
    return 0;
}

static int led_release(struct inode *inode, struct file *file)
{
    pr_info("led_drv: release\n");
    return 0;
}

static ssize_t led_write(struct file *file, const char __user *buf,
                         size_t count, loff_t *ppos)
{
    char kbuf[8];
    size_t len;
    struct led_drv_data *drvdata = file->private_data;

    if (!drvdata || !drvdata->led_gpiod)
        return -ENODEV;

    len = min(count, sizeof(kbuf) - 1);

    if (copy_from_user(kbuf, buf, len))
        return -EFAULT;

    kbuf[len] = '\0';

    /*
     * echo 1 > /dev/led_drv 会带换行
     * 所以只看第一个字符
     */
    if (kbuf[0] == '1') {
        /*
         * 设备树里用了 GPIO_ACTIVE_LOW，
         * gpiod_set_value() 传 1 表示逻辑“激活”
         */
        gpiod_set_value(drvdata->led_gpiod, 1);
        pr_info("led_drv: LED ON\n");
    } else if (kbuf[0] == '0') {
        gpiod_set_value(drvdata->led_gpiod, 0);
        pr_info("led_drv: LED OFF\n");
    } else {
        pr_err("led_drv: invalid input: %c\n", kbuf[0]);
        return -EINVAL;
    }

    return count;
}

static const struct file_operations led_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .release = led_release,
    .write = led_write,
};

static int led_chrdev_register(struct led_drv_data *drvdata)
{
    int ret;

    ret = alloc_chrdev_region(&drvdata->devno, 0, 1, DEVICE_NAME);
    if (ret < 0)
        return ret;

    cdev_init(&drvdata->cdev, &led_fops);
    drvdata->cdev.owner = THIS_MODULE;

    ret = cdev_add(&drvdata->cdev, drvdata->devno, 1);
    if (ret)
        goto err_unregister;

    drvdata->class = class_create(THIS_MODULE, DEVICE_NAME);
    if (IS_ERR(drvdata->class)) {
        ret = PTR_ERR(drvdata->class);
        goto err_cdev_del;
    }

    drvdata->device = device_create(drvdata->class, NULL,
                                    drvdata->devno, NULL, DEVICE_NAME);
    if (IS_ERR(drvdata->device)) {
        ret = PTR_ERR(drvdata->device);
        goto err_class_destroy;
    }

    pr_info("led_drv: chrdev registered, major=%d minor=%d\n",
            MAJOR(drvdata->devno), MINOR(drvdata->devno));
    return 0;

err_class_destroy:
    class_destroy(drvdata->class);
err_cdev_del:
    cdev_del(&drvdata->cdev);
err_unregister:
    unregister_chrdev_region(drvdata->devno, 1);
    return ret;
}

static void led_chrdev_unregister(struct led_drv_data *drvdata)
{
    device_destroy(drvdata->class, drvdata->devno);
    class_destroy(drvdata->class);
    cdev_del(&drvdata->cdev);
    unregister_chrdev_region(drvdata->devno, 1);
}

static int led_probe(struct platform_device *pdev)
{
    int ret;
    struct led_drv_data *drvdata;

    pr_info("led_drv: probe start\n");

    drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
    if (!drvdata)
        return -ENOMEM;

    drvdata->led_gpiod = devm_gpiod_get(&pdev->dev, "led", GPIOD_OUT_LOW);
    if (IS_ERR(drvdata->led_gpiod)) {
        dev_err(&pdev->dev, "failed to get led-gpios\n");
        return PTR_ERR(drvdata->led_gpiod);
    }

    g_led = drvdata;
    platform_set_drvdata(pdev, drvdata);

    ret = led_chrdev_register(drvdata);
    if (ret) {
        dev_err(&pdev->dev, "failed to register chrdev\n");
        return ret;
    }

    pr_info("led_drv: probe success\n");
    return 0;
}

static int led_remove(struct platform_device *pdev)
{
    struct led_drv_data *drvdata = platform_get_drvdata(pdev);

    pr_info("led_drv: remove\n");
    led_chrdev_unregister(drvdata);
    return 0;
}

static const struct of_device_id led_of_match[] = {
    { .compatible = "cgh,leddrv" },
    { }
};
MODULE_DEVICE_TABLE(of, led_of_match);

static struct platform_driver led_platform_driver = {
    .probe  = led_probe,
    .remove = led_remove,
    .driver = {
        .name = "cgh_led_drv",
        .of_match_table = led_of_match,
    },
};

module_platform_driver(led_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("cgh-c");
MODULE_DESCRIPTION("LED driver based on platform_driver + device tree");
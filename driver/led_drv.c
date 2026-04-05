#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/errno.h>

/* i.MX6ULL: GPIO5_IO03 -> SNVS_TAMPER3 */
#define CCM_CCGR1_ADDR                       0x020C406C
#define IOMUXC_SNVS_SW_MUX_CTL_PAD_TAMPER3   0x02290014
#define GPIO5_DR_ADDR                        0x020AC000
#define GPIO5_GDIR_ADDR                      0x020AC004

#define GPIO5_IO03_BIT                       3

#define DEVICE_NAME "led_drv"

static dev_t led_devno;
static struct cdev led_cdev;
static struct class *led_class;

/* hardcoded registers for experiment */
static void __iomem *ccm_ccgr1;
static void __iomem *iomuxc_tamper3_mux;
static void __iomem *gpio5_dr;
static void __iomem *gpio5_gdir;

static int led_hw_init(void)
{
    u32 val;

    ccm_ccgr1 = ioremap(CCM_CCGR1_ADDR, 4);
    iomuxc_tamper3_mux = ioremap(IOMUXC_SNVS_SW_MUX_CTL_PAD_TAMPER3, 4);
    gpio5_dr = ioremap(GPIO5_DR_ADDR, 4);
    gpio5_gdir = ioremap(GPIO5_GDIR_ADDR, 4);

    if (!ccm_ccgr1 || !iomuxc_tamper3_mux || !gpio5_dr || !gpio5_gdir) {
        pr_err("led_drv: ioremap failed\n");
        return -ENOMEM;
    }

    /* 1. enable GPIO5 clock: CCGR1[31:30] = 0b11 */
    val = readl(ccm_ccgr1);
    val |= (0x3 << 30);
    writel(val, ccm_ccgr1);

    /* 2. set SNVS_TAMPER3 to GPIO mode: ALT5 */
    val = readl(iomuxc_tamper3_mux);
    val &= ~0xf;
    val |= 0x5;
    writel(val, iomuxc_tamper3_mux);

    /* 3. set GPIO5_IO03 as output */
    val = readl(gpio5_gdir);
    val |= (1U << GPIO5_IO03_BIT);
    writel(val, gpio5_gdir);

    /* 4. default OFF: active low, so output high */
    val = readl(gpio5_dr);
    val |= (1U << GPIO5_IO03_BIT);
    writel(val, gpio5_dr);

    pr_info("led_drv: hw init done, GPIO5_IO03 configured\n");
    return 0;
}

static void led_hw_set(int on)
{
    u32 val;

    val = readl(gpio5_dr);

    if (on) {
        /* active low: 0 = ON */
        val &= ~(1U << GPIO5_IO03_BIT);
    } else {
        /* active low: 1 = OFF */
        val |= (1U << GPIO5_IO03_BIT);
    }

    writel(val, gpio5_dr);
}

static void led_hw_deinit(void)
{
    if (gpio5_dr) {
        /* leave LED off */
        led_hw_set(0);
        iounmap(gpio5_dr);
        gpio5_dr = NULL;
    }

    if (gpio5_gdir) {
        iounmap(gpio5_gdir);
        gpio5_gdir = NULL;
    }

    if (iomuxc_tamper3_mux) {
        iounmap(iomuxc_tamper3_mux);
        iomuxc_tamper3_mux = NULL;
    }

    if (ccm_ccgr1) {
        iounmap(ccm_ccgr1);
        ccm_ccgr1 = NULL;
    }
}

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

static ssize_t led_write(struct file *file, const char __user *buf,
                         size_t count, loff_t *ppos)
{
   char ch;

    if (count < 1)
        return -EINVAL;

    if (copy_from_user(&ch, buf, 1))
        return -EFAULT;

    if (ch == '1') {
        led_hw_set(1);
        pr_info("led_drv: LED ON\n");
    } else if (ch == '0') {
        led_hw_set(0);
        pr_info("led_drv: LED OFF\n");
    } else {
        pr_err("led_drv: invalid input '%c'\n", ch);
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

    ret = led_hw_init();
    if (ret) {
        device_destroy(led_class, led_devno);
        class_destroy(led_class);
        cdev_del(&led_cdev);
        unregister_chrdev_region(led_devno, 1);
        return ret;
    }

    pr_info("led_drv: init, major=%d, minor=%d\n", MAJOR(led_devno), MINOR(led_devno));
    return 0;
}

static void __exit led_drv_exit(void)
{
    led_hw_deinit();
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
MODULE_DESCRIPTION("hardcoded GPIO5_IO03 LED driver experiment");

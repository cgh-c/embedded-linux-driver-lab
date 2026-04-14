/*
 * key_drv.c - GPIO key driver with interrupt, blocking read, and poll support
 *
 * Device tree node example:
 *     key_drv {
 *         compatible = "cgh,keydrv";
 *         key-gpios = <&gpio5 1 GPIO_ACTIVE_LOW>;
 *     };
 *
 * Usage:
 *     insmod key_drv.ko
 *     cat /dev/key_drv          # blocking read, waits for key press
 *     ./key_test poll 3000      # poll with 3s timeout
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

#define DEVICE_NAME  "key_drv"
#define DEBOUNCE_MS  20

struct key_drv_data {
	dev_t                devno;
	struct cdev          cdev;
	struct class        *class;
	struct device       *device;
	struct gpio_desc    *key_gpiod;
	int                  irq;

	wait_queue_head_t    wq;
	int                  key_value;   /* 1 = pressed (active), 0 = released */
	int                  data_ready;  /* new event available */
	spinlock_t           lock;

	struct timer_list    debounce_timer;
};

static struct key_drv_data *g_key;

/* ------------------------------------------------------------------ */
/*  Debounce timer callback (softirq context)                         */
/*                                                                    */
/*  Why a timer?  Mechanical keys bounce for ~5-15 ms. The interrupt  */
/*  fires on every edge during the bounce. By deferring the actual    */
/*  GPIO read by 20 ms, we let the signal settle and only report one  */
/*  clean event per press/release.                                    */
/*                                                                    */
/*  mod_timer() in the IRQ handler resets the timer on each bounce,   */
/*  so only the LAST edge within the 20 ms window triggers a read.    */
/* ------------------------------------------------------------------ */
static void key_debounce_handler(unsigned long data)
{
	struct key_drv_data *drvdata = (struct key_drv_data *)data;
	unsigned long flags;
	int val;

	val = gpiod_get_value(drvdata->key_gpiod);

	spin_lock_irqsave(&drvdata->lock, flags);
	drvdata->key_value  = val;
	drvdata->data_ready = 1;
	spin_unlock_irqrestore(&drvdata->lock, flags);

	wake_up_interruptible(&drvdata->wq);

	pr_info("key_drv: key %s\n", val ? "PRESSED" : "RELEASED");
}

/* ------------------------------------------------------------------ */
/*  Hard IRQ handler                                                  */
/*                                                                    */
/*  Runs in interrupt context — cannot sleep, cannot call printk in   */
/*  production (we avoid it here). Just kick the debounce timer.      */
/* ------------------------------------------------------------------ */
static irqreturn_t key_irq_handler(int irq, void *dev_id)
{
	struct key_drv_data *drvdata = dev_id;

	mod_timer(&drvdata->debounce_timer,
		  jiffies + msecs_to_jiffies(DEBOUNCE_MS));

	return IRQ_HANDLED;
}

/* ------------------------------------------------------------------ */
/*  File operations                                                   */
/* ------------------------------------------------------------------ */

static int key_open(struct inode *inode, struct file *file)
{
	file->private_data = g_key;
	return 0;
}

static int key_release(struct inode *inode, struct file *file)
{
	return 0;
}

/*
 * key_read() — returns one byte per call: '1' (pressed) or '0' (released).
 *
 * Blocking mode (default):
 *   Sleeps in wait_event_interruptible() until an interrupt fires.
 *   This is safe because read() runs in process context.
 *
 * Non-blocking mode (O_NONBLOCK):
 *   Returns -EAGAIN immediately if no new event is available.
 *   This is the mode used together with poll()/select().
 */
static ssize_t key_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct key_drv_data *drvdata = file->private_data;
	unsigned long flags;
	int val;
	char kbuf;

	if (!drvdata)
		return -ENODEV;
	if (count < 1)
		return -EINVAL;

	if (file->f_flags & O_NONBLOCK) {
		spin_lock_irqsave(&drvdata->lock, flags);
		if (!drvdata->data_ready) {
			spin_unlock_irqrestore(&drvdata->lock, flags);
			return -EAGAIN;
		}
		val = drvdata->key_value;
		drvdata->data_ready = 0;
		spin_unlock_irqrestore(&drvdata->lock, flags);
	} else {
		/*
		 * wait_event_interruptible: puts the process to sleep on wq.
		 * The condition (data_ready) is checked atomically with the
		 * sleep — no race between check and sleep.
		 *
		 * Returns -ERESTARTSYS if interrupted by a signal (e.g. Ctrl-C),
		 * which the VFS translates to either restarting the syscall or
		 * returning -EINTR to userspace.
		 */
		if (wait_event_interruptible(drvdata->wq, drvdata->data_ready))
			return -ERESTARTSYS;

		spin_lock_irqsave(&drvdata->lock, flags);
		val = drvdata->key_value;
		drvdata->data_ready = 0;
		spin_unlock_irqrestore(&drvdata->lock, flags);
	}

	kbuf = val ? '1' : '0';
	if (copy_to_user(buf, &kbuf, 1))
		return -EFAULT;

	return 1;
}

/*
 * key_poll() — called by the kernel's poll/select/epoll infrastructure.
 *
 * How poll works (interview-critical):
 *   1. Userspace calls poll(fd, ...). Kernel calls our .poll callback.
 *   2. We call poll_wait() to register our wait queue with the poll_table.
 *      This does NOT sleep — it just tells the kernel "wake me via this wq".
 *   3. We return a bitmask: 0 = no data, POLLIN = readable.
 *   4. If we returned 0, the kernel sleeps until our wq gets a wake_up,
 *      then calls .poll again to re-check.
 *   5. If we returned POLLIN, the kernel returns to userspace immediately.
 *
 * Key difference from blocking read:
 *   - poll() can monitor MULTIPLE file descriptors simultaneously
 *   - poll() supports timeouts
 *   - The actual data is still fetched via a subsequent read() call
 */
static unsigned int key_poll(struct file *file, poll_table *wait)
{
	struct key_drv_data *drvdata = file->private_data;
	unsigned int mask = 0;
	unsigned long flags;

	poll_wait(file, &drvdata->wq, wait);

	spin_lock_irqsave(&drvdata->lock, flags);
	if (drvdata->data_ready)
		mask |= POLLIN | POLLRDNORM;
	spin_unlock_irqrestore(&drvdata->lock, flags);

	return mask;
}

static const struct file_operations key_fops = {
	.owner   = THIS_MODULE,
	.open    = key_open,
	.release = key_release,
	.read    = key_read,
	.poll    = key_poll,
};

/* ------------------------------------------------------------------ */
/*  Character device register / unregister                            */
/* ------------------------------------------------------------------ */

static int key_chrdev_register(struct key_drv_data *drvdata)
{
	int ret;

	ret = alloc_chrdev_region(&drvdata->devno, 0, 1, DEVICE_NAME);
	if (ret < 0)
		return ret;

	cdev_init(&drvdata->cdev, &key_fops);
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

	pr_info("key_drv: chrdev registered, major=%d minor=%d\n",
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

static void key_chrdev_unregister(struct key_drv_data *drvdata)
{
	device_destroy(drvdata->class, drvdata->devno);
	class_destroy(drvdata->class);
	cdev_del(&drvdata->cdev);
	unregister_chrdev_region(drvdata->devno, 1);
}

/* ------------------------------------------------------------------ */
/*  Platform driver probe / remove                                    */
/* ------------------------------------------------------------------ */

static int key_probe(struct platform_device *pdev)
{
	int ret;
	struct key_drv_data *drvdata;

	pr_info("key_drv: probe start\n");

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	/* Get GPIO descriptor from device tree "key-gpios" property */
	drvdata->key_gpiod = devm_gpiod_get(&pdev->dev, "key", GPIOD_IN);
	if (IS_ERR(drvdata->key_gpiod)) {
		dev_err(&pdev->dev, "failed to get key-gpios\n");
		return PTR_ERR(drvdata->key_gpiod);
	}

	/*
	 * gpiod_to_irq: maps a GPIO descriptor to its corresponding
	 * IRQ number. The GPIO controller driver provides this mapping.
	 * On i.MX6ULL, each GPIO can be configured as an interrupt source.
	 */
	drvdata->irq = gpiod_to_irq(drvdata->key_gpiod);
	if (drvdata->irq < 0) {
		dev_err(&pdev->dev, "failed to get IRQ for key GPIO\n");
		return drvdata->irq;
	}

	/* Initialize synchronization primitives */
	init_waitqueue_head(&drvdata->wq);
	spin_lock_init(&drvdata->lock);
	drvdata->key_value  = 0;
	drvdata->data_ready = 0;

	/* Setup debounce timer (Linux 4.9 API) */
	setup_timer(&drvdata->debounce_timer,
		    key_debounce_handler, (unsigned long)drvdata);

	/*
	 * Register IRQ with both edge triggers:
	 *   IRQF_TRIGGER_RISING  — key released (GPIO goes high, active-low)
	 *   IRQF_TRIGGER_FALLING — key pressed  (GPIO goes low,  active-low)
	 *
	 * We use devm_request_irq so the IRQ is automatically freed on
	 * driver removal — no need for an explicit free_irq() in remove().
	 */
	ret = devm_request_irq(&pdev->dev, drvdata->irq, key_irq_handler,
			       IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			       DEVICE_NAME, drvdata);
	if (ret) {
		dev_err(&pdev->dev, "failed to request IRQ %d\n", drvdata->irq);
		return ret;
	}

	g_key = drvdata;
	platform_set_drvdata(pdev, drvdata);

	ret = key_chrdev_register(drvdata);
	if (ret) {
		dev_err(&pdev->dev, "failed to register chrdev\n");
		return ret;
	}

	pr_info("key_drv: probe success, IRQ=%d\n", drvdata->irq);
	return 0;
}

static int key_remove(struct platform_device *pdev)
{
	struct key_drv_data *drvdata = platform_get_drvdata(pdev);

	pr_info("key_drv: remove\n");
	del_timer_sync(&drvdata->debounce_timer);
	key_chrdev_unregister(drvdata);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Device tree matching                                              */
/* ------------------------------------------------------------------ */

static const struct of_device_id key_of_match[] = {
	{ .compatible = "cgh,keydrv" },
	{ }
};
MODULE_DEVICE_TABLE(of, key_of_match);

static struct platform_driver key_platform_driver = {
	.probe  = key_probe,
	.remove = key_remove,
	.driver = {
		.name           = "cgh_key_drv",
		.of_match_table = key_of_match,
	},
};

module_platform_driver(key_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("cgh-c");
MODULE_DESCRIPTION("Key driver with GPIO interrupt, blocking read, and poll");

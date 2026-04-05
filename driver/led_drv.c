#include <linux/init.h>
#include <linux/module.h>

static int __init led_drv_init(void)
{
    pr_info("led_drv: init\n");
    return 0;
}

static void __exit led_drv_exit(void)
{
    pr_info("led_drv: exit\n");
}

module_init(led_drv_init);
module_exit(led_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("cgh-c");
MODULE_DESCRIPTION("Minimal LED driver skeleton");

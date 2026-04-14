# embedded-linux-driver-lab

基于 Linux 字符设备框架的嵌入式驱动练习项目，目标平台为 100ask IMX6ULL（Linux 4.9.88）。

## 已实现功能

### LED 驱动 (`driver/led_drv.c`)
- platform_driver + 设备树匹配
- GPIO 输出控制（`gpiod_set_value`）
- 字符设备自动创建 `/dev/led_drv`
- 用户态通过 `write()` 写入 `'1'`/`'0'` 控制亮灭

### 按键驱动 (`driver/key_drv.c`)
- platform_driver + 设备树匹配
- GPIO 中断（双边沿触发，捕获按下和释放）
- 定时器去抖（20ms，过滤机械抖动）
- 阻塞读（`wait_event_interruptible`）
- 非阻塞读（`O_NONBLOCK`，无数据返回 `-EAGAIN`）
- poll 机制（`poll_wait` + `POLLIN`）
- spinlock_irqsave 保护共享数据

## 目录结构

```
driver/          内核驱动代码
  led_drv.c        LED 控制驱动
  key_drv.c        按键输入驱动（中断 + poll）
app/             用户态测试程序
  led_test.c       LED 测试：./led_test on|off
  key_test.c       按键测试：./key_test read | ./key_test poll [ms]
docs/            开发文档和调试记录
```

## 编译

```bash
make              # 构建全部（驱动 + 用户态程序）
make clean        # 清理
```

依赖：
- 内核源码树：`/home/cgh/Linux-4.9.88`
- 交叉编译工具链：`arm-buildroot-linux-gnueabihf-`

## 部署测试

```bash
# 传文件到板子
scp driver/*.ko app/led_test app/key_test root@<board-ip>:/tmp/

# LED
insmod /tmp/led_drv.ko
/tmp/led_test on && /tmp/led_test off

# 按键
insmod /tmp/key_drv.ko
/tmp/key_test read            # 阻塞读，按键后打印事件
/tmp/key_test poll 3000       # poll，3秒超时
```

## 设备树

```dts
led_drv {
    compatible = "cgh,leddrv";
    led-gpios = <&gpio5 3 GPIO_ACTIVE_LOW>;
};

key_drv {
    compatible = "cgh,keydrv";
    key-gpios = <&gpio5 1 GPIO_ACTIVE_LOW>;
};

/* 需禁用默认的 gpio-keys 以释放 KEY0 */
gpio-keys { status = "disabled"; };
```

## 后续计划
- 多设备实例支持（`container_of` 替代全局指针）
- 完善 docs/debug_log.md 调试记录

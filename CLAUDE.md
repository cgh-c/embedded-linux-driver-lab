# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 构建

```bash
make            # 构建所有驱动和用户态程序
make clean      # 清理全部构建产物

# 或单独构建
cd driver && make          # 交叉编译，生成 led_drv.ko / key_drv.ko
cd app    && make          # 交叉编译，生成 led_test / key_test
```

构建依赖：
- 内核源码树：`/home/cgh/Linux-4.9.88`（已配置好）
- 交叉编译工具链：`arm-buildroot-linux-gnueabihf-`
- 目标架构：`arm`

## 部署到目标板

```bash
# 将 .ko 和测试程序传到板子
scp driver/led_drv.ko driver/key_drv.ko root@<board-ip>:/tmp/
scp app/led_test app/key_test root@<board-ip>:/tmp/

# --- LED 驱动 ---
insmod /tmp/led_drv.ko
echo 1 > /dev/led_drv        # 点亮
echo 0 > /dev/led_drv        # 熄灭
/tmp/led_test on              # 用户态程序控制
rmmod led_drv

# --- 按键驱动 ---
insmod /tmp/key_drv.ko
/tmp/key_test read            # 阻塞读，按键后打印事件，Ctrl-C 退出
/tmp/key_test poll 3000       # poll 模式，3 秒超时
rmmod key_drv

# 查看内核日志
dmesg | tail -20
```

**注意**：加载 `key_drv.ko` 前需确认默认设备树的 `gpio-keys` 节点已禁用（`status = "disabled"`），否则 GPIO5_IO01 被内核 built-in 驱动占用，probe 会返回 `-EBUSY`。

## 架构

这是一个嵌入式 Linux 驱动练习项目，目标平台为 100ask IMX6ULL（Linux 4.9.88）。所有驱动采用 **platform_driver + 设备树** 的标准模型。

### LED 驱动（`driver/led_drv.c`）

输出型驱动，控制 GPIO 驱动 LED。

1. **设备树匹配**：`compatible = "cgh,leddrv"`，需提供 `led-gpios` 属性（`GPIO_ACTIVE_LOW`）。
2. **probe 流程**：`devm_kzalloc` → `devm_gpiod_get`（`GPIOD_OUT_LOW`）→ 注册字符设备。
3. **字符设备**：动态主设备号，`class_create` + `device_create` 自动创建 `/dev/led_drv`；`write()` 读取用户态 `'1'`/`'0'` 调用 `gpiod_set_value`。
4. **全局状态**：`g_led` 全局指针关联 `file_operations`（单设备场景）。

### 按键驱动（`driver/key_drv.c`）

输入型驱动，通过 GPIO 中断捕获按键事件，支持阻塞读和 poll。

1. **设备树匹配**：`compatible = "cgh,keydrv"`，需提供 `key-gpios` 属性（`GPIO_ACTIVE_LOW`）。对应 100ask 板上的 KEY0（GPIO5_IO01）。
2. **probe 流程**：`devm_kzalloc` → `devm_gpiod_get`（`GPIOD_IN`）→ `gpiod_to_irq` 获取中断号 → `setup_timer` 初始化去抖定时器 → `devm_request_irq`（双边沿触发）→ 注册字符设备。
3. **中断 + 去抖**：硬中断处理函数仅调用 `mod_timer` 启动 20ms 定时器；定时器回调读取 GPIO 值、更新状态、唤醒等待队列。这样机械按键的抖动（5-15ms）被过滤为单次事件。
4. **阻塞读**：`read()` 通过 `wait_event_interruptible` 睡眠在等待队列上，直到有新按键事件。支持 `O_NONBLOCK` 非阻塞模式（无数据时返回 `-EAGAIN`）。
5. **poll 支持**：`.poll` 回调调用 `poll_wait` 注册等待队列，返回 `POLLIN | POLLRDNORM` 表示有数据可读。用户态可用 `poll()`/`select()` 同时监听多个设备。
6. **同步机制**：`spinlock_irqsave` 保护中断上下文与进程上下文之间的共享数据（`key_value`、`data_ready`）。

### 用户态测试程序

| 程序 | 用途 |
|---|---|
| `app/led_test` | `./led_test on\|off` 控制 LED |
| `app/key_test` | `./key_test read` 阻塞读；`./key_test poll [ms]` poll 模式 |

### 设备树节点

```dts
led_drv {
    compatible = "cgh,leddrv";
    led-gpios = <&gpio5 3 GPIO_ACTIVE_LOW>;
};

key_drv {
    compatible = "cgh,keydrv";
    key-gpios = <&gpio5 1 GPIO_ACTIVE_LOW>;
};
```

注意：需禁用默认设备树中的 `gpio-keys` 节点以释放 GPIO5_IO01。

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 构建驱动

```bash
cd driver
make          # 交叉编译，生成 led_drv.ko
make clean    # 清理构建产物
```

构建依赖：
- 内核源码树：`/home/cgh/Linux-4.9.88`（已配置好）
- 交叉编译工具链：`arm-buildroot-linux-gnueabihf-`
- 目标架构：`arm`

## 部署到目标板

```bash
# 将 .ko 传到板子（根据实际连接方式选择）
scp driver/led_drv.ko root@<board-ip>:/tmp/

# 在板子上加载/卸载
insmod /tmp/led_drv.ko
rmmod led_drv

# 测试控制 LED
echo 1 > /dev/led_drv   # 点亮
echo 0 > /dev/led_drv   # 熄灭

# 查看内核日志
dmesg | tail -20
```

## 架构

这是一个嵌入式 Linux 驱动练习项目，当前实现了 LED 控制驱动。

### 驱动模型（`driver/led_drv.c`）

采用 **platform_driver + 设备树** 的标准 Linux 驱动模型：

1. **设备树匹配**：驱动通过 `compatible = "cgh,leddrv"` 与设备树节点绑定，设备树节点需提供 `led-gpios` 属性（配置为 `GPIO_ACTIVE_LOW`）。

2. **probe 流程**：
   - `devm_kzalloc` 分配驱动私有数据 `led_drv_data`
   - `devm_gpiod_get` 获取 LED GPIO 描述符（`devm_` 前缀意味着卸载时自动释放）
   - 调用 `led_chrdev_register` 注册字符设备

3. **字符设备**：动态分配主设备号，通过 `class_create` + `device_create` 自动在 `/dev/led_drv` 创建设备节点；`write()` 回调读取用户态的 `'1'`/`'0'` 并调用 `gpiod_set_value` 控制 GPIO。

4. **全局状态**：`g_led` 是驱动私有数据的全局指针，供 `file_operations` 回调使用（单设备场景）。

### 项目规划方向

README 列出了后续计划：按键驱动、中断处理、阻塞读、poll 机制。新功能预计在 `driver/` 下新增源文件，并在 `app/` 下添加对应用户态测试程序。

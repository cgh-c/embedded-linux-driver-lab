# 开发环境

## 主机环境
- OS: Ubuntu 22.04 (VMware 虚拟机, 用户 cgh, IP 192.168.152.131)
- Editor: VSCode (最新版) + Remote SSH 连接 Ubuntu
- Windows 主机: Windows 11

## 目标平台
- Board: 韦东山 i.MX6ULL (100ask 平台)
- Kernel version: Linux 4.9.88
- Cross compiler: arm-buildroot-linux-gnueabihf-gcc 7.5.0 (Buildroot 2020.02, cortex-a7, hard float)

## 关键路径
- 内核源码: `/home/cgh/Linux-4.9.88`
- 工具链: `/home/cgh/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/bin`
- 项目目录: `/home/cgh/embedded-linux-driver-lab`

## 编译驱动模块
```bash
cd driver/
make    # Makefile 已配置 ARCH=arm CROSS_COMPILE=arm-buildroot-linux-gnueabihf-
```

手动编译命令 (等价):
```bash
make ARCH=arm CROSS_COMPILE=arm-buildroot-linux-gnueabihf- -C ~/Linux-4.9.88 M=$(pwd) modules
```

## 部署到开发板
```bash
# scp 方式传输 .ko
scp driver/led_drv.ko root@<板子IP>:/tmp/

# 在板子上加载/卸载
insmod /tmp/led_drv.ko
rmmod led_drv

# 查看内核日志
dmesg | tail -20
```

## 代码跳转配置
- 使用 clangd 扩展 (需安装在 SSH 远端 Ubuntu 侧)
- `bear -- make` 生成 `compile_commands.json` (Ubuntu 22.04 语法)
- `compile_commands.json` 中编译器字段须为 `arm-buildroot-linux-gnueabihf-gcc` (不能是 `cc`)

## 环境搭建注意事项
- Makefile 必须显式指定 ARCH 和 CROSS_COMPILE (不能依赖 shell 环境变量)
- 工具链迁移必须用 `rsync -aH` (保留硬链接), scp 会展开硬链接导致体积暴涨
- Linux 4.9.88 在 Ubuntu 22.04 需要 dtc yylloc 补丁:
  ```bash
  sed -i 's/^YYLTYPE yylloc;/extern YYLTYPE yylloc;/' scripts/dtc/dtc-lexer.lex.c_shipped
  ```
- modules_prepare 成功后才能编译外部模块

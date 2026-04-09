# 调试记录

## 记录规则
- 日期
- 问题现象
- 排查过程
- 解决方法

---

## 2026-04: DTS 文件选错导致设备树节点不生效

**问题现象**: 修改了设备树文件并重新编译 DTB 部署到板子，但 `/proc/device-tree/` 下看不到新增的 `led_drv` 节点。

**排查过程**:
1. 一直在修改 `imx6ull-14x14-evk.dts`，以为这是板子对应的 DTS 文件
2. 在开发板 U-Boot 中执行 `printenv`，发现 `fdt_file=100ask_imx6ull-14x14.dtb`
3. 说明实际加载的 DTB 不是 `imx6ull-14x14-evk.dtb`，而是 `100ask_imx6ull-14x14.dtb`

**解决方法**: 在正确的文件 `100ask_imx6ull-14x14.dts` 中添加 LED 节点，重新编译 DTB 并部署。

**教训**: 实际加载的 DTB 由 U-Boot 的 `fdt_file` 变量决定，不是文件名看起来"最像"的那个。

---

## 2026-04: vmlinux modpost 编译错误 (旧机 Ubuntu 18.04)

**问题现象**: 编译 platform_driver 版本驱动时报错:
```
FATAL: section header offset=11259037725229108 in file 'vmlinux' is bigger than filesize=23661956
```

**排查过程**:
1. `file vmlinux` 和 `readelf -h vmlinux` 均显示文件正常 (ELF 32-bit LSB executable, ARM)
2. 判断为 Module.symvers 过期或 modpost 二进制与内核构建树状态不一致

**解决方法**: 迁移到 Ubuntu 22.04 新机后，重新执行 `make modules_prepare`，问题不再出现。绕过了旧机的构建树不一致问题。

---

## 2026-04: dtc yylloc 重复定义 (Ubuntu 22.04 编译 Linux 4.9.88)

**问题现象**: 在 Ubuntu 22.04 上执行 `make modules_prepare` 时报错:
```
multiple definition of 'yylloc'; dtc-parser.tab.o:(.bss+0x10): first defined here
```

**排查过程**: GCC 10+ 默认启用 `-fno-common`，导致老内核代码中 `YYLTYPE yylloc;` 的全局变量定义在多个编译单元中重复。

**解决方法**:
```bash
sed -i 's/^YYLTYPE yylloc;/extern YYLTYPE yylloc;/' scripts/dtc/dtc-lexer.lex.c_shipped
```
将变量声明改为 `extern`，避免重复定义。

**注意**: 不要 `rm -f scripts/dtc/*.lex.*`，会删掉 shipped 源文件，需要从旧机恢复。

---

## 2026-04: 工具链 scp 迁移后体积暴涨 (2.2G → 44G)

**问题现象**: 用 `scp -r` 从旧机拷贝工具链到新机后，`du -sh ~/ToolChain` 显示 44G。

**排查过程**: Buildroot 工具链内部大量使用硬链接，`scp -r` 会将每个硬链接展开为独立副本。

**解决方法**:
```bash
rm -rf ~/ToolChain
rsync -aH --progress book@旧机IP:~/100ask_imx6ull-sdk/ToolChain ~/
```
`-H` 参数保留硬链接，迁移后恢复为正常的 2.2G。

---

## 2026-04: NFS 路径错误导致 Permission denied

**问题现象**: 开发板 `mount -t nfs` 时报 `Permission denied`。

**排查过程**: 写了内核子目录作为 NFS 路径，但该路径不在 `/etc/exports` 的导出列表中。

**解决方法**: NFS 挂载路径必须是 Ubuntu 已在 `/etc/exports` 中导出的绝对路径。

---

## 2026-04: Ubuntu 双网卡路由导致开发板 ping 不通

**问题现象**: Ubuntu 虚拟机有 NAT + Host-Only 双网卡，默认路由走 NAT，导致开发板 (192.168.5.x 段) 的包无法回来。

**解决方法**:
```bash
sudo ip route add 192.168.5.0/24 dev <Host-Only网卡名>
```

# 交叉编译 sysroot 配置指南

## 概述

使用 sysroot 可以让 Zig 在交叉编译时正确链接目标设备的动态库，从而减小最终二进制文件的大小。

## 准备工作

### 1. 准备 sysroot 目录

从目标 ARM 设备复制必要的库文件到 Windows：

```bash
# 在 ARM 设备上执行
scp -r /lib user@windows:/d/Temp/nfs/sys/
scp -r /usr/lib user@windows:/d/Temp/nfs/sys/usr/
```

目标 sysroot 结构：
```
D:/Temp/nfs/sys/
├── lib/          # 系统库 (libc.so, ld-*.so, etc.)
└── usr/
    └── lib/      # 用户库 (libpthread.so, etc.)
```

### 2. 编译命令

使用 `-Dsysroot` 参数指定 sysroot 路径：

```bash
# Windows 路径格式
zig build -Dbuild-only=broker -Dtarget=arm-linux-gnueabihf --release=small -Dsysroot=D:/Temp/nfs/sys

# Linux/Unix 路径格式
zig build -Dbuild-only=broker -Dtarget=arm-linux-gnueabihf --release=small -Dsysroot=/path/to/sysroot
```

## 工作原理

### sysroot 的作用

1. **编译时**: Zig 使用 sysroot 中的库进行链接验证
2. **RPATH 设置**: 自动将 RPATH 指向 sysroot 中的库路径
3. **动态链接**: 生成较小的动态链接可执行文件

### RPATH 配置

使用 sysroot 时，build.zig 自动设置：
- `D:/Temp/nfs/sys/lib`
- `D:/Temp/nfs/sys/usr/lib`

不使用 sysroot 时，使用默认路径：
- `/lib`
- `/usr/lib`

## 文件大小对比

| 链接方式 | 大小 | 说明 |
|---------|------|------|
| 静态链接 (`-Dstatic-link`) | ~500KB | 包含所有库代码 |
| 动态链接 (sysroot) | ~53KB | 运行时加载库 |
| 动态链接 (无 sysroot) | ~53KB | 需要目标设备有对应库 |

## 部署到目标设备

### 方法 1: 使用 sysroot 路径

如果 sysroot 在网络存储上（如 NFS），可以直接运行：

```bash
# 在 ARM 设备上挂载 NFS
mount -t nfs server:/path/to/sysroot /mnt/sysroot

# 运行 broker
./mqtt_broker
```

### 方法 2: 复制到目标位置

```bash
# 复制到 ARM 设备
scp zig-out/broker/arm/linux/gnueabihf/mqtt_broker arm-device:/usr/local/bin/

# 确保 /lib 和 /usr/lib 包含必要的动态库
```

## 验证依赖

在 ARM 设备上检查依赖：

```bash
# 查看动态链接依赖
ldd ./mqtt_broker

# 预期输出示例：
#   libc.so.6 => /lib/libc.so.6
#   libpthread.so.0 => /usr/lib/libpthread.so.0
#   /lib/ld-linux-armhf.so.3 => /lib/ld-linux-armhf.so.3
```

## 常见问题

### Q: 编译成功但运行时报错 "No such file or directory"

A: 检查以下项：
1. 目标设备的动态链接器是否存在 (`/lib/ld-linux-armhf.so.3`)
2. 必要的库文件是否在 RPATH 指定的路径中
3. 使用 `readelf -d mqtt_broker | grep NEEDED` 查看需要的库

### Q: 能否使用相对路径的 RPATH？

A: 可以使用 `$ORIGIN`。修改 build.zig 中的 RPATH 设置：

```zig
broker.root_module.addRPathSpecial("$ORIGIN");
broker.root_module.addRPathSpecial("$ORIGIN/lib");
```

### Q: sysroot 需要 include 文件吗？

A: 对于 broker 编译，通常不需要。Zig 自带标准 C 库头文件。
但如果编译需要特定库的头文件（如 wolfSSL），需要包含 `/usr/include`。

## 相关文件

- [build.zig](../build.zig) - 构建配置
- [examples/broker_options_example.c](../examples/broker_options_example.c) - Broker 配置示例

## 参考资料

- Zig 交叉编译: https://ziglang.org/documentation/master/#Cross-Compilation
- RPATH 说明: https://man7.org/linux/man-pages/man8/ld.so.8.html

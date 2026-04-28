# 解决 ARM 设备 "not found" 错误

## 问题诊断

当执行 `./mqtt_broker` 时出现 `sh: ./mqtt_broker: not found` 错误，通常不是文件不存在，而是：

1. **动态链接器找不到**（最常见）
2. **缺少必要的共享库**
3. **ABI 不匹配**（硬浮点 vs 软浮点）

## 快速诊断

在目标设备上运行：

```bash
# 1. 检查 CPU 特性
cat /proc/cpuinfo | grep Features
# 如果有 vfp/vfpv3/vfpv4 = 支持硬浮点
# 如果没有 = 只支持软浮点

# 2. 查找实际存在的动态链接器
ls -la /lib/ld-*.so*
ls -la /lib/arm-linux-gnueabihf/ld-*.so* 2>/dev/null  # 硬浮点
ls -la /lib/arm-linux-gnueabi/ld-*.so* 2>/dev/null      # 软浮点

# 3. 检查 mqtt_broker 期望的动态链接器
readelf -l ./mqtt_broker | grep interpreter

# 4. 尝试详细执行
strace -e trace=open,openat,execve ./mqtt_broker
```

## 解决方案

### 方案 1：使用正确匹配的 sysroot

**如果设备支持硬浮点（有 vfp 特性）：**

```bash
# 1. 在目标设备上，复制硬浮点库
scp -r root@device:/lib/arm-linux-gnueabihf D:/Temp/nfs/sys/lib/
scp -r root@device:/lib/ld-linux-armhf.so.3* D:/Temp/nfs/sys/lib/
scp -r root@device:/usr/lib/arm-linux-gnueabihf D:/Temp/nfs/sys/usr/lib/

# 2. 编译
zig build -Dbuild-only=broker -Dtarget=arm-linux-gnueabihf --release=small -Dsysroot=D:/Temp/nfs/sys
```

**如果设备只支持软浮点（无 vfp 特性）：**

```bash
# 1. 复制软浮点库
scp -r root@device:/lib/arm-linux-gnueabi D:/Temp/nfs/sys/lib/
scp -r root@device:/lib/ld-linux.so.3* D:/Temp/nfs/sys/lib/

# 2. 编译
zig build -Dbuild-only=broker -Dtarget=arm-linux-gnueabi --release=small -Dsysroot=D:/Temp/nfs/sys
```

### 方案 2：使用静态链接（最简单）

不依赖目标设备的库：

```bash
zig build -Dbuild-only=broker -Dtarget=arm-linux-gnueabihf --release=small -Dstatic-link
```

优点：不会有 "not found" 错误
缺点：文件较大（约 500KB vs 50KB）

### 方案 3：仅复制必要的库到目标设备

```bash
# 在编译机上提取依赖
zig build -Dbuild-only=broker -Dtarget=arm-linux-gnueabihf --release=small -Dsysroot=D:/Temp/nfs/sys

# 复制到设备
scp zig-out/broker/arm/linux/gnueabihf/mqtt_broker root@device:/usr/local/bin/

# 在设备上确保动态链接器存在
ls -la /lib/ld-linux-armhf.so.3  # 或 /lib/ld-linux.so.3
# 如果不存在，从 sysroot 复制
```

### 方案 4：使用 musl libc（更小，更便携）

```bash
# musl 通常更兼容，静态链接更小
zig build -Dbuild-only=broker -Dtarget=arm-linux-musleabihf --release=small
```

## 常见错误对应表

| 动态链接器 | 目标 ABI | 设备特征 |
|-----------|---------|---------|
| `/lib/ld-linux.so.3` | `arm-linux-gnueabi` | 软浮点，老设备 |
| `/lib/ld-linux-armhf.so.3` | `arm-linux-gnueabihf` | 硬浮点，现代设备 |
| `/lib/ld-musl-armhf.so.1` | `arm-linux-musleabihf` | musl libc |

## 验证步骤

编译完成后验证：

```bash
# 1. 检查文件类型
file mqtt_broker
# 应显示: ELF 32-bit LSB executable, ARM, EABI5

# 2. 检查动态链接器
readelf -l mqtt_broker | grep interpreter
# 确保路径在目标设备上存在

# 3. 检查依赖库
readelf -d mqtt_broker | grep NEEDED
# 这些库必须在目标设备上可用

# 4. 传到设备测试
scp mqtt_broker root@device:/tmp/
ssh root@device "/tmp/mqtt_broker --version" || "/tmp/mqtt_broker -?"
```

## 推荐方案

**对于大多数情况：**

```bash
# 静态链接最可靠
zig build -Dbuild-only=broker -Dtarget=arm-linux-gnueabihf --release=small -Dstatic-link
```

**如果需要动态链接以减小体积：**

1. 先在目标设备运行 `docs/diagnose-arm.sh` 诊断
2. 根据诊断结果选择正确的 ABI
3. 准备匹配的 sysroot
4. 使用 `-Dsysroot` 编译

## 相关文件

- [diagnose-arm.sh](diagnose-arm.sh) - 设备诊断脚本
- [check-arm-device.sh](check-arm-device.sh) - 环境检查脚本

# wolfMQTT Zig 构建系统

本文档介绍如何使用 Zig 构建系统来编译 wolfMQTT 库。

## 前置要求

- Zig 0.13.0 或更高版本
- 可选：wolfSSL（用于 TLS 支持）
- 可选：libcurl（用于 curl 后端）

可用的优化模式：
模式	命令	说明
Debug	（默认）	调试模式，无优化
ReleaseSafe	--release 或 --release=safe	安全优化，保留安全检查
ReleaseFast	--release=fast	快速优化，移除安全检查
ReleaseSmall	--release=small	小体积优化，最小化二进制大小


## 基本构建


### 默认配置构建

```bash

zig build

# 最快速度

zig build -Dtarget=arm-linux-gnueabihf --release=fast

# 最小体积
zig build -Dtarget=arm-linux-gnueabihf --release=small  


zig build broker -Dstatic-link=true --release=small	

```

这将使用默认配置编译 wolfMQTT 静态库：

- ✅ 启用超时支持
- ✅ 启用错误字符串
- ✅ 启用 STDIN 捕获
- ✅ 启用断开连接回调
- ✅ 启用非阻塞支持
- ✅ 启用 MQTT v5
- ✅ 启用多线程支持
- ❌ 禁用 TLS
- ❌ 禁用 MQTT-SN
- ❌ 禁用 Broker

### 查看所有可用选项

```bash
zig build --help
```

## 高级选项


### 启用 TLS 支持

需要安装 wolfSSL：

```bash
# Ubuntu/Debian
sudo apt-get install libwolfssl-dev

# macOS
brew install wolfssl

# 或从源码编译安装
```

然后使用 `-Dtls=true` 选项：

```bash
zig build -Dtls=true
```

如果 wolfSSL 安装在非标准路径：

```bash
zig build -Dtls=true -Dwolfssl-path=/path/to/wolfssl
```

### 启用 MQTT-SN 支持

```bash
zig build -Dmqtt-sn=true

```

### 启用非阻塞模式

```bash
zig build -Dnonblock=true
```

### 启用 MQTT v5 支持

```bash
zig build -Dv5=true
```

### 启用多线程支持

```bash
zig build -Dmt=true
```

### 启用 Broker

```bash
zig build -Dbroker=true
```

这将构建 `mqtt_broker` 可执行文件。

### Broker 选项

```bash
zig build -Dbroker=true \
         -Dbroker-retained=true \
         -Dbroker-will=true \
         -Dbroker-wildcards=true \
         -Dbroker-auth=true \
         -Dbroker-log=true \
         -Dbroker-insecure=true \
         -Dbroker-debug=true
```

#### Broker 调试模式

启用 `-Dbroker-debug=true` 会输出详细的调试信息：
- 客户端 IP 地址追踪
- 连接/断开统计
- PUBLISH 消息流转详情
- MQTT v5 属性信息（响应主题、关联数据、会话过期间隔）
- 订阅/退订活动
- 会话过期事件

详见 [BROKER_DEBUG_GUIDE.md](tests/BROKER_DEBUG_GUIDE.md)

#### MQTT v5 支持

Broker 完整支持 MQTT v5 协议特性：
- ✅ 响应主题 (Response Topic)
- ✅ 关联数据 (Correlation Data)
- ✅ Clean Start
- ✅ 会话过期间隔 (Session Expiry Interval)

详见：
- [MQTT_V5_RESPONSE_TOPIC.md](tests/MQTT_V5_RESPONSE_TOPIC.md)
- [MQTT_V5_SESSION_EXPIRY.md](tests/MQTT_V5_SESSION_EXPIRY.md)

### 组合选项

可以组合多个选项：

```bash
# 启用 TLS、MQTT-SN 和非阻塞模式
zig build -Dtls=true -Dmqtt-sn=true -Dnonblock=true

# 启用所有常见功能
zig build \
  -Dtls=true \
  -Dmqtt-sn=true \
  -Dnonblock=true \
  -Dv5=true \
  -Dmt=true \
  -Dbroker=true
```

## 优化和目标

### Debug 构建（默认）

```bash
zig build
```

### Release 构建

```bash
zig build -Doptimize=ReleaseFast
# 或
zig build -Doptimize=ReleaseSmall
# 或
zig build -Doptimize=ReleaseSafe
```

### 交叉编译

```bash
# 为 Windows 构建
zig build -Dtarget=x86_64-windows-gnu

# 为 Linux 构建
zig build -Dtarget=x86_64-linux-gnu

# 为 macOS 构建（从 Linux）
zig build -Dtarget=x86_64-macos-gnu

# 为 ARM64 Linux 构建
zig build -Dtarget=aarch64-linux-gnu
```

## 构建产物

构建完成后，产物位于 `zig-out/` 目录：

```
zig-out/
├── lib/
│   └── libwolfmqtt.a    # 静态库
└── bin/
    └── mqtt_broker       # Broker 可执行文件（如果启用）
```

## 安装

```bash
# 安装到系统
zig build install

# 安装到前缀
zig build install --prefix /usr/local
```

## 清理

```bash
zig build clean
```

## 格式化构建脚本

```bash
zig build fmt
```

## 与 CMake 构建的比较

| CMake 选项 | Zig 等价选项 |
|-----------|-------------|
| `-DWOLFMQTT_TLS=yes` | `-Dtls=true` |
| `-DWOLFMQTT_SN=yes` | `-Dmqtt-sn=true` |
| `-DWOLFMQTT_NONBLOCK=yes` | `-Dnonblock=true` |
| `-DWOLFMQTT_V5=yes` | `-Dv5=true` |
| `-DWOLFMQTT_MT=yes` | `-Dmt=true` |
| `-DWOLFMQTT_BROKER=yes` | `-Dbroker=true` |

## 集成到 Zig 项目

在你的 `build.zig` 中添加：

```zig
const wolfmqtt = b.dependency("wolfmqtt", .{
    .target = target,
    .optimize = optimize,
    .tls = true,
    .mqtt_sn = false,
});

const wolfmqtt_module = wolfmqtt.module("wolfmqtt");
your_exe.linkLibrary(wolfmqtt.artifact("wolfmqtt"));
```

## 故障排除

### 找不到 wolfSSL

确保设置了正确的 wolfSSL 路径：

```bash
zig build -Dtls=true -Dwolfssl-path=/usr/local
```

### 链接错误

确保所有依赖项已安装。在 Linux 上：

```bash
sudo apt-get install build-essential
```

### Windows 构建

Windows 构建需要适当的工具链。建议使用 MSYS2 或 Zig 的本机 Windows 支持。

## 许可证

wolfMQTT 使用 GPLv3 许可证。详见 LICENSE 文件。


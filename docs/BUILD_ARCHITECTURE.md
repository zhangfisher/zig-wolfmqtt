# wolfMQTT 构建架构说明

## 📋 正确的架构设计

### 组件划分

```
┌─────────────────────────────────────────────────────┐
│           wolfMQTT 客户端库 (libwolfmqtt.a)          │
├─────────────────────────────────────────────────────┤
│ 核心 MQTT 客户端功能:                                │
│  - mqtt_client.c     (MQTT 客户端 API)              │
│  - mqtt_packet.c     (MQTT 包编码/解码)             │
│  - mqtt_socket.c     (网络传输层)                   │
│  - logger.c          (日志系统)                     │
│                                                       │
│ 可选功能:                                            │
│  - mqtt_sn_client.c  (MQTT-SN 客户端)              │
│  - mqtt_sn_packet.c  (MQTT-SN 包处理)              │
└─────────────────────────────────────────────────────┘
                    ↓ linkLibrary()
┌─────────────────────────────────────────────────────┐
│         Broker 可执行文件 (mqtt_broker)              │
├─────────────────────────────────────────────────────┤
│ Broker 特有代码 (单独编译，不在库中):                 │
│  - mqtt_broker.c            (Broker 主逻辑)         │
│  - mqtt_broker_transport.c  (统一传输层)            │
│  - mqtt_websocket.c         (WebSocket 支持)*       │
│                                                       │
│ 链接:                                                │
│  - libwolfmqtt.a           (MQTT 客户端库)          │
│  - libwslay.a              (WebSocket 库)*          │
│  - libc (musl)             (C 标准库)               │
└─────────────────────────────────────────────────────┘

* 仅在启用 WebSocket 时包含
```

## 🔧 构建配置

### 1. wolfmqtt 库 (`build/entries/wolfmqtt.zig`)

**只包含客户端相关代码**：

```zig
// 核心源文件
const core_sources = [_][]const u8{
    "src/mqtt_client.c",
    "src/mqtt_packet.c",
    "src/mqtt_socket.c",
    "src/logger.c",
};

// MQTT-SN 源文件（可选）
if (opts.mqtt_sn) {
    module.addCSourceFile(.{ .file = b.path("src/mqtt_sn_client.c") });
    module.addCSourceFile(.{ .file = b.path("src/mqtt_sn_packet.c") });
}

// ⚠️ 注意: Broker 相关代码 NOT 在这里添加！
```

### 2. Broker 可执行文件 (`build/entries/broker.zig`)

**单独编译 Broker 特有代码**：

```zig
// Broker 特有源文件（不在 wolfmqtt 库中）
broker_root.addCSourceFile(.{ .file = b.path("src/mqtt_broker.c") });
broker_root.addCSourceFile(.{ .file = b.path("src/mqtt_broker_transport.c") });

// WebSocket 支持（可选）
if (opts.websocket) {
    broker_root.addCSourceFile(.{ .file = b.path("src/mqtt_websocket.c") });
    broker_root.addIncludePath(b.path("libs/wslay/include"));
}

// 链接 wolfmqtt 客户端库
broker.root_module.linkLibrary(lib);

// 链接 wslay 库（如果启用 WebSocket）
if (opts.websocket) {
    if (target.result.os.tag == .linux and target.result.cpu.arch != .x86_64) {
        // ARM Linux 交叉编译：直接链接静态库文件
        broker.root_module.addObjectFile(b.path("libs/wslay/lib/libwslay.a"));
    } else {
        // 本机编译：使用 linkSystemLibrary
        broker.root_module.linkSystemLibrary("wslay", .{});
    }
}
```

## ❌ 之前的错误架构

```
错误做法：
┌──────────────────────────────────────────┐
│      libwolfmqtt.a (包含了 Broker!)      │
│  - mqtt_client.c                         │
│  - mqtt_packet.c                         │
│  - mqtt_broker.c      ← 不应该在这里!   │
│  - mqtt_broker_transport.c ← 不应该在这!│
│  - mqtt_websocket.c     ← 不应该在这里! │
└──────────────────────────────────────────┘
              ↓
┌──────────────────────────────────────────┐
│      broker 可执行文件                    │
│  - 又编译了一次 mqtt_broker.c            │
│  - 链接 libwolfmqtt.a                    │
└──────────────────────────────────────────┘

结果: 重复符号错误 (duplicate symbol)
```

## ✅ 修复后的正确架构

```
正确做法：
┌──────────────────────────────────────────┐
│      libwolfmqtt.a (纯客户端库)          │
│  - mqtt_client.c                         │
│  - mqtt_packet.c                         │
│  - mqtt_socket.c                         │
│  - logger.c                              │
└──────────────────────────────────────────┘
              ↓ linkLibrary()
┌──────────────────────────────────────────┐
│      broker 可执行文件                    │
│  - mqtt_broker.c        (单独编译)       │
│  - mqtt_broker_transport.c (单独编译)    │
│  - mqtt_websocket.c     (单独编译)*      │
│  + libwolfmqtt.a        (链接库)         │
└──────────────────────────────────────────┘

结果: 无重复符号，架构清晰
```

## 🎯 关键原则

### 1. 职责分离

- **wolfmqtt 库** = MQTT 协议客户端实现
  - 用于嵌入式设备、IoT 应用
  - 轻量级、可移植
  - 不包含服务器端代码

- **Broker 可执行文件** = MQTT 服务器实现
  - 独立的可执行程序
  - 可以链接 wolfmqtt 库复用客户端代码
  - 包含服务器特有的功能（订阅管理、消息路由等）

### 2. 避免符号重复

- 同一个 `.c` 文件不能在库和可执行文件中都编译
- 如果库中已经编译了，可执行文件应该通过 `linkLibrary()` 链接
- 如果需要单独编译，就不要添加到库中

### 3. 模块化设计

```
wolfmqtt 库可以被多个项目使用:
  - 嵌入式 MQTT 客户端
  - PC 端 MQTT 测试工具
  - Broker 可执行文件（作为依赖）
  
每个使用者可以根据自己的需求选择功能:
  - 只需要客户端 → 只链接 libwolfmqtt.a
  - 需要 Broker → 单独编译 broker 代码 + 链接库
  - 需要 MQTT-SN → 编译时启用 opts.mqtt_sn
```

## 📦 编译命令

### 编译 wolfmqtt 客户端库

```bash
# 基础客户端库
zig build lib -Dstatic-link=true --release=small

# 带 MQTT-SN 支持
zig build lib -Dstatic-link=true -Dmqtt-sn=true --release=small
```

### 编译 Broker 可执行文件

```bash
# 基础 Broker（无 WebSocket）
zig build broker -Dstatic-link=true --release=small

# 带 WebSocket 支持
zig build broker -Dstatic-link=true -Dwebsocket=true --release=small
```

## 🔍 验证方法

### 检查库中包含的符号

```bash
# Linux/macOS
nm zig-out/lib/libwolfmqtt.a | grep MqttBroker

# 正确的结果: 不应该有 MqttBroker_* 符号
# 错误的结果: 如果有 MqttBroker_Init 等符号，说明架构错了
```

### 检查可执行文件的依赖

```bash
# Linux
ldd zig-out/broker/linux-arm/mqtt_broker-musleabihf

# 应该显示:
# - 链接了 musl libc
# - 如果是静态链接，没有动态依赖
```

## 📝 修改的文件

1. **build/utils/modules.zig**
   - 移除了 Broker 相关源文件的添加逻辑
   - 添加了注释说明 Broker 代码不在库中

2. **build/entries/broker.zig**
   - 添加了 Broker 特有源文件的编译
   - 添加了 WebSocket 源文件的条件编译
   - 添加了 wslay 库的链接逻辑

## 🚀 下一步

### 为 ARM Linux 编译 wslay

当前 `libs/wslay/lib/libwslay.a` 是 x86_64 Windows 版本，不能用于 ARM Linux 交叉编译。

需要：
1. 在 ARM Linux 环境或使用交叉编译工具链编译 wslay
2. 将生成的 `libwslay.a` 放到合适的位置
3. 或者使用 Zig 的包管理器自动下载和编译 wslay

### 临时解决方案

如果暂时不需要 WebSocket 支持，可以：
```bash
# 编译不带 WebSocket 的 Broker
zig build broker -Dstatic-link=true --release=small
```

这样生成的 Broker 仍然支持：
- ✅ TCP 连接 (端口 1883)
- ✅ TLS 连接 (端口 8883，如果启用)
- ❌ WebSocket 连接 (需要 wslay 库)

---

**总结**: wolfmqtt 是一个纯客户端库，Broker 是独立的可执行文件，两者通过清晰的接口分离，避免了符号重复和架构混乱。

# Broker 统一传输层 - 快速参考

## 一分钟了解

wolfMQTT Broker 现在使用统一的传输层接口，让 TCP、TLS 和 WebSocket 共享相同的 API。

```c
// 之前：到处都是 #ifdef
#ifdef ENABLE_MQTT_WEBSOCKET
    if (bc->is_websocket) {
        // WebSocket 逻辑...
    } else
#endif
{
    // TCP 逻辑...
}

// 之后：一行搞定！
BrokerTransport_Read(bc, buf, len, timeout);
```

## 核心 API

### 初始化
```c
// TCP 客户端
BrokerTransport_Init(bc, BROKER_TRANSPORT_TCP, broker);

// TLS 客户端
BrokerTransport_Init(bc, BROKER_TRANSPORT_TLS, broker);

// WebSocket 客户端
BrokerTransport_Init(bc, BROKER_TRANSPORT_WEBSOCKET, broker);
```

### 握手
```c
// TCP: 立即返回成功
// TLS: 执行 SSL_accept()
// WebSocket: 执行 HTTP Upgrade 握手
rc = BrokerTransport_Handshake(bc, broker);
```

### 数据读写
```c
// 读取数据（所有传输类型统一）
rc = BrokerTransport_Read(bc, buf, buf_len, timeout_ms);

// 写入数据（所有传输类型统一）
rc = BrokerTransport_Write(bc, buf, buf_len, timeout_ms);
```

### 清理
```c
// 关闭连接
BrokerTransport_Close(bc, broker);

// 释放资源
BrokerTransport_Cleanup(bc);

// 获取名称（用于日志）
printf("Client transport: %s\n", BrokerTransport_GetName(bc));
// 输出: "TCP" / "TLS" / "WebSocket"
```

## 构建命令

```bash
# 仅 TCP
zig build -Dbroker=true

# TCP + TLS
zig build -Dbroker=true -Dtls=true -Dwolfssl-path=/path/to/wolfssl

# TCP + WebSocket
zig build -Dbroker=true -Dwebsocket=true

# 全部协议
zig build -Dbroker=true -Dtls=true -Dwebsocket=true -Dwolfssl-path=/path/to/wolfssl
```

## 测试

```bash
# 编译测试程序
zig build-exe examples/broker_transport_test.c \
    -I wolfmqtt -I . -L zig-out/lib -lwolfmqtt

# 运行测试
./broker_transport_test
```

## 添加新传输类型（如 QUIC）

只需 3 步：

### 1. 实现 ops
```c
static int quic_init(BrokerClient* bc) { ... }
static int quic_handshake(BrokerClient* bc, MqttBroker* broker) { ... }
static int quic_read(BrokerClient* bc, byte* buf, int len, int timeout) { ... }
static int quic_write(BrokerClient* bc, const byte* buf, int len, int timeout) { ... }
static void quic_close(BrokerClient* bc, MqttBroker* broker) { ... }
static void quic_cleanup(BrokerClient* bc) { ... }
static const char* quic_get_name(BrokerClient* bc) { return "QUIC"; }

static const BrokerTransportOps quic_ops = {
    .init = quic_init,
    .handshake = quic_handshake,
    .read = quic_read,
    .write = quic_write,
    .close = quic_close,
    .cleanup = quic_cleanup,
    .get_name = quic_get_name
};
```

### 2. 添加枚举值
```c
typedef enum {
    BROKER_TRANSPORT_TCP = 0,
    BROKER_TRANSPORT_TLS,
    BROKER_TRANSPORT_WEBSOCKET,
    BROKER_TRANSPORT_QUIC,  // 新增
    BROKER_TRANSPORT_MAX
} BrokerTransportType;
```

### 3. 在 Init 中添加 case
```c
int BrokerTransport_Init(BrokerClient* bc, BrokerTransportType type, MqttBroker* broker)
{
    switch (type) {
        // ... existing cases ...
        
        case BROKER_TRANSPORT_QUIC:
            bc->transport.ops = &quic_ops;
            break;
    }
    // ...
}
```

**无需修改主循环或业务逻辑！** 🎉

## 常见问题

### Q: 为什么要用虚函数表？
A: C 语言没有原生多态，通过函数指针模拟 C++ 的虚函数，实现运行时多态。

### Q: 性能有影响吗？
A: 几乎为零。函数指针调用和直接调用的性能差异可以忽略不计。

### Q: 如何调试传输类型？
A: 使用 `BrokerTransport_GetName(bc)` 打印当前使用的传输类型。

### Q: WebSocket 读取为什么返回 NOT_IMPLEMENTED？
A: 需要从 wslay 回调中提取 MQTT 包，这部分工作待完成。

## 文件位置

```
wolfmqtt/
├── mqtt_broker_transport.h      # 接口定义 ⭐
└── ...

src/
├── mqtt_broker_transport.c      # 三种传输实现 ⭐
├── mqtt_broker.c                # 使用统一接口
└── ...

docs/
├── BROKER_TRANSPORT_LAYER.md           # 详细文档
├── BROKER_TRANSPORT_IMPLEMENTATION.md  # 实施总结
└── BROKER_TRANSPORT_QUICKSTART.md      # 本文档 ⭐

examples/
└── broker_transport_test.c      # 测试程序
```

## 下一步

要完全集成传输层，需要：

1. ✅ ~~创建传输层接口~~ - 已完成
2. ✅ ~~实现三种传输类型~~ - 已完成
3. ⏳ 在主循环中使用 `BrokerTransport_Init()` - 待完成
4. ⏳ 在 `BrokerClient_Process()` 中使用统一接口 - 待完成
5. ⏳ 完善 WebSocket 数据提取 - 待完成

查看 [BROKER_TRANSPORT_IMPLEMENTATION.md](BROKER_TRANSPORT_IMPLEMENTATION.md) 了解详细计划。

---

**享受简洁的代码吧！** 😊

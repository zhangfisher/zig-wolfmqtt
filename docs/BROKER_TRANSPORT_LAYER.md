# Broker 统一传输层架构

## 概述

wolfMQTT Broker 采用统一的传输层抽象，使得同时支持 TCP、TLS 和 WebSocket 变得简单且易于维护。

## 架构设计

```
┌─────────────────────────────────────┐
│      MqttBroker (业务逻辑)           │
└──────────────┬──────────────────────┘
               │ 使用统一 API
┌──────────────▼──────────────────────┐
│   BrokerTransport (传输抽象层)       │
│  ┌──────────┐    ┌──────────────┐   │
│  │ TCP      │    │  WebSocket   │   │
│  │ Transport│    │  Transport   │   │
│  └──────────┘    └──────────────┘   │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│   底层网络 (socket / wslay)          │
└─────────────────────────────────────┘
```

## 核心优势

### 1. 统一的 API

所有传输类型使用相同的接口：

```c
// 初始化
BrokerTransport_Init(bc, BROKER_TRANSPORT_TCP, broker);

// 握手
BrokerTransport_Handshake(bc, broker);

// 读取数据
BrokerTransport_Read(bc, buf, len, timeout);

// 写入数据
BrokerTransport_Write(bc, buf, len, timeout);

// 关闭
BrokerTransport_Close(bc, broker);

// 清理
BrokerTransport_Cleanup(bc);
```

### 2. 传输层无关的业务逻辑

`BrokerClient_Process()` 不需要关心底层是 TCP 还是 WebSocket：

```c
static int BrokerClient_Process(MqttBroker* broker, BrokerClient* bc)
{
    // 执行握手（如果需要）
    if (!BrokerTransport_IsHandshakeDone(bc)) {
        rc = BrokerTransport_Handshake(bc, broker);
        // ...
    }
    
    // 读取数据 - 不关心传输类型
    rc = BrokerTransport_Read(bc, bc->rx_buf, BROKER_CLIENT_RX_SZ(bc), 0);
    
    // 处理 MQTT 包 - 与传输层无关
    // ...
    
    // 发送响应 - 不关心传输类型
    BrokerTransport_Write(bc, tx_buf, tx_len, timeout);
}
```

### 3. 易于扩展

添加新的传输类型（如 QUIC）只需：

1. 实现新的 `BrokerTransportOps`
2. 在 `BrokerTransport_Init()` 中添加一个 case
3. 无需修改主循环或业务逻辑

## 支持的传输类型

### TCP (默认)
- **端口**: 1883
- **特点**: 简单、高效
- **适用场景**: 内网、可信网络

### TLS (可选)
- **端口**: 8883
- **特点**: 加密、安全
- **依赖**: wolfSSL
- **适用场景**: 公网、需要加密的场景

### WebSocket (可选)
- **端口**: 8080
- **特点**: 浏览器友好、穿透防火墙
- **依赖**: wslay
- **适用场景**: Web 应用、移动端

## 使用示例

### 启动多协议 Broker

```c
MqttBroker broker;
MqttBroker_Init(&broker);

// 配置普通 MQTT (TCP)
broker.port = 1883;

// 启动 Broker
MqttBroker_Start(&broker);

#ifdef ENABLE_MQTT_WEBSOCKET
// 启动 WebSocket 监听
MqttBroker_StartWebSocket(&broker, 8080);
#endif

// 运行
MqttBroker_Run(&broker);
```

### 客户端连接

```javascript
// JavaScript - WebSocket
const client = mqtt.connect('ws://localhost:8080/mqtt');
```

```python
# Python - WebSocket  
import paho.mqtt.client as mqtt
client = mqtt.Client()
client.ws_set_options(path="/mqtt")
client.connect("localhost", 8080)
```

```c
// C - TCP
MqttClient_NetConnect(&client, "localhost", 1883, ...);
```

## 实现细节

### 文件结构

```
wolfmqtt/
├── mqtt_broker_transport.h    # 传输层接口定义
└── ...

src/
├── mqtt_broker_transport.c    # 传输层实现
├── mqtt_broker.c              # Broker 主逻辑（使用统一接口）
├── mqtt_websocket.c           # WebSocket 实现
└── ...
```

### 关键函数

#### BrokerTransport_Init()
根据传输类型初始化相应的操作表：
- **TCP**: 直接使用 socket
- **TLS**: 初始化 wolfSSL 上下文
- **WebSocket**: 初始化 wslay 上下文

#### BrokerTransport_Handshake()
- **TCP**: 立即返回成功
- **TLS**: 执行 SSL_accept()
- **WebSocket**: 执行 HTTP Upgrade 握手

#### BrokerTransport_Read/Write()
委托给具体传输类型的实现：
- **TCP/TLS**: 直接调用 socket read/write
- **WebSocket**: 通过 wslay 进行帧处理

## 构建配置

### 启用 WebSocket 支持

```bash
zig build -Dbroker=true -Dwebsocket=true
```

### 启用 TLS 支持

```bash
zig build -Dbroker=true -Dtls=true -Dwolfssl-path=/path/to/wolfssl
```

### 同时启用所有协议

```bash
zig build -Dbroker=true -Dtls=true -Dwebsocket=true -Dwolfssl-path=/path/to/wolfssl
```

## 性能考虑

- **零拷贝**: 尽可能减少数据复制
- **异步 I/O**: 支持非阻塞操作
- **资源管理**: 自动清理传输层资源

## 调试技巧

### 查看传输类型

```c
printf("Client using transport: %s\n", 
       BrokerTransport_GetName(bc));
```

输出示例：
```
Client using transport: TCP
Client using transport: TLS
Client using transport: WebSocket
```

### 启用详细日志

构建时添加：
```bash
zig build -Dbroker-debug=true
```

## 未来扩展

可能的传输类型：
- **QUIC** (HTTP/3)
- **Unix Domain Socket**
- **Shared Memory** (进程间通信)
- **Custom Protocol**

只需实现新的 `BrokerTransportOps` 即可！

## 技术要点

### 虚函数表模式

使用 C 语言实现类似 C++ 的多态：

```c
typedef struct BrokerTransportOps {
    int (*init)(BrokerClient* bc);
    int (*handshake)(BrokerClient* bc, MqttBroker* broker);
    int (*read)(BrokerClient* bc, byte* buf, int buf_len, int timeout_ms);
    // ... 其他函数指针
} BrokerTransportOps;
```

每个传输类型实现自己的 ops 结构：

```c
static const BrokerTransportOps tcp_ops = {
    .init = tcp_init,
    .handshake = tcp_handshake,
    .read = tcp_read,
    .write = tcp_write,
    // ...
};
```

### 条件编译集中化

所有 `#ifdef` 集中在传输层实现中，业务代码保持干净：

```c
// mqtt_broker_transport.c 中
#ifdef ENABLE_MQTT_WEBSOCKET
static const BrokerTransportOps ws_ops = { ... };
#endif

// mqtt_broker.c 中 - 干净的代码
rc = BrokerTransport_Read(bc, buf, len, timeout);
```

## 总结

统一传输层架构让 wolfMQTT Broker：

✅ **代码更清晰** - 消除大量 `#ifdef` 分支  
✅ **更易维护** - 传输逻辑集中在一个模块  
✅ **更易扩展** - 添加新协议无需修改主循环  
✅ **同时支持多种协议** - TCP + TLS + WebSocket  

这是现代网络编程的最佳实践！

---

**相关文档**:
- [WEBSOCKET_SUPPORT.md](WEBSOCKET_SUPPORT.md) - WebSocket 功能详细说明
- [BROKER_RUNTIME_OPTIONS.md](BROKER_RUNTIME_OPTIONS.md) - Broker 运行时配置
- [BUILD_REFACTORING.md](BUILD_REFACTORING.md) - 构建系统重构说明

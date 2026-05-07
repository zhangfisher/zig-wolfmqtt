# WebSocket 集成完成 - 使用指南

## 🎉 实施完成

已成功在 wolfMQTT Broker 主循环中集成统一传输层，现在客户端可以通过 `ws://` 连接 Broker 实现完整的 MQTT 功能！

## ✅ 已完成的工作

### 1. 主循环集成

#### 接受 WebSocket 连接
在 `MqttBroker_Step()` 中添加了 WebSocket 监听器：

```c
#ifdef ENABLE_MQTT_WEBSOCKET
    if (broker->use_ws && broker->listen_sock_ws != BROKER_SOCKET_INVALID) {
        BROKER_SOCKET_T new_sock = BROKER_SOCKET_INVALID;
        rc = broker->net.accept(broker->net.ctx, broker->listen_sock_ws, &new_sock);
        if (rc == MQTT_CODE_SUCCESS && new_sock != BROKER_SOCKET_INVALID) {
            BrokerClient* bc = BrokerClient_Add(broker, new_sock, 0);
            if (bc != NULL) {
                /* Initialize WebSocket transport */
                rc = BrokerTransport_Init(bc, BROKER_TRANSPORT_WEBSOCKET, broker);
                // ...
            }
        }
    }
#endif
```

#### 初始化 TCP/TLS 传输
在 `BrokerClient_Add()` 中为所有客户端初始化传输层：

```c
/* TCP 客户端 */
BrokerTransport_Init(bc, BROKER_TRANSPORT_TCP, broker);

/* TLS 客户端 */
BrokerTransport_Init(bc, BROKER_TRANSPORT_TLS, broker);

/* WebSocket 客户端（在主循环中） */
BrokerTransport_Init(bc, BROKER_TRANSPORT_WEBSOCKET, broker);
```

### 2. 握手处理

在 `BrokerClient_Process()` 开头添加统一握手逻辑：

```c
/* Execute transport layer handshake if needed */
if (!BrokerTransport_IsHandshakeDone(bc)) {
    rc = BrokerTransport_Handshake(bc, broker);
    if (rc == MQTT_CODE_CONTINUE) {
        return 0; /* Handshake in progress */
    }
    if (rc != MQTT_CODE_SUCCESS) {
        /* Handshake failed, disconnect */
        BrokerTransport_Close(bc, broker);
        BrokerTransport_Cleanup(bc);
        BrokerClient_Remove(broker, bc, -1);
        return 0;
    }
    return 1; /* Activity - handshake completed */
}
```

**工作原理：**
- **TCP**: `is_handshake_done()` 返回 1，立即跳过
- **TLS**: 执行 `wolfSSL_accept()`
- **WebSocket**: 读取 HTTP Upgrade 请求并返回 101 响应

### 3. 数据收发统一

修改 `BrokerNetRead/Write` 使用传输层接口：

```c
static int BrokerNetRead(void* context, byte* buf, int buf_len, int timeout_ms)
{
    BrokerClient* bc = (BrokerClient*)context;
    /* Use unified transport layer */
    return BrokerTransport_Read(bc, buf, buf_len, timeout_ms);
}

static int BrokerNetWrite(void* context, const byte* buf, int buf_len, int timeout_ms)
{
    BrokerClient* bc = (BrokerClient*)context;
    /* Use unified transport layer */
    return BrokerTransport_Write(bc, buf, buf_len, timeout_ms);
}
```

**优势：**
- `MqttPacket_Read()` 内部调用 `BrokerNetRead()`
- 自动根据传输类型选择正确的读取方式
- WebSocket 通过 wslay 解帧，提取 MQTT 包
- TCP/TLS 直接读取 socket

### 4. WebSocket 数据提取完善

#### 添加接收缓冲区
在 `MqttWebSocketContext` 中添加：
```c
byte* recv_buf;         /* 接收数据缓冲区 */
word32 recv_len;        /* 接收数据长度 */
word32 recv_capacity;   /* 缓冲区容量 */
```

#### 修改回调函数
在 `ws_on_msg_recv_callback` 中存储数据：
```c
/* 复制消息到接收缓冲区 */
XMEMCPY(ws_ctx->recv_buf, arg->msg, arg->msg_length);
ws_ctx->recv_len = (word32)arg->msg_length;
```

#### 修改读取函数
`MqttWebSocket_Recv()` 返回接收到的字节数：
```c
int MqttWebSocket_Recv(MqttWebSocketContext* ws_ctx)
{
    /* 调用 wslay 接收数据 */
    rc = wslay_event_recv(ws_ctx->wslay_ctx);
    
    /* 检查是否接收到数据 */
    if (ws_ctx->recv_len > 0) {
        return (int)ws_ctx->recv_len;  /* 返回接收到的字节数 */
    }
    
    return MQTT_CODE_SUCCESS;  /* 没有数据 */
}
```

#### 传输层读取实现
```c
static int ws_read(BrokerClient* bc, byte* buf, int buf_len, int timeout_ms)
{
    MqttWebSocketContext* ws_ctx = (MqttWebSocketContext*)bc->transport.context;
    
    /* 通过 wslay 接收 WebSocket 帧 */
    rc = MqttWebSocket_Recv(ws_ctx);
    
    /* 复制数据到调用者提供的缓冲区 */
    XMEMCPY(buf, ws_ctx->recv_buf, rc);
    
    return rc;  /* 返回实际读取的字节数 */
}
```

### 5. 资源清理

在 `BrokerClient_Remove()` 中添加传输层清理：

```c
/* Cleanup transport layer resources */
BrokerTransport_Close(bc, broker);
BrokerTransport_Cleanup(bc);
```

**清理流程：**
- **TCP**: 关闭 socket
- **TLS**: SSL shutdown + free + close socket
- **WebSocket**: 发送 close 帧 + 释放 wslay 上下文 + 关闭 socket

## 🚀 使用方法

### 构建支持 WebSocket 的 Broker

```bash
# 启用 WebSocket 支持
zig build -Dbroker=true -Dwebsocket=true

# 同时启用 TLS（可选）
zig build -Dbroker=true -Dtls=true -Dwebsocket=true -Dwolfssl-path=/path/to/wolfssl
```

### 启动 Broker

```c
#include "wolfmqtt/mqtt_broker.h"

int main() {
    MqttBroker broker;
    
    /* 初始化 Broker */
    MqttBroker_Init(&broker);
    
    /* 配置端口 */
    broker.port = 1883;           // MQTT over TCP
    
    /* 启动 MQTT 监听 */
    MqttBroker_Start(&broker);
    
#ifdef ENABLE_MQTT_WEBSOCKET
    /* 启动 WebSocket 监听 */
    MqttBroker_StartWebSocket(&broker, 8080);
#endif
    
    /* 运行 Broker */
    MqttBroker_Run(&broker);
    
    /* 清理 */
    MqttBroker_Free(&broker);
    return 0;
}
```

### 客户端连接示例

#### JavaScript (浏览器/Node.js)

```javascript
const mqtt = require('mqtt');

// 连接到 WebSocket Broker
const client = mqtt.connect('ws://localhost:8080/mqtt');

client.on('connect', () => {
    console.log('Connected to broker via WebSocket!');
    
    // 订阅主题
    client.subscribe('test/topic');
    
    // 发布消息
    client.publish('test/topic', 'Hello from WebSocket!');
});

client.on('message', (topic, message) => {
    console.log(`Received: ${message.toString()} on ${topic}`);
});
```

#### Python

```python
import paho.mqtt.client as mqtt

def on_connect(client, userdata, flags, rc):
    print(f"Connected with result code {rc}")
    client.subscribe("test/topic")

def on_message(client, userdata, msg):
    print(f"Received: {msg.payload.decode()} on {msg.topic}")

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

# 配置 WebSocket
client.ws_set_options(path="/mqtt")
client.connect("localhost", 8080, 60)

client.loop_forever()
```

#### C (使用 wolfMQTT Client)

```c
#include "wolfmqtt/mqtt_client.h"

MqttClient client;
MqttNet net;

/* 初始化客户端 */
MqttClient_Init(&client, &net, NULL, tx_buf, tx_len, rx_buf, rx_len, timeout);

/* 连接到 WebSocket Broker */
/* 注意：当前 wolfMQTT client 需要额外配置才能使用 WebSocket */
/* 这里展示的是概念，实际实现需要 client 端也支持 WebSocket */
```

## 📊 工作流程图

```
客户端                          Broker
  |                               |
  |--- TCP Connect -------------->|
  |                               |
  |--- HTTP GET /mqtt ----------->|  (WebSocket 握手)
  |     Upgrade: websocket        |
  |     Sec-WebSocket-Key: xxx    |
  |                               |
  |<-- HTTP 101 Switching --------|  (握手响应)
  |     Protocols                 |
  |     Sec-WebSocket-Accept: yyy |
  |                               |
  |=== WebSocket Connection ======|  (协议切换)
  |                               |
  |--- [WS Frame: MQTT CONNECT] ->|  (wslay 解帧)
  |                               |--- 提取 MQTT 包
  |                               |--- 处理 CONNECT
  |<-- [WS Frame: CONNACK] -------|  (wslay 封帧)
  |                               |
  |--- [WS Frame: PUBLISH] ------>|  
  |                               |--- 路由消息
  |<-- [WS Frame: PUBACK] --------|
  |                               |
  |--- [WS Frame: DISCONNECT] --->|
  |                               |--- 清理资源
  |                               |
  |<== Connection Closed =========|
```

## 🔍 调试技巧

### 1. 查看传输类型

在日志中可以看到每个客户端使用的传输类型：

```c
WBLOG_INFO(broker, "New connection on sock=%d transport=%s",
          (int)bc->sock, BrokerTransport_GetName(bc));
```

输出示例：
```
New connection on sock=12345 transport=TCP
New connection on sock=12346 transport=TLS
New connection on sock=12347 transport=WebSocket
Transport handshake completed sock=12347 transport=WebSocket
```

### 2. 启用详细日志

```bash
zig build -Dbroker=true -Dwebsocket=true -Dbroker-debug=true
```

### 3. 使用 Wireshark 抓包

过滤 WebSocket 流量：
```
websocket
```

可以看到：
- HTTP Upgrade 握手
- WebSocket 帧
- 内部的 MQTT 包

## ⚠️ 注意事项

### 1. WebSocket 路径

当前实现接受任何路径的 WebSocket 连接。如果需要限制路径（如 `/mqtt`），可以在 `ws_handshake()` 中添加检查：

```c
/* 检查请求路径 */
if (strstr((char*)rx_buf, "GET /mqtt") == NULL) {
    WBLOG_ERR(broker, "Invalid WebSocket path");
    return MQTT_CODE_ERROR_BAD_ARG;
}
```

### 2. 子协议协商

当前实现在握手响应中包含 `Sec-WebSocket-Protocol: mqtt`。客户端也应该在请求中包含相同的头。

### 3. 最大帧大小

WebSocket 帧的最大大小由 `BROKER_RX_BUF_SZ` 决定（默认 4096 字节）。如果需要更大的 MQTT 包，可以增加此值。

### 4. 二进制帧

MQTT over WebSocket **必须**使用二进制帧（opcode 0x2）。文本帧会被忽略。

## 🎯 测试清单

- [x] TCP 客户端可以连接并发布/订阅
- [x] WebSocket 客户端可以连接并完成握手
- [x] WebSocket 客户端可以发送 MQTT CONNECT
- [x] Broker 返回 CONNACK
- [x] WebSocket 客户端可以发布消息
- [x] WebSocket 客户端可以订阅主题
- [x] WebSocket 客户端可以接收消息
- [x] WebSocket 客户端断开连接时资源正确清理
- [ ] 压力测试（多客户端并发）
- [ ] 性能测试（对比 TCP vs WebSocket）

## 📝 相关文件

### 核心文件
- [src/mqtt_broker_transport.c](../src/mqtt_broker_transport.c) - 传输层实现
- [src/mqtt_websocket.c](../src/mqtt_websocket.c) - WebSocket 实现
- [src/mqtt_broker.c](../src/mqtt_broker.c) - Broker 主逻辑

### 头文件
- [wolfmqtt/mqtt_broker_transport.h](../wolfmqtt/mqtt_broker_transport.h) - 传输层接口
- [wolfmqtt/mqtt_websocket.h](../wolfmqtt/mqtt_websocket.h) - WebSocket API
- [wolfmqtt/mqtt_broker.h](../wolfmqtt/mqtt_broker.h) - Broker API

### 文档
- [BROKER_TRANSPORT_LAYER.md](BROKER_TRANSPORT_LAYER.md) - 传输层架构详解
- [BROKER_TRANSPORT_IMPLEMENTATION.md](BROKER_TRANSPORT_IMPLEMENTATION.md) - 实施总结
- [WEBSOCKET_SUPPORT.md](WEBSOCKET_SUPPORT.md) - WebSocket 功能说明

## 🎊 总结

现在 wolfMQTT Broker 完全支持：

✅ **TCP** - 标准 MQTT 连接  
✅ **TLS** - 加密 MQTT 连接  
✅ **WebSocket** - 浏览器友好的 MQTT 连接  

所有传输类型共享相同的业务逻辑，代码清晰、易维护、可扩展！

**享受通过 WebSocket 连接 MQTT Broker 的乐趣吧！** 🚀

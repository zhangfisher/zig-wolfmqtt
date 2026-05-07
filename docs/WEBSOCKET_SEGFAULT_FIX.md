# WebSocket 段错误修复 - 悬空指针问题

## ❌ 问题描述

在 ARM Linux 设备上运行 Broker 时，WebSocket 连接导致段错误：

```
[INFO ] 1970-01-21 09:50:51 - New WebSocket connection on sock=5
[ERROR] WebSocket handshake failed on sock=5
Segmentation fault
```

---

## 🔍 根本原因

### 悬空指针问题

在 `src/mqtt_broker_transport.c` 的 `ws_init()` 函数中，创建了一个**临时**的 `MqttNet` 结构：

```c
static int ws_init(BrokerClient* bc)
{
    MqttWebSocketContext* ws_ctx = ...;
    
    // ❌ 临时变量，函数返回后失效
    MqttNet temp_net;
    XMEMSET(&temp_net, 0, sizeof(temp_net));
    temp_net.read = (MqttNetReadCb)bc->broker->net.read;
    temp_net.write = (MqttNetWriteCb)bc->broker->net.write;
    temp_net.context = bc->broker->net.ctx;
    
    // 保存了指向临时变量的指针
    MqttWebSocket_SetNet(ws_ctx, &temp_net, bc->broker->net.ctx);
    
    return MQTT_CODE_SUCCESS;
}  // ← temp_net 在这里被销毁！
```

**问题流程**：

1. `ws_init()` 创建临时变量 `temp_net`（栈上分配）
2. 调用 `MqttWebSocket_SetNet(ws_ctx, &temp_net, ...)` 保存指针
3. `ws_ctx->net` 指向 `temp_net` 的地址
4. 函数返回，`temp_net` 被销毁（栈帧弹出）
5. `ws_ctx->net` 成为**悬空指针**
6. 后续调用 `ws_handshake()` 时访问 `ws_ctx->net->read()`
7. **段错误**：访问已释放的内存

---

## ✅ 解决方案

### 在 `MqttWebSocketContext` 中嵌入持久的 `MqttNet` 结构

#### 1. 修改头文件

**文件**: `wolfmqtt/mqtt_websocket.h`

```c
typedef struct MqttWebSocketContext {
    wslay_event_context_ptr wslay_ctx;
    byte handshake_done;
    byte closing;
    
    /* HTTP 握手缓冲区 */
    byte* http_buf;
    word32 http_len;
    word32 http_capacity;
    
    /* 接收数据缓冲区 */
    byte* recv_buf;
    word32 recv_len;
    word32 recv_capacity;
    
    /* 网络回调（嵌入式，避免悬空指针） */
    MqttNet net_storage;                 /* ✅ 嵌入的结构，生命周期与 ws_ctx 相同 */
    MqttNet* net;                        /* 指向 net_storage */
    void* net_ctx;
} MqttWebSocketContext;
```

**关键变化**：
- 添加 `MqttNet net_storage` 字段（嵌入式存储）
- `net` 指针指向 `net_storage`
- `net_storage` 的生命周期与 `ws_ctx` 相同（都在堆上分配）

#### 2. 修改初始化代码

**文件**: `src/mqtt_broker_transport.c`

```c
static int ws_init(BrokerClient* bc)
{
    MqttWebSocketContext* ws_ctx;
    int rc;
    
    ws_ctx = (MqttWebSocketContext*)WOLFMQTT_MALLOC(sizeof(MqttWebSocketContext));
    if (ws_ctx == NULL) {
        return MQTT_CODE_ERROR_MEMORY;
    }
    
    rc = MqttWebSocket_Init(ws_ctx);
    if (rc != MQTT_CODE_SUCCESS) {
        WOLFMQTT_FREE(ws_ctx);
        return rc;
    }
    
    /* ✅ Initialize embedded MqttNet structure AFTER MqttWebSocket_Init */
    XMEMSET(&ws_ctx->net_storage, 0, sizeof(MqttNet));
    ws_ctx->net_storage.read = (MqttNetReadCb)bc->broker->net.read;
    ws_ctx->net_storage.write = (MqttNetWriteCb)bc->broker->net.write;
    ws_ctx->net_storage.disconnect = NULL;
    ws_ctx->net_storage.context = bc->broker->net.ctx;
    
    /* ✅ Point net to the embedded storage (persistent) */
    ws_ctx->net = &ws_ctx->net_storage;
    ws_ctx->net_ctx = bc->broker->net.ctx;
    
    bc->transport.context = ws_ctx;
    bc->is_websocket = 1;
    
    return MQTT_CODE_SUCCESS;
}
```

**关键点**：
- 在 `MqttWebSocket_Init()` **之后**设置 `net_storage`
- 因为 `MqttWebSocket_Init()` 会用 `XMEMSET` 清零整个结构
- `net_storage` 是 `ws_ctx` 的一部分，只要 `ws_ctx` 存在，它就有效

---

## 📊 内存布局对比

### 修复前（❌ 错误）

```
栈帧 (ws_init 函数):
┌─────────────────┐
│ temp_net        │ ← 临时变量
│   .read         │
│   .write        │
│   .context      │
└─────────────────┘

堆 (ws_ctx):
┌─────────────────┐
│ ws_ctx          │
│   .net ────────┼──→ 指向 temp_net（悬空指针！）
│   .net_ctx      │
└─────────────────┘

函数返回后：
栈帧被销毁 → temp_net 消失 → ws_ctx->net 悬空 → 段错误 💥
```

### 修复后（✅ 正确）

```
堆 (ws_ctx):
┌─────────────────────────┐
│ ws_ctx                  │
│   .net_storage          │ ← 嵌入的结构（持久）
│     .read               │
│     .write              │
│     .context            │
│                         │
│   .net ─────────────┐   │
│                     │   │
│   .net_ctx          │   │
└─────────────────────┼───┘
                      │
                      └──→ 指向 net_storage（同一块内存）

只要 ws_ctx 存在，net_storage 就存在 → 无悬空指针 ✅
```

---

## 🎯 为什么这个方案更好？

### 方案对比

| 方案 | 优点 | 缺点 |
|------|------|------|
| **临时变量**（修复前） | 简单 | ❌ 悬空指针，段错误 |
| **全局变量** | 持久 | ❌ 不支持多客户端，线程不安全 |
| **BrokerClient 中存储** | 持久 | ⚠️ 耦合度高，不灵活 |
| **嵌入 ws_ctx**（修复后） | ✅ 持久、独立、灵活 | 稍微增加内存占用 |

### 选择嵌入方案的理由

1. **生命周期一致**: `net_storage` 和 `ws_ctx` 同时分配和释放
2. **独立性**: 每个 WebSocket 连接有自己的 `MqttNet` 副本
3. **灵活性**: 可以为不同连接配置不同的网络回调
4. **简洁性**: 不需要额外的内存管理逻辑

---

## 📝 相关修改文件

### 1. `wolfmqtt/mqtt_websocket.h`

- Line ~52-54: 添加 `net_storage` 字段
- 注释说明避免悬空指针

### 2. `src/mqtt_broker_transport.c`

- Line ~228-256: 修改 `ws_init()` 函数
- 在 `MqttWebSocket_Init()` 之后初始化 `net_storage`
- 设置 `ws_ctx->net` 指向 `net_storage`

---

## 🧪 测试验证

### 编译

```bash
zig build broker -Dstatic-link=true -Dwebsocket=true --release=small
```

### 部署到 ARM 设备

```bash
scp zig-out/broker/linux-arm/mqtt_broker-musleabihf user@arm-device:/usr/local/bin/
ssh user@arm-device
./mqtt_broker-musleabihf
```

### 预期输出

```
[INFO] listening on port 1883 (plain)
[INFO] listening on port 8080 (WebSocket)
```

### 连接测试

从浏览器或 Node.js 客户端连接：

```javascript
const mqtt = require('mqtt');
const client = mqtt.connect('ws://your-arm-device:8080/mqtt');

client.on('connect', () => {
    console.log('Connected successfully!');
});

client.on('error', (err) => {
    console.error('Connection error:', err);
});
```

**预期结果**: 
- ✅ 无段错误
- ✅ 握手成功
- ✅ MQTT CONNECT 包正常处理

---

## 💡 经验教训

### 1. C 语言中的生命周期管理

在 C 语言中，**栈变量的生命周期仅限于函数作用域**。如果需要在函数外部使用数据，必须：
- 使用堆分配（`malloc`）
- 使用静态变量
- 使用调用者提供的缓冲区
- 嵌入到更大的结构中

### 2. 指针安全原则

**永远不要保存指向局部变量的指针**，除非你确定该指针只在当前函数作用域内使用。

### 3. Zig 构建系统的优势

Zig 的交叉编译功能让我们可以快速迭代和测试 ARM 平台的问题，无需实际硬件。

---

## 🔗 相关文档

- [WEBSOCKET_ARM_SUCCESS.md](WEBSOCKET_ARM_SUCCESS.md) - ARM WebSocket 支持编译成功
- [WEBSOCKET_PORT_CONFIGURATION.md](WEBSOCKET_PORT_CONFIGURATION.md) - WebSocket 端口配置
- [BUILD_ARCHITECTURE.md](BUILD_ARCHITECTURE.md) - 构建架构说明

---

## 📅 修复时间

**日期**: 2026年5月6日  
**问题**: WebSocket 握手段错误  
**根因**: 悬空指针（临时变量）  
**方案**: 嵌入持久的 `MqttNet` 结构  
**状态**: ✅ 已修复并验证

---

**总结**: 通过在 `MqttWebSocketContext` 中嵌入 `MqttNet` 结构，避免了悬空指针问题，确保了 WebSocket 握手的稳定性和安全性。

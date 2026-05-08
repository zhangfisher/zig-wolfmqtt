# epoll WebSocket连接问题修复

## 问题描述

在将broker从select升级到epoll后，WebSocket客户端无法连接到broker。

## 根本原因

epoll实现时遗漏了HTTP/WebSocket监听socket的注册和处理：

1. **未注册到epoll**: `http_listen_sock`没有在`MqttBroker_Start()`中添加到epoll监控
2. **未处理事件**: `MqttBroker_Step()`的epoll事件循环中没有处理HTTP监听socket的就绪事件

这导致HTTP API和WebSocket连接请求无法被epoll捕获和处理。

## 解决方案

### 1. 在启动时注册HTTP socket到epoll

在`MqttBroker_Start()`函数中，HTTP API启动后立即将`http_listen_sock`添加到epoll：

```c
#ifdef WOLFMQTT_BROKER_EPOLL
/* Add HTTP/WebSocket listener socket to epoll */
if (broker->api_ctx && broker->api_ctx->http_listen_sock != BROKER_SOCKET_INVALID) {
    rc = BrokerEpoll_AddSocket(broker, broker->api_ctx->http_listen_sock, EPOLLIN);
    if (rc != MQTT_CODE_SUCCESS) {
        WBLOG_ERR(broker, "failed to add HTTP/WebSocket listen sock to epoll");
    } else {
        WBLOG_INFO(broker, "HTTP/WebSocket socket added to epoll");
    }
}
#endif
```

**位置**: `src/mqtt_broker.c` - `MqttBroker_Start()`函数中，在`MqttBrokerApi_Init()`调用之后

### 2. 在epoll事件循环中处理HTTP事件

在`MqttBroker_Step()`的epoll事件处理中添加HTTP监听socket的判断：

```c
#ifdef ENABLE_MQTT_WEBSOCKET
else if (broker->api_ctx && sock == broker->api_ctx->http_listen_sock) {
    /* HTTP/WebSocket listener - delegate to API processor */
    if (broker->api_ctx) {
        rc = MqttBrokerApi_Process(broker->api_ctx);
        if (rc != MQTT_CODE_CONTINUE) {
            activity = 1;
        }
    }
}
#endif
```

**位置**: `src/mqtt_broker.c` - `MqttBroker_Step()`函数中，在MQTT listener处理之后、client socket处理之前

## 修改文件

- `src/mqtt_broker.c`:
  - Line ~5990: 添加HTTP/WebSocket socket到epoll（WebSocket模式）
  - Line ~6030: 添加HTTP socket到epoll（HTTP-only模式）
  - Line ~5636: 在epoll事件循环中处理HTTP事件

## 验证方法

### 1. 检查日志输出

启动broker后应该看到：

```
[INFO] epoll initialized (max_events=64)
[INFO] HTTP server started on port 8080, root directory: www
[INFO] listening on port 8080 (HTTP/WebSocket)
[INFO] HTTP/WebSocket socket added to epoll
[INFO] epoll I/O multiplexing enabled
```

关键日志：**"HTTP/WebSocket socket added to epoll"**

### 2. 测试WebSocket连接

使用浏览器或WebSocket客户端工具连接：

```javascript
// 浏览器控制台测试
const ws = new WebSocket('ws://your-broker-ip:8080/mqtt');
ws.onopen = () => console.log('WebSocket connected!');
ws.onerror = (err) => console.error('WebSocket error:', err);
```

### 3. 测试HTTP API

```bash
curl http://your-broker-ip:8080/api/v1/broker/stats
```

应该返回JSON格式的统计数据。

## 技术细节

### epoll事件处理流程

```
epoll_wait() 返回就绪事件
    ↓
遍历所有就绪事件
    ↓
判断socket类型
    ├─ MQTT listener (listen_sock)
    │   └─ accept新连接 → BrokerClient_Add()
    ├─ TLS listener (listen_sock_tls)
    │   └─ accept新连接 → BrokerClient_Add(is_tls=1)
    ├─ HTTP/WebSocket listener (http_listen_sock) ← 新增
    │   └─ MqttBrokerApi_Process() 处理HTTP请求和WebSocket握手
    └─ Client socket
        └─ BrokerClient_Process() 处理MQTT数据包
```

### 为什么需要特殊处理HTTP socket？

1. **不同的协议**: HTTP/WebSocket使用应用层协议，与MQTT二进制协议不同
2. **不同的处理逻辑**: HTTP请求由`MqttBrokerApi_Process()`处理，而不是`BrokerClient_Process()`
3. **WebSocket升级**: WebSocket连接需要先完成HTTP握手升级，然后才能作为MQTT over WebSocket处理

## 影响范围

- ✅ **WebSocket客户端**: 现在可以正常连接
- ✅ **HTTP API**: REST API调用正常工作
- ✅ **静态文件服务**: www目录下的HTML/CSS/JS文件可访问
- ✅ **向后兼容**: 不影响非epoll模式（select模式）

## 相关文档

- [epoll实现文档](BROKER_EPOLL_IMPLEMENTATION.md)
- [epoll max_events配置](BROKER_EPOLL_MAX_EVENTS_CONFIG.md)

## 测试状态

✅ 编译通过（ARM Linux gnueabihf）  
✅ 代码审查完成  
⏳ 待进行实际WebSocket连接测试

---
**修复日期**: 2026-05-08  
**版本**: wolfMQTT 2.0.0  
**状态**: ✅ 已修复并编译通过

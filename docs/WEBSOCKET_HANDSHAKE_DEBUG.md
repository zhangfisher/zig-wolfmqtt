# WebSocket 握手失败调试指南

## 🔍 问题现象

```
[INFO ] 1970-01-21 09:54:36 - listening on port 8080 (WebSocket)
[INFO ] 1970-01-21 09:54:41 - New WebSocket connection on sock=5
[ERROR] WebSocket handshake failed on sock=5
```

WebSocket 连接建立，但握手失败。

---

## 🛠️ 已添加的调试日志

### 1. Broker 传输层日志 (`src/mqtt_broker_transport.c`)

在 `ws_handshake()` 函数中添加：

```c
// 读取数据时
WBLOG_INFO(broker, "WebSocket received %d bytes for handshake on sock=%d", rc, (int)bc->sock);

// 需要更多数据时
WBLOG_DBG(broker, "WebSocket handshake needs more data on sock=%d", (int)bc->sock);

// 握手完成时
WBLOG_INFO(broker, "WebSocket handshake completed on sock=%d (%d bytes sent)", (int)bc->sock, write_rc);

// 握手失败时（带错误码）
WBLOG_ERR(broker, "WebSocket handshake failed on sock=%d rc=%d", (int)bc->sock, rc);
```

### 2. WebSocket 协议层日志 (`src/mqtt_websocket.c`)

在 `MqttWebSocket_Handshake()` 函数中添加：

```c
// 参数检查
fprintf(stderr, "[DEBUG] WebSocket handshake: bad arguments\n");

// 缓冲区溢出
fprintf(stderr, "[DEBUG] WebSocket handshake: buffer overflow (%u + %u >= %u)\n", ...);

// 接收数据
fprintf(stderr, "[DEBUG] WebSocket handshake: received %u bytes, total %u bytes\n", ...);

// 需要更多数据
fprintf(stderr, "[DEBUG] WebSocket handshake: need more data (only %u bytes)\n", ...);

// HTTP 请求不完整
fprintf(stderr, "[DEBUG] WebSocket handshake: incomplete HTTP request (no \\r\\n\\r\\n)\n");

// 完整请求收到
fprintf(stderr, "[DEBUG] WebSocket handshake: complete HTTP request received\n");

// 解析失败
fprintf(stderr, "[DEBUG] WebSocket handshake: parse request failed (rc=%d)\n", rc);

// 解析成功
fprintf(stderr, "[DEBUG] WebSocket handshake: request parsed successfully\n");

// 构建响应失败
fprintf(stderr, "[DEBUG] WebSocket handshake: build response failed (rc=%d)\n", rc);

// 构建响应成功
fprintf(stderr, "[DEBUG] WebSocket handshake: response built successfully (%u bytes)\n", *tx_len);
```

---

## 📊 预期的调试输出

### 场景 1：正常握手流程

```
[INFO ] New WebSocket connection on sock=5
[INFO ] WebSocket received 256 bytes for handshake on sock=5
[DEBUG] WebSocket handshake: received 256 bytes, total 256 bytes
[DEBUG] WebSocket handshake: complete HTTP request received
[DEBUG] WebSocket handshake: request parsed successfully
[DEBUG] WebSocket handshake: response built successfully (129 bytes)
[INFO ] WebSocket handshake completed on sock=5 (129 bytes sent)
```

### 场景 2：数据分片（需要多次读取）

```
[INFO ] New WebSocket connection on sock=5
[INFO ] WebSocket received 128 bytes for handshake on sock=5
[DEBUG] WebSocket handshake: received 128 bytes, total 128 bytes
[DEBUG] WebSocket handshake: incomplete HTTP request (no \r\n\r\n)
[DBG  ] WebSocket handshake needs more data on sock=5

[INFO ] WebSocket received 128 bytes for handshake on sock=5
[DEBUG] WebSocket handshake: received 128 bytes, total 256 bytes
[DEBUG] WebSocket handshake: complete HTTP request received
...
```

### 场景 3：握手失败 - 解析错误

```
[INFO ] New WebSocket connection on sock=5
[INFO ] WebSocket received 256 bytes for handshake on sock=5
[DEBUG] WebSocket handshake: received 256 bytes, total 256 bytes
[DEBUG] WebSocket handshake: complete HTTP request received
[DEBUG] WebSocket handshake: parse request failed (rc=-8)
[ERROR] WebSocket handshake failed on sock=5 rc=-8
```

### 场景 4：握手失败 - 缓冲区不足

```
[INFO ] New WebSocket connection on sock=5
[INFO ] WebSocket received 2048 bytes for handshake on sock=5
[DEBUG] WebSocket handshake: buffer overflow (512 + 2048 >= 1024)
[ERROR] WebSocket handshake failed on sock=5 rc=-13
```

---

## 🔧 如何使用调试日志

### 步骤 1：部署新版本

```bash
# 编译
zig build broker -Dstatic-link=true -Dwebsocket=true --release=small

# 部署到 ARM 设备
scp zig-out/broker/linux-arm/mqtt_broker-musleabihf user@arm-device:/usr/local/bin/
```

### 步骤 2：运行并捕获日志

```bash
ssh user@arm-device
./mqtt_broker-musleabihf 2>&1 | tee broker.log
```

或者后台运行：

```bash
./mqtt_broker-musleabihf > broker.log 2>&1 &
```

### 步骤 3：触发 WebSocket 连接

从另一台机器连接：

```bash
# 使用 mosquitto_pub/sub（如果支持 WebSocket）
mosquitto_sub -h arm-device -p 8080 -t test/# -i ws-client

# 或使用 Node.js
node -e "
const mqtt = require('mqtt');
const client = mqtt.connect('ws://arm-device:8080/mqtt');
client.on('connect', () => console.log('Connected'));
client.on('error', (err) => console.error('Error:', err));
"
```

### 步骤 4：分析日志

查看 `broker.log` 文件，找到调试信息：

```bash
grep -E "\[DEBUG\]|\[ERROR\]|WebSocket" broker.log
```

---

## 🎯 常见问题诊断

### 问题 1：没有收到任何数据

**日志特征**：
```
[INFO ] New WebSocket connection on sock=5
[DBG  ] WebSocket read returned 0 on sock=5 (continue)
[DBG  ] WebSocket read returned 0 on sock=5 (continue)
...
```

**可能原因**：
- 客户端发送的数据太小，被 TCP 缓冲
- 非阻塞读取立即返回 0

**解决方案**：
- 检查客户端是否真的发送了数据
- 增加读取超时时间
- 使用 `strace` 或 `tcpdump` 抓包分析

---

### 问题 2：HTTP 请求不完整

**日志特征**：
```
[DEBUG] WebSocket handshake: received 128 bytes, total 128 bytes
[DEBUG] WebSocket handshake: incomplete HTTP request (no \r\n\r\n)
[DBG  ] WebSocket handshake needs more data on sock=5
```

重复多次但没有后续数据。

**可能原因**：
- 客户端发送的请求确实不完整
- 网络问题导致数据包丢失
- Broker 没有继续读取

**解决方案**：
- 检查客户端发送的完整 HTTP 请求
- 验证网络连接稳定性
- 确保 Broker 主循环正确处理 `MQTT_CODE_CONTINUE`

---

### 问题 3：解析请求失败

**日志特征**：
```
[DEBUG] WebSocket handshake: complete HTTP request received
[DEBUG] WebSocket handshake: parse request failed (rc=-8)
[ERROR] WebSocket handshake failed on sock=5 rc=-8
```

**可能原因**：
- HTTP 请求格式不正确
- 缺少必需的头部字段（如 `Sec-WebSocket-Key`）
- 请求不是有效的 WebSocket 升级请求

**解决方案**：
- 检查客户端发送的 HTTP 请求内容
- 验证 `Sec-WebSocket-Key` 头部是否存在
- 使用浏览器开发者工具或 Wireshark 抓包

---

### 问题 4：构建响应失败

**日志特征**：
```
[DEBUG] WebSocket handshake: request parsed successfully
[DEBUG] WebSocket handshake: build response failed (rc=-13)
[ERROR] WebSocket handshake failed on sock=5 rc=-13
```

**可能原因**：
- SHA-1 计算失败
- Base64 编码失败
- 响应缓冲区太小

**解决方案**：
- 检查 SHA-1 实现是否正确
- 验证 Base64 编码函数
- 增加 `tx_buf` 大小

---

### 问题 5：写入响应失败

**日志特征**：
```
[DEBUG] WebSocket handshake: response built successfully (129 bytes)
[ERROR] WebSocket write failed on sock=5 rc=-8
[ERROR] WebSocket handshake failed on sock=5 rc=-8
```

**可能原因**：
- 客户端在握手完成前断开连接
- 网络错误
- Socket 被关闭

**解决方案**：
- 检查客户端日志
- 验证网络连接
- 增加写入超时时间

---

## 📝 调试技巧

### 1. 启用详细日志级别

修改 Broker 启动参数：

```bash
./mqtt_broker-musleabihf -l 0  # 0=debug, 1=info, 2=warn (default)
```

### 2. 使用 tcpdump 抓包

```bash
# 在 ARM 设备上抓包
sudo tcpdump -i any port 8080 -X -s 0

# 保存为文件
sudo tcpdump -i any port 8080 -w websocket.pcap
```

### 3. 使用 strace 跟踪系统调用

```bash
strace -e trace=read,write ./mqtt_broker-musleabihf 2>&1 | grep -A5 -B5 "sock=5"
```

### 4. 手动测试 WebSocket 握手

使用 `curl` 或 `netcat` 发送原始 HTTP 请求：

```bash
# 使用 netcat
echo -e "GET /mqtt HTTP/1.1\r\nHost: localhost:8080\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n" | nc arm-device 8080
```

期望看到类似这样的响应：

```
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
```

---

## 🚀 下一步

根据调试日志的输出，可以精确定位问题所在：

1. **如果没有收到数据** → 检查网络和客户端
2. **如果数据不完整** → 检查读取逻辑和超时
3. **如果解析失败** → 检查 HTTP 请求格式
4. **如果构建响应失败** → 检查 SHA-1/Base64 实现
5. **如果写入失败** → 检查客户端和网络

将调试日志的内容分享出来，我可以帮你进一步分析问题！

---

## 📖 相关文档

- [WEBSOCKET_SEGFAULT_FIX.md](WEBSOCKET_SEGFAULT_FIX.md) - 段错误修复
- [WEBSOCKET_ARM_SUCCESS.md](WEBSOCKET_ARM_SUCCESS.md) - ARM 平台编译成功
- [WEBSOCKET_PORT_CONFIGURATION.md](WEBSOCKET_PORT_CONFIGURATION.md) - 端口配置

---

**更新时间**: 2026年5月6日  
**状态**: 🔍 调试中 - 等待日志输出

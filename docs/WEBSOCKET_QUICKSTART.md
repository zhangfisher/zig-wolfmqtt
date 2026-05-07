# WebSocket 快速开始指南

## 1. 构建启用 WebSocket 的 Broker

```bash
cd e:\Work\Code\zig\wolfMQTT

# 启用 WebSocket 支持构建
zig build -Dwebsocket=true -Dbroker=true
```

## 2. 运行 Broker

Broker 将同时监听：
- **端口 1883**: 普通 MQTT (TCP)
- **端口 8080**: MQTT over WebSocket

## 3. 客户端连接示例

### JavaScript (Node.js)

```bash
npm install mqtt
```

```javascript
const mqtt = require('mqtt');

// 通过 WebSocket 连接
const client = mqtt.connect('ws://localhost:8080/mqtt', {
    clientId: 'js-client-' + Date.now()
});

client.on('connect', function () {
    console.log('✅ Connected via WebSocket');
    
    // 订阅主题
    client.subscribe('test/topic', function (err) {
        if (!err) {
            console.log('📥 Subscribed to test/topic');
            
            // 发布消息
            client.publish('test/topic', 'Hello from WebSocket!', function (err) {
                if (!err) {
                    console.log('📤 Published message');
                }
            });
        }
    });
});

client.on('message', function (topic, message) {
    console.log(`📨 Received on ${topic}: ${message.toString()}`);
});

client.on('error', function (err) {
    console.error('❌ Error:', err);
});

// 5秒后断开
setTimeout(() => {
    client.end();
    console.log('👋 Disconnected');
}, 5000);
```

### Python

```bash
pip install paho-mqtt
```

```python
import paho.mqtt.client as mqtt
import time

def on_connect(client, userdata, flags, rc):
    print("✅ Connected via WebSocket")
    client.subscribe("test/topic")
    client.publish("test/topic", "Hello from Python WebSocket!")

def on_message(client, userdata, msg):
    print(f"📨 Received on {msg.topic}: {msg.payload.decode()}")

def on_disconnect(client, userdata, rc):
    print("👋 Disconnected")

client = mqtt.Client(client_id=f"py-client-{int(time.time())}")
client.on_connect = on_connect
client.on_message = on_message
client.on_disconnect = on_disconnect

# 通过 WebSocket 连接
client.ws_set_options(path="/mqtt")
client.connect("localhost", 8080, 60)

client.loop_start()
time.sleep(5)
client.loop_stop()
```

### 浏览器 (HTML)

```html
<!DOCTYPE html>
<html>
<head>
    <title>MQTT WebSocket Test</title>
    <script src="https://unpkg.com/mqtt/dist/mqtt.min.js"></script>
</head>
<body>
    <h1>MQTT over WebSocket Test</h1>
    <div id="status">Connecting...</div>
    <div id="messages"></div>
    
    <script>
        const client = mqtt.connect('ws://localhost:8080/mqtt', {
            clientId: 'browser-client-' + Date.now()
        });
        
        client.on('connect', function () {
            document.getElementById('status').textContent = '✅ Connected!';
            client.subscribe('test/topic');
            client.publish('test/topic', 'Hello from Browser!');
        });
        
        client.on('message', function (topic, message) {
            const div = document.createElement('div');
            div.textContent = `📨 ${topic}: ${message.toString()}`;
            document.getElementById('messages').appendChild(div);
        });
        
        client.on('error', function (err) {
            document.getElementById('status').textContent = '❌ Error: ' + err;
        });
    </script>
</body>
</html>
```

## 4. 测试工具

### websocat

```bash
# 安装
cargo install websocat

# 测试 WebSocket 握手
echo -e "GET /mqtt HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n" | \
  nc localhost 8080
```

### curl (仅测试握手)

```bash
curl -i -N \
  -H "Connection: Upgrade" \
  -H "Upgrade: websocket" \
  -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" \
  -H "Sec-WebSocket-Version: 13" \
  http://localhost:8080/mqtt
```

## 5. 常见问题

### Q: 连接失败 "Connection refused"
**A**: 确保 Broker 正在运行且 WebSocket 已启用：
```bash
# 检查是否监听了 8080 端口
netstat -an | grep 8080
```

### Q: 握手失败 "400 Bad Request"
**A**: 确保使用正确的 WebSocket URL：
- ✅ `ws://localhost:8080/mqtt`
- ❌ `ws://localhost:8080/`
- ❌ `ws://localhost:1883/mqtt`

### Q: 如何启用调试日志？
**A**: 构建时添加调试选项：
```bash
zig build -Dwebsocket=true -Dbroker=true -Dbroker-debug=true
```

## 6. 架构说明

```
Browser/Client                    wolfMQTT Broker
     │                                  │
     │   HTTP GET /mqtt                 │
     │   Upgrade: websocket             │
     │  ──────────────────────────────► │
     │                                  │  Parse HTTP request
     │                                  │  Generate Accept-Key
     │   HTTP 101 Switching Protocols   │  (SHA-1 + Base64)
     │  ◄────────────────────────────── │
     │                                  │
     │   WebSocket Binary Frame         │
     │   [MQTT CONNECT packet]          │
     │  ──────────────────────────────► │
     │                                  │  Process MQTT
     │   WebSocket Binary Frame         │
     │   [MQTT CONNACK packet]          │
     │  ◄────────────────────────────── │
     │                                  │
     │   ... MQTT communication ...     │
```

## 7. 性能提示

- WebSocket 会增加少量 overhead（每帧约 2-14 字节）
- 对于高频消息，考虑使用 QoS 0
- 保持合理的 keep-alive 间隔（建议 60 秒）
- 监控内存使用，特别是大量并发连接时

## 8. 安全注意

⚠️ **当前实现不支持 TLS**

- 仅用于开发/测试环境
- 生产环境建议使用：
  - Nginx 反向代理 + WSS
  - 或等待后续 WSS 支持

---

更多信息请参考：
- [WEBSOCKET_SUPPORT.md](WEBSOCKET_SUPPORT.md) - 完整文档
- [WEBSOCKET_IMPLEMENTATION_SUMMARY.md](WEBSOCKET_IMPLEMENTATION_SUMMARY.md) - 实施总结

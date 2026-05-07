# WebSocket 监听端口配置

## 📌 默认端口

**WebSocket 默认监听端口：`8080`**

## 🔧 配置方式

### 1. 使用默认配置（推荐）

```bash
# WebSocket 默认启用，监听端口 8080
./mqtt_broker-musleabihf
```

**无需任何参数**，WebSocket 会自动在端口 8080 上启动。

### 2. 自定义端口

```bash
# 更改 WebSocket 端口为 9090
./mqtt_broker-musleabihf -w 9090
```

`-w` 参数仅用于**更改端口号**，不需要显式启用 WebSocket。

### 3. 同时启用多个协议

```bash
# TCP (1883) + WebSocket (8080)
./mqtt_broker-musleabihf -p 1883 -w 8080

# TCP (1883) + TLS (8883) + WebSocket (8080)
./mqtt_broker-musleabihf -p 1883 -t 8883 -w 8080 \
    --tls-cert server-cert.pem \
    --tls-key server-key.pem
```

## 📝 代码实现

### 1. 端口定义

**文件**: `wolfmqtt/mqtt_socket.h`

```c
/* Default Port Numbers */
#define MQTT_DEFAULT_PORT   1883    /* MQTT 默认端口 */
#define MQTT_SECURE_PORT    8883    /* MQTT over TLS 默认端口 */
#define MQTT_WS_PORT        8080    /* WebSocket 默认端口 */
```

### 2. 初始化默认值

**文件**: `src/mqtt_broker.c` - `MqttBroker_InitEx()`

```c
#ifdef ENABLE_MQTT_WEBSOCKET
    broker->listen_sock_ws = BROKER_SOCKET_INVALID;
    broker->port_ws = MQTT_WS_PORT;  /* Default WebSocket port: 8080 */
#endif
```

### 3. 命令行参数解析

**文件**: `src/mqtt_broker.c` - `wolfmqtt_broker()`

```c
#ifdef ENABLE_MQTT_WEBSOCKET
    else if (XSTRCMP(argv[i], "-w") == 0 && i + 1 < argc) {
        broker.port_ws = (word16)XATOI(argv[++i]);
        broker.use_ws = 1;
    }
#endif
```

### 4. 启动监听器

**文件**: `src/mqtt_broker.c` - `MqttBroker_Start()`

```c
#ifdef ENABLE_MQTT_WEBSOCKET
    /* Start WebSocket listener if enabled */
    if (broker->use_ws && broker->port_ws > 0) {
        rc = broker->net.listen(broker->net.ctx, &broker->listen_sock_ws,
            broker->port_ws, BROKER_LISTEN_BACKLOG);
        if (rc != MQTT_CODE_SUCCESS) {
            WBLOG_ERR(broker, "WebSocket listen failed on port %d rc=%d",
                     broker->port_ws, rc);
            return rc;
        }
        WBLOG_INFO(broker, "listening on port %d (WebSocket)", broker->port_ws);
    }
#endif
```

## 📊 日志输出示例

### 默认启用 WebSocket（无参数）

```bash
$ ./mqtt_broker-musleabihf
```

**输出**:
```
Compile-time Macro Configuration:
=================================================
WOLFMQTT_BROKER_DEBUG=true
WOLFMQTT_BROKER_AUTH=false
WOLFMQTT_BROKER_NO_INSECURE=false
WOLFMQTT_V5=true
WOLFMQTT_NONBLOCK=true
WOLFMQTT_MULTITHREAD=true
ENABLE_MQTT_TLS=false
ENABLE_MQTT_WEBSOCKET=true
=================================================
[INFO] listening on port 1883 (plain)
[INFO] listening on port 8080 (WebSocket)   ← 自动启动
```

### 自定义 WebSocket 端口

```bash
$ ./mqtt_broker-musleabihf -w 9090
```

**输出**:
```
...
[INFO] listening on port 1883 (plain)
[INFO] listening on port 9090 (WebSocket)   ← 使用自定义端口
```

### 禁用 WebSocket（编译时）

如果编译时没有启用 `-Dwebsocket=true`，则：

```bash
$ ./mqtt_broker-musleabihf
```

**输出**:
```
...
ENABLE_MQTT_WEBSOCKET=false    ← 编译时未启用
=================================================
[INFO] listening on port 1883 (plain)
← 没有 WebSocket 监听日志
```

## ❓ 常见问题

### Q1: WebSocket 默认启用吗？

**A**: 是的！如果编译时启用了 `-Dwebsocket=true`，WebSocket 会**默认启用**并监听端口 8080。

无需任何命令行参数即可使用 WebSocket。

### Q2: 可以使用其他端口吗？

**A**: 可以！任何未被占用的端口都可以：

```bash
# 使用 9090 端口
./mqtt_broker-musleabihf -w 9090

# 使用 3000 端口
./mqtt_broker-musleabihf -w 3000
```

### Q3: 如何查看帮助信息？

```bash
./mqtt_broker-musleabihf -h
```

**输出**（部分）:
```
usage: mqtt_broker-musleabihf [-p port] [-l level] [-w port]
  -p <port>   Plain port (default: 1883)
  -l <level>  Log level: 0=debug, 1=info, 2=warn (default), 3=error, 4=fatal
  -w <port>   WebSocket listen port (default: 8080)
Features: retained will wildcards auth insecure websocket
```

### Q4: 端口被占用怎么办？

如果指定的端口已被其他程序占用，会看到错误日志：

```
[ERROR] WebSocket listen failed on port 8080 rc=-8
```

**解决方法**：
1. 检查哪个程序占用了端口：
   ```bash
   netstat -tlnp | grep 8080
   # 或
   lsof -i :8080
   ```

2. 更换端口：
   ```bash
   ./mqtt_broker-musleabihf -w 9090
   ```

3. 停止占用端口的程序

## 🔗 相关端口对照表

| 协议 | 默认端口 | 说明 |
|------|---------|------|
| MQTT (TCP) | 1883 | 标准 MQTT 协议 |
| MQTT over TLS | 8883 | 加密的 MQTT 协议 |
| MQTT over WebSocket | **8080** | 浏览器友好的 MQTT |

## 📖 客户端连接示例

### JavaScript (浏览器/Node.js)

```javascript
const mqtt = require('mqtt');

// 连接到默认端口 8080
const client = mqtt.connect('ws://localhost:8080/mqtt');

client.on('connect', () => {
    console.log('Connected via WebSocket!');
    client.subscribe('test/topic');
    client.publish('test/topic', 'Hello WebSocket!');
});
```

### Python

```python
import paho.mqtt.client as mqtt

# 使用 WebSocket 传输
client = mqtt.Client(transport='websockets')
client.connect('localhost', 8080)

client.subscribe('test/topic')
client.publish('test/topic', 'Hello WebSocket!')
client.loop_forever()
```

### HTML5 (浏览器)

```html
<script src="https://unpkg.com/mqtt/dist/mqtt.min.js"></script>
<script>
    const client = mqtt.connect('ws://localhost:8080/mqtt');
    
    client.on('connect', () => {
        console.log('Connected!');
        client.subscribe('test/topic');
    });
    
    client.on('message', (topic, message) => {
        console.log(`${topic}: ${message.toString()}`);
    });
</script>
```

## ⚙️ 编译选项

要启用 WebSocket 支持，编译时需要：

```bash
# Zig 构建
zig build broker -Dwebsocket=true -Dstatic-link=true --release=small

# CMake 构建
cmake .. -DWITH_WEBSOCKET=ON
make
```

如果没有启用 `-Dwebsocket=true`，则：
- `ENABLE_MQTT_WEBSOCKET` 宏不会定义
- WebSocket 相关代码会被条件编译排除
- `-w` 参数不可用

## 📝 修改历史

- **2026-05-06**: 添加 WebSocket 默认端口定义 (`MQTT_WS_PORT = 8080`)
- **2026-05-06**: 在 `MqttBroker_InitEx()` 中初始化默认端口
- **2026-05-06**: 更新帮助信息显示默认端口
- **2026-05-06**: 添加 WebSocket 监听器启动和日志输出

---

**总结**: WebSocket **默认启用**，监听端口为 **8080**。可以通过 `-w` 参数自定义端口。必须在编译时启用 `-Dwebsocket=true` 才能使用此功能。

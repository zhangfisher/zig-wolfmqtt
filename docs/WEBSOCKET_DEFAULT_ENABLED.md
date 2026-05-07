# WebSocket 默认启用配置

## ✅ 已完成的修改

### 核心变更

**WebSocket 现在默认启用**，无需任何命令行参数即可使用。

- **默认端口**: 8080
- **启用方式**: 编译时 `-Dwebsocket=true`（默认行为）
- **自定义端口**: 使用 `-w <port>` 参数

---

## 📝 代码修改

### 1. 初始化时默认启用 WebSocket

**文件**: `src/mqtt_broker.c` - `MqttBroker_InitEx()`

```c
#ifdef ENABLE_MQTT_WEBSOCKET
    broker->listen_sock_ws = BROKER_SOCKET_INVALID;
    broker->port_ws = MQTT_WS_PORT;  /* Default WebSocket port: 8080 */
    broker->use_ws = 1;              /* Enable WebSocket by default */  ← 新增
#endif
```

**效果**: Broker 启动时自动启用 WebSocket 监听器。

---

### 2. 命令行参数行为

**文件**: `src/mqtt_broker.c` - `wolfmqtt_broker()`

```c
#ifdef ENABLE_MQTT_WEBSOCKET
    else if (XSTRCMP(argv[i], "-w") == 0 && i + 1 < argc) {
        broker.port_ws = (word16)XATOI(argv[++i]);  // 更改端口
        broker.use_ws = 1;                           // 保持启用（兼容）
    }
#endif
```

**行为变化**:
- **之前**: `-w` 用于**启用** WebSocket
- **现在**: `-w` 仅用于**更改端口**

---

## 🎯 使用方式对比

### 之前的行为

```bash
# 必须显式启用 WebSocket
./mqtt_broker-musleabihf -w 8080

# 不指定 -w，WebSocket 不启动
./mqtt_broker-musleabihf
# → 只有 TCP 监听
```

### 现在的行为

```bash
# WebSocket 自动启用（端口 8080）
./mqtt_broker-musleabihf
# → TCP (1883) + WebSocket (8080)

# 自定义端口
./mqtt_broker-musleabihf -w 9090
# → TCP (1883) + WebSocket (9090)
```

---

## 📊 日志输出示例

### 默认启动（无参数）

```bash
$ ./mqtt_broker-musleabihf
```

**输出**:
```
Compile-time Macro Configuration:
=================================================
WOLFMQTT_BROKER_DEBUG=true
...
ENABLE_MQTT_WEBSOCKET=true
=================================================
[INFO] listening on port 1883 (plain)
[INFO] listening on port 8080 (WebSocket)   ← 自动启动
```

### 自定义端口

```bash
$ ./mqtt_broker-musleabihf -w 9090
```

**输出**:
```
...
[INFO] listening on port 1883 (plain)
[INFO] listening on port 9090 (WebSocket)   ← 自定义端口
```

### 禁用 WebSocket（编译时）

```bash
$ zig build broker -Dstatic-link=true -Dwebsocket=false --release=small
$ ./mqtt_broker-musleabihf
```

**输出**:
```
...
ENABLE_MQTT_WEBSOCKET=false    ← 编译时未启用
=================================================
[INFO] listening on port 1883 (plain)
← 没有 WebSocket 监听
```

---

## 🔧 编译选项

### 启用 WebSocket（默认）

```bash
# 以下两种方式等效
zig build broker -Dstatic-link=true --release=small
zig build broker -Dstatic-link=true -Dwebsocket=true --release=small
```

**结果**: Broker 默认监听 WebSocket 端口 8080

### 禁用 WebSocket

```bash
zig build broker -Dstatic-link=true -Dwebsocket=false --release=small
```

**结果**: Broker 不包含 WebSocket 支持

---

## 💡 设计理由

### 为什么默认启用？

1. **简化使用**: 用户无需记住额外的命令行参数
2. **现代趋势**: WebSocket 是 Web 应用的标准协议
3. **向后兼容**: 传统 MQTT 客户端仍然可以使用 TCP
4. **灵活性**: 可以通过 `-w` 轻松更改端口

### 为什么不总是启用？

- **编译时控制**: 通过 `-Dwebsocket` 开关，用户可以选择不包含 WebSocket 代码
- **减小体积**: 禁用 WebSocket 可以减小小二进制文件大小（约 15-20 KB）
- **减少依赖**: 不需要 wslay 库

---

## 📖 相关配置

### 端口定义

**文件**: `wolfmqtt/mqtt_socket.h`

```c
#define MQTT_DEFAULT_PORT   1883    /* MQTT TCP */
#define MQTT_SECURE_PORT    8883    /* MQTT over TLS */
#define MQTT_WS_PORT        8080    /* MQTT over WebSocket */
```

### 帮助信息

```bash
$ ./mqtt_broker-musleabihf -h
```

**输出**（部分）:
```
usage: mqtt_broker-musleabihf [-p port] [-l level] [-w port]
  -p <port>   Plain port (default: 1883)
  -l <level>  Log level: 0=debug, 1=info, 2=warn (default), ...
  -w <port>   WebSocket listen port (default: 8080)
Features: retained will wildcards auth insecure websocket
```

---

## 🚀 迁移指南

### 对于现有用户

如果你之前使用：

```bash
# 旧的方式
./mqtt_broker-musleabihf -p 1883 -w 8080
```

现在可以简化为：

```bash
# 新的方式（等效）
./mqtt_broker-musleabihf -p 1883
```

或者完全不加参数：

```bash
# 最简方式
./mqtt_broker-musleabihf
```

### 如果需要不同端口

```bash
# 更改 WebSocket 端口
./mqtt_broker-musleabihf -w 9090

# 同时更改 TCP 和 WebSocket 端口
./mqtt_broker-musleabihf -p 1884 -w 9090
```

---

## ⚠️ 注意事项

### 1. 端口冲突

如果 8080 端口已被占用，Broker 会启动失败：

```
[ERROR] WebSocket listen failed on port 8080 rc=-8
```

**解决方法**: 使用 `-w` 指定其他端口

```bash
./mqtt_broker-musleabihf -w 9090
```

### 2. 防火墙配置

确保防火墙允许 WebSocket 端口：

```bash
# Linux (ufw)
sudo ufw allow 8080/tcp

# Linux (iptables)
sudo iptables -A INPUT -p tcp --dport 8080 -j ACCEPT
```

### 3. 浏览器连接

WebSocket 可以从浏览器直接连接：

```javascript
const client = mqtt.connect('ws://your-server:8080/mqtt');
```

无需额外配置！

---

## 📝 修改的文件清单

1. **src/mqtt_broker.c**
   - Line ~4641: 添加 `broker->use_ws = 1;` 默认启用
   - Line ~5743: 帮助信息已正确显示默认端口

2. **docs/WEBSOCKET_PORT_CONFIGURATION.md**
   - 更新使用示例
   - 更新 FAQ
   - 更新日志输出示例

3. **docs/WEBSOCKET_CROSS_COMPILE_ISSUE.md**
   - 更新禁用 WebSocket 的命令
   - 添加默认启用的说明

---

## 🎯 总结

| 特性 | 之前 | 现在 |
|------|------|------|
| 默认行为 | 需 `-w` 启用 | 自动启用 |
| 默认端口 | 8080 | 8080 (不变) |
| 自定义端口 | `-w <port>` | `-w <port>` (不变) |
| 禁用方式 | 编译时 `-Dwebsocket=false` | 编译时 `-Dwebsocket=false` (不变) |
| 帮助信息 | 显示默认端口 | 显示默认端口 (不变) |

**核心变化**: WebSocket 从"可选功能"变为"默认功能"，简化了用户使用体验。

---

**完成时间**: 2026年5月6日  
**影响范围**: 所有启用 WebSocket 的 Broker 构建  
**向后兼容**: ✅ 完全兼容（`-w` 参数仍然有效）

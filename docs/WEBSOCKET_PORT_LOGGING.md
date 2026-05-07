# Broker WebSocket 监听端口日志输出

## ✅ 已实现的功能

当 Broker 启用 WebSocket 时，会在启动时输出以下日志信息：

### 1. 编译时配置信息

```
=================================================
Compile-time Macro Configuration:
 
WOLFMQTT_BROKER_DEBUG=true/false
WOLFMQTT_BROKER_AUTH=true/false
WOLFMQTT_BROKER_NO_INSECURE=true/false
WOLFMQTT_V5=true/false
WOLFMQTT_NONBLOCK=true/false
WOLFMQTT_MULTITHREAD=true/false
ENABLE_MQTT_TLS=true/false
ENABLE_MQTT_WEBSOCKET=true/false    ← 新增
=================================================
```

### 2. 运行时监听端口信息

```
[INFO] listening on port 1883 (plain)          ← TCP 监听
[INFO] listening on port 8883 (TLS)            ← TLS 监听（如果启用）
[INFO] listening on port 8080 (WebSocket)      ← WebSocket 监听（如果启用）
```

## 📝 代码位置

### 文件：`src/mqtt_broker.c`

#### 1. 编译时宏配置输出（Line ~5457-5462）

```c
#ifdef ENABLE_MQTT_WEBSOCKET
    PRINTF("ENABLE_MQTT_WEBSOCKET=true");
#else
    PRINTF("ENABLE_MQTT_WEBSOCKET=false");
#endif
```

#### 2. WebSocket 监听器启动（Line ~5546-5558）

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

## 🧪 测试方法

### 方法 1：使用命令行参数启动

```bash
# 在 ARM Linux 设备上运行
./mqtt_broker-musleabihf -p 1883 -w 8080
```

**预期输出**：
```
=================================================
Compile-time Macro Configuration:
 
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
[INFO] listening on port 8080 (WebSocket)
```

### 方法 2：只启用 TCP（不启用 WebSocket）

```bash
./mqtt_broker-musleabihf -p 1883
```

**预期输出**：
```
=================================================
Compile-time Macro Configuration:
 
...
ENABLE_MQTT_WEBSOCKET=true    ← 编译时启用了 WebSocket 支持
=================================================
[INFO] listening on port 1883 (plain)
← 没有 WebSocket 监听日志，因为没有指定 -w 参数
```

### 方法 3：同时启用 TCP、TLS 和 WebSocket

```bash
./mqtt_broker-musleabihf -p 1883 -t 8883 -w 8080 \
    --tls-cert server-cert.pem \
    --tls-key server-key.pem
```

**预期输出**：
```
=================================================
Compile-time Macro Configuration:
 
...
ENABLE_MQTT_TLS=true
ENABLE_MQTT_WEBSOCKET=true
=================================================
[INFO] listening on port 1883 (plain)
[INFO] listening on port 8883 (TLS)
[INFO] listening on port 8080 (WebSocket)
```

## 🔍 日志级别说明

### WBLOG_INFO 日志

- **条件**：需要定义 `WOLFMQTT_BROKER_LOG` 宏
- **输出目标**：stderr
- **格式**：`[INFO] <message>`

### 禁用日志

如果不希望看到这些日志，可以在编译时禁用：

```bash
# Zig 构建
zig build broker -Dstatic-link=true -Dbroker-log=false --release=small

# 或者在 configure 时
./configure --disable-broker-log
```

## 📊 完整的启动流程

```
MqttBroker_Init()
    ↓
MqttBroker_Start()
    ├─ 打印编译时宏配置
    │   └─ ENABLE_MQTT_WEBSOCKET=true/false
    │
    ├─ 启动 TCP 监听器
    │   └─ WBLOG_INFO("listening on port %d (plain)")
    │
    ├─ 启动 TLS 监听器（如果启用）
    │   └─ WBLOG_INFO("listening on port %d (TLS)")
    │
    └─ 启动 WebSocket 监听器（如果启用）  ← 新增
        └─ WBLOG_INFO("listening on port %d (WebSocket)")
    
    ↓
MqttBroker_Run() / MqttBroker_Step()
    └─ 主循环开始处理连接
```

## 🎯 关键变量

| 变量 | 类型 | 说明 |
|------|------|------|
| `broker->use_ws` | `byte` | 是否启用 WebSocket（通过 `-w` 参数设置） |
| `broker->port_ws` | `word16` | WebSocket 监听端口（默认 8080） |
| `broker->listen_sock_ws` | `BROKER_SOCKET_T` | WebSocket 监听套接字 |

## ⚠️ 注意事项

1. **编译时必须启用 WebSocket 支持**
   ```bash
   zig build broker -Dwebsocket=true ...
   ```
   否则 `ENABLE_MQTT_WEBSOCKET` 宏不会定义，相关代码会被条件编译排除。

2. **运行时必须指定 WebSocket 端口**
   ```bash
   ./mqtt_broker-musleabihf -w 8080
   ```
   如果不指定 `-w` 参数，`broker->use_ws` 为 0，不会启动 WebSocket 监听器。

3. **端口冲突检测**
   如果指定的端口已被占用，`broker->net.listen()` 会返回错误，并输出：
   ```
   [ERROR] WebSocket listen failed on port 8080 rc=-8
   ```

4. **日志输出目标**
   所有 `WBLOG_INFO` 日志都输出到 **stderr**，可以通过重定向查看：
   ```bash
   ./mqtt_broker-musleabihf -w 8080 2>&1 | grep WebSocket
   ```

## 📖 相关文档

- [BUILD_ARCHITECTURE.md](BUILD_ARCHITECTURE.md) - 构建架构说明
- [WEBSOCKET_INTEGRATION_COMPLETE.md](WEBSOCKET_INTEGRATION_COMPLETE.md) - WebSocket 集成指南
- [COMPILATION_FIX_COMPLETE.md](COMPILATION_FIX_COMPLETE.md) - 编译错误修复报告

---

**完成时间**：2026年5月6日  
**修改文件**：`src/mqtt_broker.c`  
**新增功能**：WebSocket 监听端口日志输出

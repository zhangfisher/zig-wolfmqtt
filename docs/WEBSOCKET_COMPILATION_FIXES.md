# Broker WebSocket 集成 - 编译错误修复清单

## 📋 当前状态

已完成统一传输层架构的核心实现，但编译时仍有错误需要修复。

## ❌ 编译错误汇总 (41个错误)

### 1. 旧的 libwebsockets 代码残留 (约20个错误)

**位置**: `src/mqtt_broker.c` line 1068-1521

**问题**: 注释掉 `#include <libwebsockets.h>` 后，所有使用 libwebsockets API 的代码都会报错

**解决方案**: **完全删除** line 1068-1521 之间的所有旧代码

这段代码包括：
- `BrokerClient_AddWs()` 
- `callback_broker_mqtt()`
- `BrokerWsNetRead/Write/Disconnect()`
- `BrokerWs_Start()`
- `BrokerWs_Free()`
- 所有相关的 lws 协议定义和回调

**操作**: 删除从 `#ifdef ENABLE_MQTT_WEBSOCKET` (line 1068) 到 `#endif /* ENABLE_MQTT_WEBSOCKET */` (line 1521) 的所有内容

---

### 2. 字符串函数宏未定义 (7个错误)

**位置**: `src/mqtt_websocket.c`

**错误**:
```
error: call to undeclared function 'XSTRSTR'
error: call to undeclared function 'XSTRNCASECMP'
```

**原因**: `mqtt_websocket.c` 没有包含正确的头文件来定义这些宏

**解决方案**: 在文件开头添加：
```c
#include "wolfmqtt/mqtt_types.h"  /* 定义 XSTRSTR, XSTRNCASECMP 等 */
```

或者直接使用标准 C 函数：
```c
// 替换 XSTRSTR -> strstr
// 替换 XSTRNCASECMP -> strncasecmp (Linux) 或 _strnicmp (Windows)
```

---

### 3. 错误码未定义 (4个错误)

**位置**: `src/mqtt_websocket.c`, `src/mqtt_broker_transport.c`

**错误**:
```
error: use of undeclared identifier 'MQTT_CODE_ERROR_BUFFER_E'
error: use of undeclared identifier 'MQTT_CODE_ERROR_NOT_IMPLEMENTED'
```

**原因**: 这些错误码在 `mqtt_types.h` 中不存在

**解决方案**: 使用现有的错误码替换
- `MQTT_CODE_ERROR_BUFFER_E` → `MQTT_CODE_ERROR_OUT_OF_BUFFER` (-2)
- `MQTT_CODE_ERROR_NOT_IMPLEMENTED` → `MQTT_CODE_ERROR_NETWORK` (-8)

---

### 4. Broker 宏未定义 (4个错误)

**位置**: `src/mqtt_websocket.c`

**错误**:
```
error: use of undeclared identifier 'BROKER_RX_BUF_SZ'
error: call to undeclared function 'BROKER_FORCE_ZERO'
```

**原因**: `mqtt_websocket.c` 没有包含 `mqtt_broker.h`

**解决方案**: 在文件开头添加：
```c
#include "wolfmqtt/mqtt_broker.h"  /* 定义 BROKER_RX_BUF_SZ, BROKER_FORCE_ZERO */
```

---

### 5. 日志宏未定义 (2个错误)

**位置**: `src/mqtt_broker_transport.c`

**错误**:
```
error: call to undeclared function 'WBLOG_INFO'
error: call to undeclared function 'WBLOG_ERR'
```

**原因**: 日志宏可能需要在特定条件下才定义

**解决方案**: 检查 `wolfmqtt/logger.h` 中的定义，确保：
```c
#ifdef WOLFMQTT_BROKER_LOG
    #define WBLOG_INFO(broker, ...) ...
    #define WBLOG_ERR(broker, ...) ...
#endif
```

或者临时替换为 `printf`：
```c
printf("WebSocket handshake completed on sock=%d\n", (int)bc->sock);
```

---

### 6. BrokerClient 结构体字段缺失 (1个错误)

**位置**: `src/mqtt_broker.c` line 1188

**错误**:
```
error: no member named 'ws_ctx' in 'struct BrokerClient'
```

**原因**: 我们之前移除了 `ws_ctx` 字段，改用 `transport.context`

**解决方案**: 这是旧代码的问题，删除整段旧代码即可解决

---

## 🔧 修复步骤

### Step 1: 删除旧的 libwebsockets 代码

在 `src/mqtt_broker.c` 中：

1. 找到 line 1068: `#ifdef ENABLE_MQTT_WEBSOCKET`
2. 找到 line 1521: `#endif /* ENABLE_MQTT_WEBSOCKET */`
3. **删除这两行之间的所有内容**（约450行）

保留的部分应该是：
```c
#endif /* WOLFMQTT_WOLFIP / !WOLFMQTT_BROKER_CUSTOM_NET */

/* -------------------------------------------------------------------------- */
/* Per-client MqttNet callbacks (route through MqttBrokerNet)                  */
/* -------------------------------------------------------------------------- */
static int BrokerNetConnect(void* context, const char* host, word16 port,
    int timeout_ms)
{
    // ...
}
```

### Step 2: 添加缺失的头文件

**src/mqtt_websocket.c** 开头添加：
```c
#include "wolfmqtt/mqtt_types.h"
#include "wolfmqtt/mqtt_broker.h"
```

**src/mqtt_broker_transport.c** 已有正确的头文件

### Step 3: 替换不存在的错误码

**src/mqtt_websocket.c**:
```c
// Line ~373, ~413, ~698
// 替换: MQTT_CODE_ERROR_BUFFER_E
// 为:   MQTT_CODE_ERROR_OUT_OF_BUFFER
```

**src/mqtt_broker_transport.c**:
```c
// Line ~452, ~465
// 替换: MQTT_CODE_ERROR_NOT_IMPLEMENTED  
// 为:   MQTT_CODE_ERROR_NETWORK
```

### Step 4: 修复日志宏（可选）

如果 `WBLOG_INFO/WBLOG_ERR` 仍然报错，可以临时替换为 printf 或删除这些日志语句。

---

## ✅ 修复后的预期结果

修复以上问题后，应该能够成功编译：

```bash
zig build broker -Dstatic-link=true -Dwebsocket=true --release=small
```

输出应该是：
```
broker
└─ install mqtt_broker-musleabihf
   └─ compile exe mqtt_broker-musleabihf ReleaseSmall arm-linux-musleabihf
      └─ compile lib wolfmqtt ReleaseSmall arm-linux-musleabihf success
```

生成的可执行文件：
```
zig-out/broker/linux-arm/mqtt_broker-musleabihf
```

---

## 📝 相关文档

- [WEBSOCKET_INTEGRATION_COMPLETE.md](WEBSOCKET_INTEGRATION_COMPLETE.md) - WebSocket 集成完成指南
- [BROKER_TRANSPORT_LAYER.md](BROKER_TRANSPORT_LAYER.md) - 统一传输层架构
- [BROKER_TRANSPORT_IMPLEMENTATION.md](BROKER_TRANSPORT_IMPLEMENTATION.md) - 实施总结

---

## 🎯 下一步

修复编译错误后，就可以：

1. **运行 Broker**
   ```bash
   ./zig-out/broker/linux-arm/mqtt_broker-musleabihf
   ```

2. **测试 WebSocket 连接**
   ```javascript
   const client = mqtt.connect('ws://localhost:8080/mqtt');
   ```

3. **验证完整功能**
   - CONNECT/DISCONNECT
   - PUBLISH/SUBSCRIBE
   - QoS 0/1/2
   - 保留消息
   - 遗嘱消息

---

**祝修复顺利！** 🚀

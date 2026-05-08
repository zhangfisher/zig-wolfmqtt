# wolfMQTT 日志系统使用规范

## 概述

wolfMQTT 项目使用统一的日志系统，但不同模块根据上下文使用不同的日志接口。

## 日志模块架构

```
logger.c (通用日志模块)
├── Log_Output()           - 默认日志输出（printf + 时间戳）
├── Log_DefaultCallback()  - 带颜色的日志回调
├── Log_OutputEx()         - 支持自定义回调
└── Log_GetLevelStr()      - 获取日志级别字符串

Broker 日志系统 (mqtt_broker.c)
├── broker_log()           - Broker 专用日志函数
│   ├── 调用 b->log 回调
│   └── 发布到 $sys/broker/logs (如果启用)
├── WBLOG_DBG/INFO/WARN/ERR/FATAL - Broker 日志宏
└── 支持运行时日志级别控制

API 日志系统 (mqtt_broker_api.c)
├── api_log()              - API 专用日志函数
│   └── 调用 broker->log 回调
└── BA_LOG_DBG/INFO/WARN/ERR/FATAL - API 日志宏

WebSocket 日志系统 (mqtt_websocket.c)
├── WS_LOG_ERR/WARN/INFO   - WebSocket 日志宏
│   └── 直接调用 Log_Output()
└── WS_LOG_DBG             - 调试日志（条件编译）
```

## 各模块日志使用规范

### 1. logger.c - 通用日志模块

**用途**: 提供基础日志功能，所有模块都可以使用

**导出函数**:
```c
void Log_Output(LogLevel level, const char* format, ...);
void Log_DefaultCallback(LogLevel level, const char* format, va_list args);
void Log_OutputEx(LogLevel level, MqttLogCb cb, const char* format, ...);
const char* Log_GetLevelStr(LogLevel level);
```

**适用场景**:
- ✅ 独立模块（无 broker 上下文）
- ✅ 客户端库代码
- ✅ 工具函数
- ✅ WebSocket 模块

**示例**:
```c
#include "wolfmqtt/logger.h"

// 简单日志输出
Log_Output(LOG_LEVEL_ERROR, "Connection failed: %s", strerror(errno));

// 带回调的日志
Log_OutputEx(LOG_LEVEL_INFO, my_custom_callback, "Processing request");
```

### 2. mqtt_broker.c - Broker 核心日志

**用途**: Broker 核心功能的日志，支持 MQTT 发布

**特性**:
- ✅ 遵循 `broker->log_level` 设置
- ✅ 自动发布到 `$sys/broker/logs` 主题
- ✅ 防止递归调用
- ✅ 支持自定义日志回调

**日志宏**:
```c
WBLOG_DBG(broker, ...)    // DEBUG 级别
WBLOG_INFO(broker, ...)   // INFO 级别
WBLOG_WARN(broker, ...)   // WARN 级别
WBLOG_ERR(broker, ...)    // ERROR 级别
WBLOG_FATAL(broker, ...)  // FATAL 级别
```

**适用场景**:
- ✅ Broker 核心逻辑
- ✅ 客户端管理
- ✅ 消息路由
- ✅ 订阅处理

**示例**:
```c
// 在 Broker 函数中
static int BrokerHandle_Connect(BrokerClient* bc, MqttBroker* broker) {
    WBLOG_INFO(broker, "Client %s connected from %s", 
               bc->client_id, bc->client_ip);
    
    if (some_error) {
        WBLOG_ERR(broker, "Failed to process CONNECT: rc=%d", rc);
        return rc;
    }
}
```

**注意事项**:
- ⚠️ 必须传入 `broker` 指针
- ⚠️ 日志会自动发布到 MQTT（如果启用 API）
- ⚠️ 已实现递归保护，无需额外处理

### 3. mqtt_broker_api.c - API 日志

**用途**: HTTP API 相关的日志

**特性**:
- ✅ 使用 broker 的日志回调（`broker->log`）
- ✅ 遵循 broker 的日志级别
- ✅ 与 Broker 日志保持一致

**日志宏**:
```c
BA_LOG_DBG(broker, ...)    // DEBUG 级别
BA_LOG_INFO(broker, ...)   // INFO 级别
BA_LOG_WARN(broker, ...)   // WARN 级别
BA_LOG_ERR(broker, ...)    // ERROR 级别
BA_LOG_FATAL(broker, ...)  // FATAL 级别
```

**适用场景**:
- ✅ HTTP 请求处理
- ✅ API 响应
- ✅ 认证授权
- ✅ 配置管理

**示例**:
```c
static int handle_get_stats(MqttBrokerApiContext* api_ctx, ...) {
    MqttBroker* broker = api_ctx->broker;
    
    BA_LOG_INFO(broker, "API GET /stats from %s", client_ip);
    
    if (error) {
        BA_LOG_ERR(broker, "Failed to get stats: %s", error_msg);
        return error_code;
    }
}
```

**为什么不用 WBLOG?**
- API 模块有自己的日志前缀和格式
- 可以独立控制 API 日志的启用/禁用（`WOLFMQTT_BROKER_LOG` 宏）
- 避免与 Broker 核心日志混淆

### 4. mqtt_websocket.c - WebSocket 日志

**用途**: WebSocket 协议处理的日志

**特性**:
- ✅ 使用通用日志函数 `Log_Output()`
- ✅ 无 broker 上下文依赖
- ✅ 简单直接

**日志宏**:
```c
WS_LOG_ERR(...)     // ERROR 级别（始终启用）
WS_LOG_WARN(...)    // WARN 级别（始终启用）
WS_LOG_INFO(...)    // INFO 级别（始终启用）
WS_LOG_DBG(...)     // DEBUG 级别（需要 WOLFMQTT_DEBUG_WEBSOCKET）
```

**适用场景**:
- ✅ WebSocket 握手
- ✅ 帧解析
- ✅ 连接管理
- ✅ 错误处理

**示例**:
```c
int WebSocket_Upgrade(MqttBroker* broker, BROKER_SOCKET_T sock, ...) {
    WS_LOG_INFO("WebSocket upgrade request from socket %d", sock);
    
    if (invalid_key) {
        WS_LOG_ERR("Invalid Sec-WebSocket-Key header");
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
#ifdef WOLFMQTT_DEBUG_WEBSOCKET
    WS_LOG_DBG("Parsed headers: %d bytes", header_len);
#endif
}
```

**注意事项**:
- ⚠️ 不遵循 broker 的日志级别（直接使用 `Log_Output()`）
- ⚠️ DEBUG 日志需要编译时启用 `WOLFMQTT_DEBUG_WEBSOCKET`
- ⚠️ 不会发布到 MQTT

## 日志级别定义

```c
typedef enum {
    LOG_LEVEL_DEBUG = 0,  // 最详细，包含所有调试信息
    LOG_LEVEL_INFO,       // 一般信息（推荐生产环境）
    LOG_LEVEL_WARN,       // 警告信息
    LOG_LEVEL_ERROR,      // 错误信息
    LOG_LEVEL_FATAL       // 致命错误
} LogLevel;
```

## 最佳实践

### 1. 选择合适的日志宏

| 模块 | 有 broker 上下文 | 无 broker 上下文 |
|------|-----------------|-----------------|
| Broker 核心 | `WBLOG_*` | N/A |
| Broker API | `BA_LOG_*` | N/A |
| WebSocket | N/A | `WS_LOG_*` |
| 客户端库 | N/A | `Log_Output()` |
| 工具函数 | N/A | `Log_Output()` |

### 2. 日志级别选择

- **DEBUG**: 详细的调试信息，变量值、中间状态
  ```c
  WBLOG_DBG(broker, "Processing packet type=%d len=%d", pkt_type, len);
  ```

- **INFO**: 重要事件，连接、断开、配置变更
  ```c
  WBLOG_INFO(broker, "Client %s connected", client_id);
  ```

- **WARN**: 潜在问题，但不影响功能
  ```c
  WBLOG_WARN(broker, "Subscription queue full, dropping oldest");
  ```

- **ERROR**: 错误情况，功能受影响
  ```c
  WBLOG_ERR(broker, "Failed to send PUBLISH: rc=%d", rc);
  ```

- **FATAL**: 致命错误，程序可能崩溃
  ```c
  WBLOG_FATAL(broker, "Out of memory, cannot allocate client");
  ```

### 3. 性能考虑

**Broker 日志 (WBLOG/BA_LOG)**:
- ✅ 受 `broker->log_level` 控制
- ✅ DEBUG 级别在生产环境应禁用
- ⚠️ 每条日志会发布到 MQTT（如果启用 API）
- ⚠️ 高频日志会影响性能

**通用日志 (Log_Output/WS_LOG)**:
- ✅ 不受运行时控制（始终输出）
- ⚠️ 适合低频日志（错误、警告）
- ❌ 不适合高频调试日志

### 4. 日志内容规范

**好的日志**:
```c
WBLOG_ERR(broker, "PUBLISH decode failed: client=%s topic=%s rc=%d",
          client_id, topic_name, rc);
```

**不好的日志**:
```c
WBLOG_ERR(broker, "Error occurred");  // ❌ 缺少上下文
WBLOG_ERR(broker, "rc=%d", rc);       // ❌ 不清楚是什么操作
```

**原则**:
- ✅ 包含足够的上下文（谁、什么、哪里）
- ✅ 包含错误码或原因
- ✅ 使用有意义的变量名
- ❌ 避免敏感信息（密码、密钥）

## 编译时控制

### 启用/禁用日志

**Broker 日志**:
```c
// 在 build.zig 或 configure 中
module.addCMacro("WOLFMQTT_BROKER_LOG", "1");  // 启用
module.addCMacro("WOLFMQTT_BROKER_LOG", "0");  // 禁用（所有 BA_LOG 为空）
```

**WebSocket 调试日志**:
```c
module.addCMacro("WOLFMQTT_DEBUG_WEBSOCKET", "1");  // 启用 WS_LOG_DBG
```

### 日志级别控制

**Broker 运行时控制**:
```c
broker->log_level = LOG_LEVEL_INFO;  // 只输出 INFO 及以上
```

**通用日志**: 无法运行时控制（始终输出）

## 常见问题

### Q1: 为什么 Broker 和 API 使用不同的日志宏？

**A**: 
- `WBLOG_*`: Broker 核心逻辑，关注协议处理
- `BA_LOG_*`: HTTP API 逻辑，关注 REST 请求
- 分离后可以独立控制和过滤

### Q2: WebSocket 为什么不使用 broker 的日志系统？

**A**:
- WebSocket 模块可能在无 broker 上下文的场景使用
- 保持模块独立性
- 简化代码结构

### Q3: 如何完全禁用日志以减小二进制大小？

**A**:
```c
// 方法 1: 编译时禁用
module.addCMacro("WOLFMQTT_BROKER_LOG", "0");

// 方法 2: 移除日志调用
// 手动删除或使用 sed 脚本
```

### Q4: 日志发布到 MQTT 会导致递归吗？

**A**:
- ✅ 已实现递归保护（`s_log_publishing` 标志）
- ✅ 线程安全（`__thread` 存储）
- ✅ 无需额外处理

## 总结

| 特性 | WBLOG | BA_LOG | WS_LOG | Log_Output |
|------|-------|--------|--------|------------|
| 需要 broker 上下文 | ✅ | ✅ | ❌ | ❌ |
| 运行时级别控制 | ✅ | ✅ | ❌ | ❌ |
| MQTT 发布 | ✅ | ❌ | ❌ | ❌ |
| 编译时禁用 | ✅ | ✅ | 部分 | ❌ |
| 适用模块 | Broker 核心 | Broker API | WebSocket | 通用 |

**推荐用法**:
- Broker 模块 → 使用 `WBLOG_*` 或 `BA_LOG_*`
- WebSocket 模块 → 使用 `WS_LOG_*`
- 其他模块 → 使用 `Log_Output()`

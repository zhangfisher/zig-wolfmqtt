# Broker 统一日志系统 - 快速参考

## 使用方法

### 包含头文件
```c
#include "wolfmqtt/mqtt_broker_logger.h"
```

### 日志宏（所有模块通用）

#### 推荐使用的新宏
```c
BROKER_LOG_DBG(broker, "Debug message: %d", value);
BROKER_LOG_INFO(broker, "Info message: %s", str);
BROKER_LOG_WARN(broker, "Warning message");
BROKER_LOG_ERR(broker, "Error: %s", error_msg);
BROKER_LOG_FATAL(broker, "Fatal error!");
```

#### 向后兼容的旧宏（仍然可用）
```c
// mqtt_broker.c 和 mqtt_broker_transport.c
WBLOG_DBG(broker, ...);   // → BROKER_LOG_DBG
WBLOG_INFO(broker, ...);  // → BROKER_LOG_INFO
WBLOG_WARN(broker, ...);  // → BROKER_LOG_WARN
WBLOG_ERR(broker, ...);   // → BROKER_LOG_ERR
WBLOG_FATAL(broker, ...); // → BROKER_LOG_FATAL

// mqtt_broker_api.c
BA_LOG_DBG(broker, ...);   // → BROKER_LOG_DBG
BA_LOG_INFO(broker, ...);  // → BROKER_LOG_INFO
BA_LOG_WARN(broker, ...);  // → BROKER_LOG_WARN
BA_LOG_ERR(broker, ...);   // → BROKER_LOG_ERR
BA_LOG_FATAL(broker, ...); // → BROKER_LOG_FATAL
```

## 日志级别

```c
typedef enum {
    LOG_LEVEL_DEBUG = 0,  // 最详细
    LOG_LEVEL_INFO,       // 一般信息（默认）
    LOG_LEVEL_WARN,       // 警告
    LOG_LEVEL_ERROR,      // 错误
    LOG_LEVEL_FATAL       // 致命错误
} LogLevel;
```

### 设置日志级别
```c
broker->log_level = LOG_LEVEL_INFO;  // 只输出 INFO 及以上
```

## 特性

### ✅ 自动 MQTT 发布
- 主题: `$sys/broker/logs`
- QoS: 0
- Retain: false
- 条件: `broker->api_ctx != NULL`

### ✅ 递归保护
- 线程局部标志 `s_log_publishing`
- 防止无限递归
- 无需手动处理

### ✅ 运行时控制
- 通过 `broker->log_level` 动态调整
- 无需重新编译

### ✅ 编译时控制
```c
// 在 build.zig 中
module.addCMacro("WOLFMQTT_BROKER_LOG", "1");  // 启用
module.addCMacro("WOLFMQTT_BROKER_LOG", "0");  // 禁用（所有宏为空）
```

## 示例

### 基本用法
```c
static int BrokerHandle_Connect(BrokerClient* bc, MqttBroker* broker) {
    BROKER_LOG_INFO(broker, "Client %s connecting from %s", 
                    bc->client_id, bc->client_ip);
    
    if (auth_failed) {
        BROKER_LOG_ERR(broker, "Authentication failed for client %s", 
                       bc->client_id);
        return MQTT_CODE_ERROR_AUTH;
    }
    
    BROKER_LOG_INFO(broker, "Client %s connected successfully", 
                    bc->client_id);
    return MQTT_CODE_SUCCESS;
}
```

### 调试日志
```c
#ifdef WOLFMQTT_DEBUG_BROKER
    BROKER_LOG_DBG(broker, "Processing packet: type=%d len=%d", 
                   pkt_type, pkt_len);
#endif
```

### 条件日志
```c
if (verbose_mode) {
    BROKER_LOG_INFO(broker, "Detailed status: clients=%d subs=%d", 
                    broker->stats.connected_clients,
                    broker->stats.total_subscriptions);
}
```

## 模块特定说明

### mqtt_broker.c
- 使用 `WBLOG_*` 或 `BROKER_LOG_*`
- 两者等效（兼容性宏）

### mqtt_broker_api.c  
- 使用 `BA_LOG_*` 或 `BROKER_LOG_*`
- `BA_LOG_*` 更具可读性（表明是 API 日志）

### mqtt_broker_transport.c
- 使用 `WBLOG_*` 或 `BROKER_LOG_*`
- 之前使用 `fprintf`，现在统一为回调 + MQTT

### mqtt_websocket.c
- 保持独立，使用 `WS_LOG_*`
- 原因: 可能在无 broker 上下文使用

## 常见问题

### Q: 如何完全禁用日志？
```c
// 方法 1: 编译时禁用
module.addCMacro("WOLFMQTT_BROKER_LOG", "0");

// 方法 2: 运行时设置为最高级别
broker->log_level = LOG_LEVEL_FATAL;
```

### Q: 日志会发布到 MQTT 吗？
是的，如果：
1. `broker->api_ctx != NULL`（API 已初始化）
2. 日志级别 >= `broker->log_level`
3. 不在递归中（自动保护）

### Q: 会影响性能吗？
- 影响很小（与之前相当）
- DEBUG 级别在高频率下可能有影响
- 生产环境建议使用 INFO 或 WARN

### Q: 可以自定义日志输出吗？
是的，设置 `broker->log` 回调：
```c
void my_log_callback(LogLevel level, const char* format, va_list args) {
    // 自定义输出（文件、网络等）
    vfprintf(log_file, format, args);
}

broker->log = my_log_callback;
```

## 最佳实践

1. **选择合适的级别**
   - DEBUG: 开发调试，详细状态
   - INFO: 重要事件（连接、断开）
   - WARN: 潜在问题
   - ERROR: 错误情况
   - FATAL: 致命错误

2. **提供足够上下文**
   ```c
   // ✅ 好
   BROKER_LOG_ERR(broker, "PUBLISH failed: client=%s topic=%s rc=%d",
                  client_id, topic, rc);
   
   // ❌ 不好
   BROKER_LOG_ERR(broker, "Error occurred");
   ```

3. **避免敏感信息**
   ```c
   // ❌ 不要记录密码
   BROKER_LOG_DBG(broker, "Password: %s", password);
   
   // ✅ 可以记录用户名
   BROKER_LOG_INFO(broker, "Auth attempt: user=%s", username);
   ```

4. **生产环境配置**
   ```c
   broker->log_level = LOG_LEVEL_INFO;  // 推荐
   // 或
   broker->log_level = LOG_LEVEL_WARN;  // 更简洁
   ```

## 迁移指南

### 从旧代码迁移
无需修改！兼容性宏确保现有代码继续工作。

### 新代码建议
直接使用 `BROKER_LOG_*` 宏：
```c
// 推荐
BROKER_LOG_INFO(broker, "New feature working");

// 仍然有效（但建议使用上面的）
WBLOG_INFO(broker, "New feature working");
```

## 相关文件

- `wolfmqtt/mqtt_broker_logger.h` - 接口定义
- `src/mqtt_broker_logger.c` - 实现代码
- `docs/BROKER_UNIFIED_LOGGER_DESIGN.md` - 设计文档
- `docs/BROKER_UNIFIED_LOGGER_COMPLETE.md` - 实施报告

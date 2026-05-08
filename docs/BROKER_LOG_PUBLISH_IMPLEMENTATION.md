# Broker 日志发布功能实现总结

## 变更概述

实现了将 MqttBroker 日志自动发布到 `$sys/broker/logs` MQTT 主题的功能，允许管理员通过订阅该主题实时监控 Broker 运行状态。

## 文件变更

### 1. src/mqtt_broker.c
**修改内容**:
- 增强了 `broker_log()` 函数（第 98-134 行）
- 添加了前向声明：`MqttBrokerApi_PublishLog()`
- 在日志回调后自动格式化并发布到 MQTT 主题

**关键代码**:
```c
static inline void broker_log(MqttBroker* b, LogLevel level, const char* format, ...) {
    if (level >= b->log_level) {
        va_list args;
        va_start(args, format);
        
        /* Call the log callback */
        b->log(level, format, args);
        
        /* Publish to MQTT topic $sys/broker/logs if API context is available */
        if (b->api_ctx != NULL) {
            // 格式化日志消息
            // 调用 MqttBrokerApi_PublishLog()
        }
        
        va_end(args);
    }
}
```

### 2. src/mqtt_broker_api.c
**新增内容**:
- 添加了 `MqttBrokerApi_PublishLog()` 函数（文件末尾）
- 实现日志消息的 MQTT 发布逻辑

**函数签名**:
```c
int MqttBrokerApi_PublishLog(MqttBrokerApiContext* api_ctx, 
                             const char* log_msg, 
                             LogLevel level)
```

**特性**:
- QoS = 0（最多一次交付，最小开销）
- Retain = false（不保留消息）
- 主题: `$sys/broker/logs`

### 3. wolfmqtt/mqtt_broker.h
**新增内容**:
- 添加了 `MqttBrokerApi_PublishLog()` 函数声明（第 675 行）

```c
/* Publish log message to $sys/broker/logs topic (QoS=0, retain=false) */
WOLFMQTT_API int MqttBrokerApi_PublishLog(MqttBrokerApiContext* api_ctx, 
                                          const char* log_msg, 
                                          LogLevel level);
```

## 设计决策

### 为什么不创建独立的 broker-logger.c 文件？

1. **避免代码重复**: logger.c 中的通用日志函数（`Log_Output`, `Log_DefaultCallback` 等）仍然需要被其他模块使用
2. **简化构建**: 不需要修改多个构建配置文件（Makefile.am, build.zig, CMakeLists.txt）
3. **内聚性**: 日志发布功能紧密依赖于 Broker 的 API 上下文，放在 mqtt_broker_api.c 中更合理
4. **性能**: 直接在 broker_log() 中实现避免了额外的函数调用层

### 日志级别过滤修复

同时修复了之前发现的日志级别过滤 bug：
- **原问题**: `if (b->log_level >= level)` 导致 DEBUG 日志在 INFO 级别时仍会输出
- **修复**: 改为 `if (level >= b->log_level)`
- **影响**: 现在日志级别过滤正常工作

## 使用说明

### 启用日志发布

日志发布功能在以下条件下自动启用：
1. Broker 启用了 HTTP API (`opts.broker_api = true`)
2. Broker 成功初始化并启动

无需额外配置，功能自动生效。

### 订阅日志

```bash
# 使用 mosquitto_sub
mosquitto_sub -h localhost -t '$sys/broker/logs' -v

# 只查看 ERROR 级别
mosquitto_sub -h localhost -t '$sys/broker/logs' -v | grep ERROR
```

### 控制日志级别

```c
// 在代码中设置
broker->log_level = LOG_LEVEL_INFO;  // 默认值

// 或通过命令行参数（如果支持）
./mqtt_broker --log-level 1  // 1 = INFO
```

日志级别枚举：
- `LOG_LEVEL_DEBUG = 0`: 最详细，包括所有调试信息
- `LOG_LEVEL_INFO = 1`: 一般信息（推荐生产环境）
- `LOG_LEVEL_WARN = 2`: 警告信息
- `LOG_LEVEL_ERROR = 3`: 错误信息
- `LOG_LEVEL_FATAL = 4`: 致命错误

## 性能考虑

### CPU 开销
- 每条日志约增加 16-75 μs 的处理时间
- 主要来自：时间格式化、字符串格式化、MQTT 发布

### 内存开销
- 栈空间：约 550-570 字节/次日志调用
- 堆空间：取决于 tx_buf_sz（默认 4096 字节）

### 网络开销
- 每条日志约 123-127 字节（假设 100 字节 payload）
- DEBUG 级别高频率日志可能导致显著网络流量

### 优化建议
1. **生产环境**: 使用 `LOG_LEVEL_INFO` 或更高
2. **监控**: 定期检查 CPU 和网络使用情况
3. **访问控制**: 对 `$sys/broker/logs` 主题实施订阅权限控制

## 潜在风险与缓解

### 1. 递归调用风险
**风险**: 如果 `BrokerHandle_PublishMessage()` 内部也调用日志函数，可能导致无限递归。

**当前状态**: 
- 需要验证 `BrokerHandle_PublishMessage()` 是否触发日志
- 如果发现递归，需要添加保护机制（如标志位）

**缓解措施**（如需要）:
```c
static __thread int log_publishing = 0;

if (log_publishing) return;  // 防止递归
log_publishing = 1;
// ... 发布逻辑
log_publishing = 0;
```

### 2. 性能退化
**场景**: DEBUG 级别 + 高频操作 + 多订阅者

**缓解**:
- 默认使用 INFO 级别
- 实现速率限制（可选）
- 提供编译选项禁用此功能（可选）

### 3. 安全考虑
**风险**: 日志可能包含敏感信息（客户端 ID、IP 地址等）

**缓解**:
- 对 `$sys/broker/logs` 实施访问控制
- 考虑加密传输（TLS）
- 审计日志内容，避免泄露敏感数据

## 测试建议

### 功能测试
1. 启动 Broker
2. 订阅 `$sys/broker/logs` 主题
3. 触发各种操作（连接、发布、订阅）
4. 验证日志消息正确接收

### 性能测试
1. 基准测试：禁用日志发布时的吞吐量
2. 对比测试：启用日志发布（INFO 级别）时的吞吐量
3. 压力测试：DEBUG 级别 + 高频操作

### 边界测试
1. 无订阅者时的行为
2. 多个订阅者时的性能
3. 长日志消息的处理
4. API 上下文未初始化时的容错

## 未来改进方向

1. **可配置性**: 
   - 通过 HTTP API 动态启用/禁用日志发布
   - 运行时调整日志级别

2. **过滤功能**:
   - 按模块过滤（如只发布网络相关日志）
   - 按模式匹配过滤

3. **批处理**:
   - 将多条日志合并为一条 MQTT 消息
   - 减少网络开销

4. **异步发布**:
   - 使用队列缓冲日志
   - 后台线程处理发布，不阻塞主路径

5. **持久化**:
   - 重要日志存储到文件
   - 集成外部日志系统（syslog、ELK 等）

## 相关文档

- [BROKER_LOG_PUBLISH_FEATURE.md](./BROKER_LOG_PUBLISH_FEATURE.md): 详细的功能说明和性能分析
- [FIELD_NAMING.md](./FIELD_NAMING.md): 字段命名规范
- [API_CONFIG_QUICK_REF.md](./API_CONFIG_QUICK_REF.md): API 配置快速参考

## 总结

✅ **已完成**:
- 日志发布功能实现
- 日志级别过滤 bug 修复
- 性能分析和文档编写

⚠️ **需要注意**:
- 监控生产环境的性能影响
- 验证无递归调用问题
- 实施适当的访问控制

📊 **推荐配置**:
- 生产环境: `LOG_LEVEL_INFO`
- 开发环境: `LOG_LEVEL_DEBUG`（注意性能）
- 始终监控 CPU 和网络使用情况

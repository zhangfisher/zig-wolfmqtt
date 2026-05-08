# Broker 日志发布到 MQTT 主题功能说明

## 功能概述

实现了将 Broker 日志自动发布到 `$sys/broker/logs` MQTT 主题的功能，管理员可以通过订阅该主题实时接收 Broker 日志。

## 实现细节

### 1. 日志发布配置
- **主题**: `$sys/broker/logs`
- **QoS**: 0 (最多一次交付，最小开销)
- **Retain**: false (不保留消息)
- **触发条件**: 当日志级别 >= broker->log_level 时

### 2. 代码修改

#### 2.1 mqtt_broker_api.c
添加了 `MqttBrokerApi_PublishLog()` 函数：
```c
int MqttBrokerApi_PublishLog(MqttBrokerApiContext* api_ctx, 
                             const char* log_msg, 
                             LogLevel level)
```

#### 2.2 mqtt_broker.c
增强了 `broker_log()` 函数，在调用日志回调后自动发布到 MQTT：
- 格式化日志消息（包含时间戳和级别）
- 检查 API 上下文是否可用
- 调用 `MqttBrokerApi_PublishLog()` 发布消息

#### 2.3 mqtt_broker.h
添加了函数声明：
```c
WOLFMQTT_API int MqttBrokerApi_PublishLog(MqttBrokerApiContext* api_ctx, 
                                          const char* log_msg, 
                                          LogLevel level);
```

## 性能影响分析

### 1. CPU 开销

#### 额外操作：
- **时间格式化**: `localtime()` + `strftime()` ≈ 1-5 μs
- **字符串格式化**: `snprintf()` + `vsnprintf()` ≈ 5-20 μs（取决于日志长度）
- **MQTT 发布**: `BrokerHandle_PublishMessage()` ≈ 10-50 μs

**总计**: 每条日志约 16-75 μs 的额外 CPU 时间

#### 影响因素：
- 日志频率：高频日志（如 DEBUG 级别）会显著增加 CPU 负载
- 订阅者数量：如果有多个客户端订阅 `$sys/broker/logs`，每次发布需要复制消息给每个订阅者
- 消息长度：长日志消息会增加字符串处理和内存复制时间

### 2. 内存开销

#### 栈空间：
- `log_msg[512]`: 512 字节（每次日志调用时分配在栈上）
- `time_str[20]`: 20 字节
- `va_list`: 平台相关，通常 16-32 字节

**总计**: 约 550-570 字节栈空间 per 日志调用

#### 堆空间：
- MQTT 消息缓冲区：取决于 broker 的 tx_buf_sz（默认 4096 字节）
- 如果启用零拷贝优化，使用消息池（BROKER_MSG_POOL_SIZE * BROKER_MSG_MAX_SIZE）

### 3. 网络开销

#### 带宽消耗：
假设平均日志消息长度为 100 字节：
- MQTT 固定头部：2-5 字节
- 剩余长度编码：1-2 字节
- 主题名称 `$sys/broker/logs`：18 字节
- 主题长度编码：2 字节
- Payload：100 字节

**每条约**: 123-127 字节网络传输

#### 频率影响：
- INFO 级别：每秒约 1-10 条 → 123-1270 字节/秒
- DEBUG 级别：每秒可能 100-1000 条 → 12.3-127 KB/秒 ⚠️

### 4. 潜在问题

#### 4.1 递归风险
⚠️ **重要**: 如果 `BrokerHandle_PublishMessage()` 内部也调用日志函数，可能导致无限递归。

**当前实现的安全性**：
- `MqttBrokerApi_PublishLog()` 直接调用 `BrokerHandle_PublishMessage()`
- 需要确保 `BrokerHandle_PublishMessage()` 不会触发日志记录
- 或者在发布日志时临时禁用日志发布

#### 4.2 性能退化场景

**最坏情况**：
- DEBUG 级别日志 + 高频率操作（如大量 PUBLISH/SUBSCRIBE）
- 多个客户端订阅 `$sys/broker/logs`
- 长日志消息

**估算影响**：
```
假设：1000 条日志/秒，每条 100 字节，5 个订阅者
- CPU: 1000 × 75μs = 75ms/s = 7.5% CPU
- 网络: 1000 × 127 × 5 = 635 KB/s
- 内存: 栈空间峰值 570KB/s（但会快速释放）
```

### 5. 优化建议

#### 5.1 推荐配置
```c
// 生产环境
broker->log_level = LOG_LEVEL_INFO;  // 或 LOG_LEVEL_WARN

// 开发/调试环境
broker->log_level = LOG_LEVEL_DEBUG;
```

#### 5.2 条件编译控制
可以添加编译选项来控制是否启用日志发布：

```c
#ifdef WOLFMQTT_BROKER_LOG_PUBLISH
    /* 启用日志发布功能 */
    if (b->api_ctx != NULL) {
        // ... 发布逻辑
    }
#endif
```

#### 5.3 速率限制
可以实现简单的速率限制：

```c
static unsigned long last_log_publish_time = 0;
#define LOG_PUBLISH_INTERVAL_MS 100  // 最少间隔 100ms

unsigned long now = WOLFMQTT_BROKER_GET_TIME_S() * 1000;
if (now - last_log_publish_time < LOG_PUBLISH_INTERVAL_MS) {
    return;  // 跳过此次发布
}
last_log_publish_time = now;
```

#### 5.4 异步发布
将日志发布放到单独的队列中，由后台线程处理：
- 优点：不阻塞主日志路径
- 缺点：增加复杂度，可能丢失日志

### 6. 监控建议

启用此功能后，建议监控以下指标：

1. **CPU 使用率**: 观察 broker 进程的 CPU 占用
2. **网络流量**: 监控 `$sys/broker/logs` 主题的流量
3. **订阅者数量**: 跟踪有多少客户端订阅了日志主题
4. **日志频率**: 统计不同级别的日志产生速率

## 使用示例

### 订阅 Broker 日志

```bash
# 使用 mosquitto_sub 订阅日志
mosquitto_sub -h localhost -t '$sys/broker/logs' -v

# 只订阅 ERROR 级别（需要在客户端过滤）
mosquitto_sub -h localhost -t '$sys/broker/logs' -v | grep ERROR
```

### 编程方式订阅

```c
MqttClient client;
MqttSubscribe sub;

// 连接到 broker
MqttClient_Connect(&client, ...);

// 订阅日志主题
sub.topic_count = 1;
sub.topics[0].topic_filter = "$sys/broker/logs";
sub.topics[0].qos = MQTT_QOS_0;
MqttClient_Subscribe(&client, &sub);

// 接收日志消息
while (running) {
    MqttMessage msg;
    MqttClient_WaitMessage(&client, &msg);
    printf("Log: %.*s\n", msg.total_len, msg.buffer);
}
```

## 总结

### 优点
✅ 实时监控 Broker 运行状态  
✅ 无需 SSH 或文件访问即可查看日志  
✅ 支持远程管理和故障诊断  
✅ 可集成到监控系统（如 Grafana、Prometheus）  

### 缺点
⚠️ 增加 CPU 和网络开销（尤其是 DEBUG 级别）  
⚠️ 高频率日志可能影响 Broker 性能  
⚠️ 需要注意递归调用风险  
⚠️ 未加密传输可能泄露敏感信息  

### 最佳实践
1. **生产环境**: 使用 `LOG_LEVEL_INFO` 或 `LOG_LEVEL_WARN`
2. **开发环境**: 可以使用 `LOG_LEVEL_DEBUG`，但注意监控性能
3. **安全考虑**: 对 `$sys/broker/logs` 主题实施访问控制
4. **性能监控**: 定期检查 CPU 和网络使用情况
5. **日志轮转**: 考虑实现日志消息的大小限制和速率限制

## 未来改进方向

1. **可配置性**: 通过 HTTP API 动态启用/禁用日志发布
2. **过滤功能**: 允许按级别、模块过滤日志
3. **批处理**: 将多条日志合并为一条 MQTT 消息
4. **压缩**: 对长日志消息进行压缩
5. **持久化**: 将重要日志存储到数据库或文件系统

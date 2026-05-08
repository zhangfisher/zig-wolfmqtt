# 客户端连接状态消息功能

## 概述

此功能在MQTT Broker中实现了客户端连接和断开时的系统消息发布。当客户端连接到broker或从broker断开时，会自动向特定的系统主题发布JSON格式的状态消息。

## 功能特性

### 1. 连接消息
- **主题**: `$sys/broker/clients/connected`
- **触发时机**: 客户端成功完成CONNECT握手后
- **消息格式**: JSON字符串

### 2. 断开消息
- **主题**: `$sys/broker/clients/disconnected`
- **触发时机**: 客户端断开连接时（包括正常断开、超时、被踢出等）
- **消息格式**: JSON字符串

## 消息格式

```json
{
  "id": "clientId",
  "ip": "IP地址",
  "type": "tcp或websocket"
}
```

### 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| id | string | 客户端ID |
| ip | string | 客户端IP地址 |
| type | string | 传输类型："TCP"、"TLS" 或 "WebSocket" |

## 使用示例

### 订阅系统消息

```bash
# 订阅所有客户端状态消息
mosquitto_sub -h localhost -t '$sys/broker/clients/#'

# 只订阅连接消息
mosquitto_sub -h localhost -t '$sys/broker/clients/connected'

# 只订阅断开消息
mosquitto_sub -h localhost -t '$sys/broker/clients/disconnected'
```

### 示例输出

当一个名为 `test-client-1` 的客户端通过TCP连接时：

```
Topic: $sys/broker/clients/connected
Message: {"id":"test-client-1","ip":"127.0.0.1","type":"TCP"}
```

当该客户端断开时：

```
Topic: $sys/broker/clients/disconnected
Message: {"id":"test-client-1","ip":"127.0.0.1","type":"TCP"}
```

### WebSocket客户端示例

当客户端通过WebSocket连接时：

```
Topic: $sys/broker/clients/connected
Message: {"id":"ws-client-1","ip":"192.168.1.100","type":"WebSocket"}
```

## 实现细节

### 代码位置

- **辅助函数**: `BrokerPublish_ClientStatus()` in `src/mqtt_broker.c`
- **连接调用点**: 客户端CONNECT处理成功后
- **断开调用点**: `BrokerClient_Remove()` 函数中

### 技术要点

1. **传输类型检测**: 使用 `BrokerTransport_GetName()` 函数获取客户端的传输类型
2. **消息质量**: QoS 0，不保留（retain = false）
3. **错误处理**: 如果发布失败，仅记录错误日志，不影响客户端连接/断开流程

### 支持的传输类型

- **TCP**: 标准TCP连接
- **TLS**: TLS加密连接（如果启用了TLS支持）
- **WebSocket**: WebSocket连接（如果启用了WebSocket支持）

## 应用场景

### 1. 实时监控
监控系统可以订阅这些主题来实时跟踪客户端的连接状态。

### 2. 审计日志
记录所有客户端的连接和断开事件，用于安全审计。

### 3. 负载均衡
根据当前连接的客户端数量进行负载均衡决策。

### 4. 故障诊断
快速识别客户端连接问题，例如频繁的连接/断开循环。

### 5. 客户端管理
自动化的客户端管理系统可以根据这些消息执行相应的操作。

## 测试

运行测试脚本验证功能：

```bash
chmod +x test_client_status.sh
./test_client_status.sh
```

## 注意事项

1. **性能影响**: 每次连接/断开都会发布一条消息，在高并发场景下需要考虑性能影响
2. **主题命名**: 使用 `$sys/` 前缀表示这是系统主题，通常不会被普通客户端订阅
3. **消息可靠性**: 使用QoS 0，不保证消息送达，适合监控场景
4. **JSON格式**: 消息采用JSON格式，便于解析和处理

## 配置选项

目前此功能默认启用，无需额外配置。如果需要禁用，可以在编译时添加相应的宏定义。

## 兼容性

- 兼容MQTT 3.1.1和MQTT 5.0协议
- 支持所有传输层类型（TCP、TLS、WebSocket）
- 与现有的broker功能完全兼容

## 未来扩展

可能的扩展方向：

1. 添加更多客户端信息（如协议版本、clean session标志等）
2. 支持可配置的消息格式
3. 添加连接持续时间统计
4. 支持消息过滤和聚合

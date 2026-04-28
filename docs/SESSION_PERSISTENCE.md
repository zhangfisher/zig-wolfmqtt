# 持久会话功能说明

## 概述

wolfMQTT Broker 现在支持 MQTT 3.1.1 和 MQTT 5 的持久会话功能。当客户端断开连接时，其订阅状态可以被保留，在重连后自动恢复。

## 默认会话过期间隔

Broker 提供了 `default_session_expiry_interval` 配置选项，用于指定持久会话的默认过期间隔：

- **默认值**: 180 秒（3 分钟）
- **作用**: 当客户端没有明确指定会话过期间隔时使用
- **命名**: `default_session_expiry_interval` (蛇形命名，与 BrokerOptions 其他字段保持一致)

## MQTT 3.1.1 持久会话

### 工作原理

MQTT 3.1.1 使用 `clean session` 标志：
- `clean = false`: 持久会话（订阅在断开后保留）
- `clean = true`: 清洁会话（订阅在断开后清除）

### 行为

当 MQTT 3.1.1 客户端以 `clean = false` 连接时：
1. Broker 自动使用配置的 `sessionExpiryInterval` 作为会话过期间隔
2. 客户端断开后，订阅状态保留
3. 在 `sessionExpiryInterval` 时间内重连，订阅自动恢复
4. 超过 `sessionExpiryInterval` 后，订阅被清除

### 示例

```javascript
const client = mqtt.connect({
    host: 'localhost',
    port: 1883,
    protocolVersion: 4,  // MQTT 3.1.1
    clientId: 'persistent-client',
    clean: false  // 持久会话
});

// 订阅主题
client.subscribe('topic/test');

// 断开连接（订阅保留 180 秒）
client.end();

// 180 秒内重连（订阅自动恢复）
const reconnected = mqtt.connect({
    host: 'localhost',
    port: 1883,
    protocolVersion: 4,
    clientId: 'persistent-client',
    clean: false
});
```

## MQTT 5 持久会话

### 工作原理

MQTT 5 使用 `Session Expiry Interval` 属性替代 `clean session`：
- `Session Expiry Interval > 0`: 持久会话（指定秒数）
- `Session Expiry Interval = 0`: 会话在断开时结束

### 行为优先级

1. **客户端指定属性**: 如果 MQTT 5 客户端在 CONNECT 中包含 `Session Expiry Interval` 属性，使用客户端指定的值
2. **Clean Session = 0**: 如果客户端没有指定属性但 `clean = false`，使用 Broker 的默认值
3. **Clean Session = 1**: 如果 `clean = true`，会话在断开时立即结束

### 示例

```javascript
// 情况 1: 客户端指定 Session Expiry Interval
const client1 = mqtt.connect({
    host: 'localhost',
    port: 1883,
    protocolVersion: 5,
    clientId: 'client-with-expiry',
    clean: false,
    properties: {
        sessionExpiryInterval: 3600  // 1 小时
    }
});
// 使用的会话过期间隔: 3600 秒（客户端指定）

// 情况 2: 客户端未指定，使用默认值
const client2 = mqtt.connect({
    host: 'localhost',
    port: 1883,
    protocolVersion: 5,
    clientId: 'client-without-expiry',
    clean: false
});
// 使用的会话过期间隔: 180 秒（Broker 默认值）

// 情况 3: Clean = true
const client3 = mqtt.connect({
    host: 'localhost',
    port: 1883,
    protocolVersion: 5,
    clientId: 'clean-client',
    clean: true
});
// 使用的会话过期间隔: 0（会话在断开时结束）
```

## 配置选项

### BrokerOptions.default_session_expiry_interval

**说明**: Broker 默认会话过期间隔（秒）

**用途**: 当客户端未指定 Session Expiry Interval 时使用此默认值
- **MQTT 3.1.1**: 客户端设置 `clean = false` 时使用此值
- **MQTT 5**: 客户端未设置 Session Expiry Interval 属性且 `clean = false` 时使用此值

**与客户端指定的区别**:
- 客户端通过 MQTT 5 properties 指定的 `sessionExpiryInterval` 只对当前连接有效
- `default_session_expiry_interval` 是 Broker 的配置值，作为后备默认值

**示例**:
```javascript
// 客户端指定 - 只对当前连接有效
const client1 = mqtt.connect({
    protocolVersion: 5,
    properties: {
        sessionExpiryInterval: 3600  // 客户端指定，优先使用
    }
});

// 客户端未指定 - 使用 Broker 默认值
const client2 = mqtt.connect({
    protocolVersion: 4,
    clean: false  // 使用 Broker 的 default_session_expiry_interval
});
```

```c
#include "wolfmqtt/mqtt_broker.h"

MqttBroker broker;

// 使用简化的 Init API（自动初始化默认网络层）
MqttBroker_Init(&broker);

// 设置自定义默认会话过期间隔
BrokerOptions options = BROKER_OPTIONS_DEFAULTS;
options.default_session_expiry_interval = 600;  // 10 分钟

MqttBroker_SetOptions(&broker, &options);
MqttBroker_Start(&broker);
```

### 可选值

- `0`: 会话在断开时立即结束（等同于 `clean = true`）
- `1 - 4294967295`: 会话保留指定的秒数
- **推荐值**:
  - 短期: 60-300 秒（1-5 分钟）
  - 中期: 600-1800 秒（10-30 分钟）
  - 长期: 3600+ 秒（1 小时以上）

## 会话状态管理

### 订阅状态

| 事件 | 行为 |
|------|------|
| 客户端断开（持久会话） | 订阅的 `client` 指针设为 NULL，但保留订阅记录 |
| 客户端重连（会话未过期） | 订阅的 `client` 指针恢复到新连接 |
| 会话过期 | 删除所有孤立订阅 |
| Clean Session = 1 | 立即删除所有订阅 |

### 内存占用

对于每个持久会话的订阅：
- **静态内存**: `sizeof(BrokerSub)` 字节
- **动态内存**: 主题字符串 + 客户端 ID 字符串

示例：
```
10 个持久客户端 × 5 个订阅/客户端 = 50 个订阅
每个订阅约 100 字节（静态模式）
总内存: 50 × 100 = 5KB
```

## 限制和注意事项

### 限制

1. **离线消息**: Broker 不存储 QoS 1/2 离线消息
   - 持久会话只保留订阅状态
   - 客户端离线期间的消息会丢失
   - 使用 Retain 消息获取最新状态

2. **内存限制**: 静态内存模式有固定限制
   - `BROKER_MAX_SUBS`: 最大订阅数（默认 32）
   - `BROKER_MAX_CLIENTS`: 最大客户端数（默认 8）

3. **会话过期检查**: 每秒执行一次
   - 精度约为 ±1 秒
   - 短暂的会话过期间隔可能不准确

### 最佳实践

1. **合理设置过期间隔**
   - 移动设备: 60-300 秒（网络不稳定）
   - IoT 设备: 300-1800 秒（省电模式）
   - 固定设备: 3600+ 秒（稳定连接）

2. **使用 Retain 消息补充**
   ```javascript
   // 发布者
   client.publish('status', JSON.stringify({value: 42}), {retain: true});

   // 订阅者（持久会话 + Retain）
   client.subscribe('status');
   // 重连后立即收到最新状态
   ```

3. **监控会话过期**
   ```c
   // Broker 日志会显示会话过期事件
   // "broker: session expired client_id=xxx filter=xxx"
   ```

## 故障排查

### 问题: 重连后订阅丢失

**可能原因**:
1. 超过了 `sessionExpiryInterval`
2. 使用了不同的 `clientId`
3. 连接时设置了 `clean = true`

**解决方案**:
1. 增加 `sessionExpiryInterval` 值
2. 确保使用相同的 `clientId`
3. 确保连接时 `clean = false`（MQTT 3.1.1）或未设置 `Session Expiry Interval = 0`（MQTT 5）

### 问题: 订阅恢复但消息丢失

**原因**: Broker 不支持离线消息存储

**解决方案**:
1. 使用 Retain 消息获取最新状态
2. 客户端重连后主动查询状态
3. 确保客户端常连（使用心跳保活）

## 示例代码

完整示例请参考：
- `tests/test_persistent_session.c` - 持久会话测试
- `tests/test_session_expiry_default.c` - 默认会话过期间隔测试

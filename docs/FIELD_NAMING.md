# 字段命名说明：default_session_expiry_interval

## 命名规范

wolfMQTT Broker 的 `BrokerOptions` 结构采用**蛇形命名（snake_case）**规范：

```c
typedef struct BrokerOptions {
    word16 rx_buf_sz;              // 蛇形命名 ✓
    word16 tx_buf_sz;              // 蛇形命名 ✓
    word16 max_clients;            // 蛇形命名 ✓
    word16 max_subs;               // 蛇形命名 ✓
    word32 default_session_expiry_interval;  // 蛇形命名 ✓
} BrokerOptions;
```

## 字段含义

### `default_session_expiry_interval`

- **类型**: `word32` (32位无符号整数)
- **单位**: 秒
- **默认值**: `180` (3分钟)
- **用途**: 当客户端未指定会话过期间隔时使用的默认值

## 与客户端字段的关系

### MQTT 5 客户端

客户端通过 properties 指定的 `sessionExpiryInterval`（驼峰命名）：

```javascript
// JavaScript 客户端示例
const client = mqtt.connect({
    protocolVersion: 5,
    properties: {
        sessionExpiryInterval: 3600  // 客户端指定（驼峰）
    }
});
```

**优先级**: 客户端指定的值 > Broker 默认值

### MQTT 3.1.1 客户端

客户端使用 `clean` 标志：

```javascript
// JavaScript 客户端示例
const client = mqtt.connect({
    protocolVersion: 4,
    clean: false  // 使用 Broker 的 default_session_expiry_interval
});
```

## 代码示例

### C 语言（Broker 配置）

```c
#include "wolfmqtt/mqtt_broker.h"

MqttBroker broker;

// 使用简化的 Init API（自动初始化默认网络层）
MqttBroker_Init(&broker);

// 使用默认值（180秒）
BrokerOptions options = BROKER_OPTIONS_DEFAULTS;
MqttBroker_SetOptions(&broker, &options);

// 或自定义默认值
BrokerOptions custom_options = BROKER_OPTIONS_DEFAULTS;
custom_options.default_session_expiry_interval = 600;  // 10分钟
MqttBroker_SetOptions(&broker, &custom_options);

MqttBroker_Start(&broker);
```

### JavaScript（客户端）

```javascript
// 情况 1: MQTT 5 客户端指定（优先使用客户端值）
const client1 = mqtt.connect({
    host: 'localhost',
    port: 1883,
    protocolVersion: 5,
    clientId: 'client-with-expiry',
    properties: {
        sessionExpiryInterval: 3600  // 1小时（客户端指定）
    }
});
// 实际使用的会话过期间隔: 3600秒

// 情况 2: MQTT 5 客户端未指定（使用 Broker 默认值）
const client2 = mqtt.connect({
    host: 'localhost',
    port: 1883,
    protocolVersion: 5,
    clientId: 'client-without-expiry',
    clean: false
});
// 实际使用的会话过期间隔: 180秒（Broker 的 default_session_expiry_interval）

// 情况 3: MQTT 3.1.1 客户端 clean=0（使用 Broker 默认值）
const client3 = mqtt.connect({
    host: 'localhost',
    port: 1883,
    protocolVersion: 4,
    clientId: 'mqtt311-client',
    clean: false
});
// 实际使用的会话过期间隔: 180秒（Broker 的 default_session_expiry_interval）
```

## 字段对比表

| 位置 | 字段名 | 命名风格 | 作用域 |
|------|--------|---------|--------|
| BrokerOptions | `default_session_expiry_interval` | 蛇形 (snake_case) | Broker 配置，作为默认值 |
| MQTT 5 properties | `sessionExpiryInterval` | 驼峰 (camelCase) | 客户端指定，仅当前连接有效 |
| BrokerClient | `session_expiry_interval` | 蛇形 (snake_case) | 运行时存储，实际使用的值 |
| BrokerSub | `session_expiry_interval` | 蛇形 (snake_case) | 运行时存储，实际使用的值 |

## 命名规范总结

1. **BrokerOptions 配置字段**: 蛇形命名（snake_case）
   - 示例: `rx_buf_sz`, `max_clients`, `default_session_expiry_interval`

2. **客户端库（JavaScript/Python等）**: 驼峰命名（camelCase）
   - 示例: `sessionExpiryInterval`, `willDelayInterval`

3. **内部运行时结构**: 蛇形命名（snake_case）
   - 示例: `session_expiry_interval`, `disconnect_time`

## 修改历史

- **2025-04**: 将 `sessionExpiryInterval` 改为 `default_session_expiry_interval`
  - 原因: 统一 BrokerOptions 的命名风格
  - 影响: 所有引用该字段的代码需要更新

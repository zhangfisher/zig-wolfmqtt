# MQTT v5 Clean Start 和会话过期间隔支持

## 概述

wolfMQTT Broker 现已完整支持 MQTT v5 协议中的 **Clean Start** 和 **会话过期间隔 (Session Expiry Interval)** 特性，实现了符合 MQTT v5 规范的会话管理功能。

## 功能说明

### Clean Start (v5) / Clean Session (v3.1.1)

- **MQTT v3.1.1**: 使用 `clean_session` 标志
- **MQTT v5**: 使用 `clean_start` 标志
- **行为**:
  - `clean=1`: 新的会话，断开连接时清除所有订阅
  - `clean=0`: 持久会话，断开连接后保留订阅（直到会话过期）

### 会话过期间隔 (Session Expiry Interval)

- **属性类型**: `MQTT_PROP_SESSION_EXPIRY_INTERVAL` (值: 17)
- **数据类型**: 4字节整数 (秒)
- **默认值**: 0 (会话在断开连接时立即结束)
- **行为**:
  - `0`: 会话在断开连接时立即结束
  - `> 0`: 会话在断开连接后保留指定秒数

## 实现细节

### 数据结构修改

**1. BrokerClient 结构** ([mqtt_broker.h:238](wolfmqtt/mqtt_broker.h#L238)):
```c
#ifdef WOLFMQTT_V5
    word32  session_expiry_interval; /* v5 Session Expiry Interval (seconds) */
    WOLFMQTT_BROKER_TIME_T disconnect_time; /* When client disconnected */
#endif
```

**2. BrokerSub 结构** ([mqtt_broker.h:270](wolfmqtt/mqtt_broker.h#L270)):
```c
#ifdef WOLFMQTT_V5
    word32  session_expiry_interval; /* Session expiry interval in seconds */
    WOLFMQTT_BROKER_TIME_T disconnect_time; /* When client disconnected */
#endif
```

### 关键功能

#### 1. CONNECT 处理 ([mqtt_broker.c:2939](src/mqtt_broker.c#L2939))
- 从 MQTT v5 CONNECT 属性中提取会话过期间隔
- 存储在 `bc->session_expiry_interval`
- 调试输出:
  ```
  [BROKER-DEBUG] CONNECT: client_id=abc session_expiry_interval=3600 seconds
  ```

#### 2. 订阅时存储会话信息 ([mqtt_broker.c:1584](src/mqtt_broker.c#L1584))
- 在 `BrokerSubs_Add` 中，将客户端的会话过期间隔复制到订阅
- `sub->session_expiry_interval = bc->session_expiry_interval;`
- 活跃连接的 `disconnect_time = 0`

#### 3. 断开时记录时间戳 ([mqtt_broker.c:1456](src/mqtt_broker.c#L1456))
- 在 `BrokerSubs_OrphanClient` 中记录断开时间
- `sub->disconnect_time = now;`
- 调试输出:
  ```
  [BROKER-DEBUG] Orphaned subscriptions: client_id=abc count=2 session_expiry_interval=3600
  ```

#### 4. 会话过期检查 ([mqtt_broker.c:1495](src/mqtt_broker.c#L1495))
- 在 `MqttBroker_Step` 的每次迭代中调用 `BrokerSubs_CheckSessionExpiry`
- 检查所有孤儿订阅 (client == NULL)
- 计算经过时间: `elapsed = now - disconnect_time`
- 如果 `elapsed >= session_expiry_interval`，删除订阅
- 调试输出:
  ```
  [BROKER-DEBUG] Session expired: client_id=abc filter=sensor/# disconnect_time=123456 now=124056 interval=600 elapsed=600
  ```

## 使用示例

### 场景 1: 立即清理 (clean=1 或 session_expiry_interval=0)

```bash
# 客户端连接
mosquitto_sub -h localhost -t 'sensor/#' -c -i test_client

# Broker 输出:
# broker: CONNECT proto=5 clean=1 will=0 client_id=test_client ip=127.0.0.1
# [BROKER-DEBUG] CONNECT: client_id=test_client session_expiry_interval=0 seconds

# 客户端断开连接
# broker: orphaned 1 subs for client_id=test_client (session persist)

# 下次 MqttBroker_Step 调用:
# broker: session ended (clean) client_id=test_client filter=sensor/#
```

### 场景 2: 持久会话 (clean=0, session_expiry_interval=3600)

```bash
# 客户端连接（会话保留1小时）
mosquitto_sub -h localhost -t 'sensor/#' -i test_client \
    -D CONNECT session-expiry-interval 3600

# Broker 输出:
# broker: CONNECT proto=5 clean=0 will=0 client_id=test_client ip=127.0.0.1
# [BROKER-DEBUG] CONNECT: client_id=test_client session_expiry_interval=3600 seconds

# 客户端断开连接
# broker: orphaned 1 subs for client_id=test_client (session persist)
# [BROKER-DEBUG] Orphaned subscriptions: client_id=test_client count=1 session_expiry_interval=3600

# 订阅保留1小时，在此期间发布的消息会累积
# 1小时后:
# broker: session expired client_id=test_client filter=sensor/#
```

### 场景 3: 重连 (会话未过期)

```bash
# 客户端A连接并订阅
mosquitto_sub -h localhost -t 'sensor/#' -i test_client \
    -D CONNECT session-expiry-interval 3600
# 订阅: sensor/#

# 客户端A断开

# 在过期前，其他客户端发布消息
mosquitto_pub -h localhost -t 'sensor/temperature' -m '25.5'

# 客户端A重连（相同 client_id）
mosquitto_sub -h localhost -t 'sensor/#' -i test_client \
    -D CONNECT session-expiry-interval 3600

# Broker 输出:
# broker: duplicate client_id=test_client, disconnecting old sock=5
# broker: CONNECT proto=5 clean=0 will=0 client_id=test_client ip=127.0.0.1

# 客户端A会立即收到重连期间发布的消息
```

## 调试输出详解

### 启用调试模式

```bash
# 构建时启用调试
zig build -Dbuild-only=broker -Dbroker-debug=true

# 运行 broker
./zig-out/broker/arm/linux/gnueabihf/mqtt_broker
```

### 调试信息格式

**1. CONNECT 时:**
```
[BROKER-DEBUG] CONNECT: client_id=test_client session_expiry_interval=3600 seconds
```
- `client_id`: 客户端标识符
- `session_expiry_interval`: 会话过期间隔（秒）

**2. 断开连接时:**
```
[BROKER-DEBUG] Orphaned subscriptions: client_id=test_client count=2 session_expiry_interval=3600
```
- `count`: 孤儿化的订阅数量
- `session_expiry_interval`: 使用的会话过期间隔

**3. 会话过期时:**
```
[BROKER-DEBUG] Session expired: client_id=test_client filter=sensor/# disconnect_time=1713789600 now=1713793200 interval=600 elapsed=600
```
- `filter`: 过期的主题过滤器
- `disconnect_time`: 断开连接的时间戳
- `now`: 当前时间戳
- `interval`: 会话过期间隔
- `elapsed`: 实际经过时间

## 构建选项

```bash
# 默认构建（包含 MQTT v5 支持）
zig build -Dbuild-only=broker -Dtarget=arm-linux-gnueabihf -Dstatic-link=true --release=small

# 启用详细调试
zig build -Dbuild-only=broker -Dtarget=arm-linux-gnueabihf -Dstatic-link=true --release=small -Dbroker-debug=true
```

## 与 MQTT v3.1.1 的兼容性

| 特性 | MQTT v3.1.1 | MQTT v5 |
|------|------------|---------|
| 标志名称 | `clean_session` | `clean_start` |
| 会话过期间隔 | 无 | 有 (Session Expiry Interval) |
| 默认行为 | `clean=0`: 会话永久保留 | `clean=0`: 默认 `session_expiry_interval=0` (立即过期) |

**注意**: MQTT v5 的默认行为更安全。如果客户端未设置会话过期间隔，broker 会在断开连接时立即清理会话。

## 性能考虑

- **内存**: 每个订阅额外 12 字节 (session_expiry_interval + disconnect_time)
- **CPU**: 每次 `MqttBroker_Step` 调用增加 O(n) 复杂度，n 为孤儿订阅数量
- **建议**: 对于高吞吐场景，建议设置合理的会话过期间隔，避免订阅无限累积

## 测试

### 测试用例 1: 立即清理
```bash
# 1. 启动 broker
./mqtt_broker

# 2. 连接客户端（立即清理）
mosquitto_sub -h localhost -t 'test/#' -c -i client1

# 3. 验证订阅在断开后立即删除（检查 broker 日志）
```

### 测试用例 2: 会话保留
```bash
# 1. 连接客户端（会话保留10秒）
mosquitto_sub -h localhost -t 'test/#' -i client2 \
    -D CONNECT session-expiry-interval 10

# 2. 断开连接

# 3. 在10秒内发布消息
mosquitto_pub -h localhost -t 'test/msg' -m 'hello'

# 4. 验证消息被保留

# 5. 等待10秒后，验证订阅被删除（检查 broker 日志）
```

### 测试用例 3: 重连
```bash
# 1. 连接客户端
mosquitto_sub -h localhost -t 'test/#' -i client3 \
    -D CONNECT session-expiry-interval 60

# 2. 断开连接

# 3. 发布消息
mosquitto_pub -h localhost -t 'test/msg' -m 'hello'

# 4. 重连（相同 client_id）
mosquitto_sub -h localhost -t 'test/#' -i client3

# 5. 验证客户端收到重连期间的消息
```

## 参考文档

- [MQTT v5.0 规范](https://docs.oasis-open.org/mqtt/mqtt/v5.0/mqtt-v5.0.html)
- [MQTT v5 Clean Start](https://docs.oasis-open.org/mqtt/mqtt/v5.0/mqtt-v5.0.html#_Toc3901207)
- [MQTT v5 Session Expiry Interval](https://docs.oasis-open.org/mqtt/mqtt/v5.0/mqtt-v5.0.html#_Toc3901210)

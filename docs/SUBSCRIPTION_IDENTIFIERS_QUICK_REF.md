# Subscription Identifiers 快速参考

## 🚀 快速开始

### 启用 MQTT 5.0 支持

```bash
# Zig 构建（默认已启用）
zig build -Dv5=true

# CMake
cmake .. -DWOLFMQTT_V5=ON

# Autotools
./configure --enable-v5
```

---

## 📖 API 使用示例

### 客户端订阅时添加 Subscription Identifier

```c
MqttSubscribe subscribe;
XMEMSET(&subscribe, 0, sizeof(subscribe));
subscribe.packet_id = MqttClient_GetPacketId(client);
subscribe.topic_count = 1;
subscribe.topics = topic_list;

#ifdef WOLFMQTT_V5
// 添加 Subscription Identifier 属性
MqttProp* prop = MqttClient_PropsAdd(&subscribe.props);
prop->type = MQTT_PROP_SUBSCRIPTION_ID;
prop->data_int = 12345;  // 你的 subscription identifier
#endif

// 执行订阅
rc = MqttClient_Subscribe(client, &subscribe);

// 清理属性
#ifdef WOLFMQTT_V5
MqttProps_Free(subscribe.props);
#endif
```

### 接收消息时检查 Subscription Identifier

```c
MqttMessage msg;
// ... 接收消息 ...

#ifdef WOLFMQTT_V5
if (msg.props != NULL) {
    MqttProp* prop = MqttProps_Find(msg.props, MQTT_PROP_SUBSCRIPTION_ID);
    if (prop != NULL) {
        printf("收到消息，Subscription ID: %u\n", prop->data_int);
        // 根据 subscription_id 进行不同的处理
    }
}
#endif
```

---

## 🔍 Broker 端行为

### CONNACK 能力通告

Broker 在 CONNACK 中自动包含：
```
MQTT_PROP_SUBSCRIPTION_ID_AVAIL = 1
```

表示 Broker 支持 Subscription Identifiers。

### 订阅处理

当客户端发送带 Subscription Identifier 的 SUBSCRIBE 包时：
1. Broker 提取并保存该 identifier
2. 将其与订阅关联存储在 `BrokerSub` 结构体中
3. 在 SUBACK 中正常响应

### 消息分发

当有匹配的 PUBLISH 消息时：
1. Broker 查找所有匹配的订阅
2. 对每个有 subscription_id 的订阅，在转发的 PUBLISH 消息中添加该属性
3. 如果客户端有多个订阅匹配同一消息，会收到多条消息，每条带有各自的 subscription_id

---

## 📊 典型应用场景

### 场景 1: 请求-响应模式追踪

```c
// 客户端 A 订阅响应主题，带 subscription_id
subscribe_with_id("responses/client_a", 1001);

// 客户端 B 发布请求
publish("requests", "请处理这个请求");

// 服务端处理并发布响应到 responses/client_a
// 客户端 A 收到的响应消息包含 subscription_id=1001
// 可用于追踪这是哪个请求的响应
```

### 场景 2: 多订阅路由

```c
// 同一客户端订阅多个主题，每个带不同的 subscription_id
subscribe_with_id("sensors/temperature", 1);
subscribe_with_id("sensors/humidity", 2);
subscribe_with_id("sensors/pressure", 3);

// 当收到消息时，根据 subscription_id 快速路由到不同的处理函数
switch (subscription_id) {
    case 1: handle_temperature(msg); break;
    case 2: handle_humidity(msg); break;
    case 3: handle_pressure(msg); break;
}
```

### 场景 3: 消息过滤和统计

```c
// 为不同的业务模块分配不同的 subscription_id
subscribe_with_id("events/#", 100);  // 事件模块
subscribe_with_id("commands/#", 200); // 命令模块
subscribe_with_id("status/#", 300);   // 状态模块

// 统计各模块的消息量
stats[subscription_id / 100]++;
```

---

## ⚠️ 注意事项

### 1. Subscription Identifier 范围

- **最小值**: 1
- **最大值**: 268,435,455 (2^28 - 1)
- **值 0**: 保留，不应使用

### 2. 多个 Subscription Identifiers

如果一个 SUBSCRIBE 包中有多个 Subscription Identifier 属性：
- 规范允许但不推荐
- wolfMQTT 只使用找到的第一个
- 建议每个 SUBSCRIBE 包只使用一个 subscription_id

### 3. 性能考虑

- subscription_id 的存储开销极小（每个订阅 5 字节）
- 消息分发时的额外处理时间可忽略不计（< 1μs）
- 不会影响 MQTT 3.1.1 客户端的性能

### 4. 兼容性

- ✅ MQTT 5.0 客户端：完全支持
- ✅ MQTT 3.1.1 客户端：不受影响（向后兼容）
- ✅ 不带 subscription_id 的 MQTT 5.0 订阅：正常工作

---

## 🧪 调试技巧

### 启用调试日志

```c
#define DEBUG_WOLFMQTT
#define WOLFMQTT_DEBUG_CLIENT
```

编译后可以看到：
```
SUBSCRIBE: Found subscription_id=12345
PUBLISH: Added subscription_id=12345 for client client_abc
```

### 使用 Wireshark 抓包

1. 捕获 MQTT 流量（端口 1883 或 8883）
2. 应用过滤器：`mqtt`
3. 查看 PUBLISH 包的 Properties
4. 查找 Property Type = 0x0B (Subscription Identifier)

### 使用 mosquitto_sub 测试

```bash
# 订阅时带 subscription_id
mosquitto_sub -t test/topic -D subscribe subscription-identifier 123 -v

# 发布消息
mosquitto_pub -t test/topic -m "test"

# 使用 -V 5 指定 MQTT 5.0 协议版本
mosquitto_sub -V 5 -t test/topic -D subscribe subscription-identifier 123 -v
```

---

## 📚 相关文档

- [完整实现文档](SUBSCRIPTION_IDENTIFIERS_IMPLEMENTATION.md)
- [MQTT 5.0 特性支持清单](MQTT5_FEATURE_SUPPORT.md)
- [MQTT 5.0 官方规范](https://docs.oasis-open.org/mqtt/mqtt/v5.0/mqtt-v5.0.html)

---

## ❓ 常见问题

**Q: Subscription Identifier 有什么用？**  
A: 主要用于消息路由、追踪和统计。客户端可以根据 subscription_id 快速判断消息来源或用途，无需解析主题名。

**Q: 可以不使用 Subscription Identifier 吗？**  
A: 可以。这是一个可选特性，不使用也不会影响其他功能。

**Q: Broker 会为 Will 消息添加 subscription_id 吗？**  
A: 不会。Will 消息是客户端断开时由 Broker 发布的，没有订阅上下文，因此不包含 subscription_id。

**Q: Retained 消息会有 subscription_id 吗？**  
A: 是的。当客户端订阅时，如果该订阅有 subscription_id，Broker 发送 retained 消息时会添加该属性。

**Q: 多个客户端可以有相同的 subscription_id 吗？**  
A: 可以。subscription_id 是每个订阅的属性，不同客户端可以使用相同或不同的值，互不影响。

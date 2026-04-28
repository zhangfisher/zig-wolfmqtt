# MQTT Broker 调试模式使用指南

## 概述

`WOLFMQTT_BROKER_DEBUG` 宏开关提供了详细的调试信息，帮助开发者和运维人员监控 broker 的消息流转情况。

## 构建选项

### 默认构建（不启用调试）
```bash
zig build -Dbuild-only=broker -Dtarget=arm-linux-gnueabihf -Dstatic-link=true --release=small
```
**文件大小**: ~79K

### 启用调试模式构建
```bash
zig build -Dbuild-only=broker -Dtarget=arm-linux-gnueabihf -Dstatic-link=true --release=small -Dbroker-debug=true
```
**文件大小**: ~80K

## 调试输出内容

### 1. 接收消息 (PUBLISH received)

当客户端发布消息时，输出：

**可打印字符负载**:
```
[BROKER-DEBUG] PUBLISH received from client=mqttx_abc123 ip=192.168.1.100
[BROKER-DEBUG]   topic=sensor/temperature, qos=1, retain=0
[BROKER-DEBUG]   payload="Hello World" (11 bytes)
```

**二进制/十六进制负载**:
```
[BROKER-DEBUG] PUBLISH received from client=device_001 ip=192.168.1.100
[BROKER-DEBUG]   topic=sensor/data, qos=1, retain=0
[BROKER-DEBUG]   payload=hex: 7b2254656d70223a32352e377d (15 bytes)
```

**说明**:
- `client`: 客户端 ID
- `ip`: 客户端 IP 地址
- `topic`: 消息主题
- `qos`: 服务质量等级 (0/1/2)
- `retain`: 是否为保留消息 (0/1)
- `payload`: 负载内容
  - 如果是可打印 ASCII 字符 (32-126)，直接显示字符串
  - 如果包含二进制数据，显示为 `hex: xxxx` 格式
  - 最多显示 128 字节，超出显示 `...`
  - 括号内显示实际字节数

### 2. 转发消息 (PUBLISH forward)

当将消息转发给订阅者时，输出：

```
[BROKER-DEBUG] PUBLISH forward: topic=sensor/temperature -> subscriber=mqttx_subscriber ip=192.168.1.101, bytes=85
```

**说明**:
- `topic`: 消息主题
- `subscriber`: 订阅者客户端 ID
- `ip`: 订阅者 IP 地址
- `bytes`: 转发的消息大小（字节数）

### 3. 订阅消息 (SUBSCRIBE)

当客户端订阅主题时，输出：

```
[BROKER-DEBUG] SUBSCRIBE: client=mqttx_subscriber ip=192.168.1.101 topic=sensor/# qos=1
[BROKER-DEBUG] SUBSCRIBE: FAILED (rc=-1)  // 如果订阅失败
```

**说明**:
- `client`: 客户端 ID
- `ip`: 客户端 IP 地址
- `topic`: 订阅的主题过滤器
- `qos`: 请求的 QoS 等级
- 如果订阅失败，会显示错误码

### 4. 退订消息 (UNSUBSCRIBE)

当客户端退订主题时，输出：

```
[BROKER-DEBUG] UNSUBSCRIBE: client=mqttx_subscriber ip=192.168.1.101 topic=sensor/temperature
```

**说明**:
- `client`: 客户端 ID
- `ip`: 客户端 IP 地址
- `topic`: 退订的主题过滤器

## 使用场景

### 场景 1: 调试消息路由问题

当消息没有到达预期的订阅者时，使用调试模式查看：

```bash
# 1. 启动调试版 broker
./mqtt_broker

# 2. 客户端 A 发布消息
# 输出: [BROKER-DEBUG] PUBLISH received from client=client_a ...

# 3. 检查是否转发给客户端 B
# 输出: [BROKER-DEBUG] PUBLISH forward: topic=test -> subscriber=client_b ...
```

### 场景 2: 验证订阅状态

```bash
# 客户端订阅时
# 输出: [BROKER-DEBUG] SUBSCRIBE: client=client1 ip=10.0.0.1 topic=sensor/#
```

### 场景 3: 负载内容检查

```bash
# 可打印文本消息
# 输出: [BROKER-DEBUG] payload="Hello World" (11 bytes)

# 二进制/十六进制消息
# 输出: [BROKER-DEBUG] payload=hex: 48656c6c6f20576f726c6421 (12 bytes)
```

## 输出示例

### 文本消息示例

```
# 客户端 A 发布文本消息
[BROKER-DEBUG] PUBLISH received from client=device_001 ip=192.168.1.100
[BROKER-DEBUG]   topic=sensor/alert, qos=1, retain=0
[BROKER-DEBUG]   payload="Temperature high" (17 bytes)

# 转发给订阅者
[BROKER-DEBUG] PUBLISH forward: topic=sensor/alert -> subscriber=monitor_app ip=192.168.1.201, bytes=92
```

### 二进制消息示例

```
# 客户端 B 发布二进制数据
[BROKER-DEBUG] PUBLISH received from client=sensor_001 ip=192.168.1.100
[BROKER-DEBUG]   topic=sensor/data, qos=1, retain=0
[BROKER-DEBUG]   payload=hex: 7b2254656d70223a32352e377d (15 bytes)

# 转发给订阅者
[BROKER-DEBUG] PUBLISH forward: topic=sensor/data -> subscriber=data_collector ip=192.168.1.200, bytes=92
```

### 订阅管理示例

```
# 新客户端订阅
[BROKER-DEBUG] SUBSCRIBE: client=new_subscriber ip=192.168.1.150 topic=sensor/# qos=1

# 客户端退订
[BROKER-DEBUG] UNSUBSCRIBE: client=new_subscriber ip=192.168.1.150 topic=sensor/temperature
```

## 性能影响

| 版本 | 文件大小 | 性能影响 |
|------|---------|---------|
| Release（默认） | ~79K | 无影响 |
| Debug（调试） | ~80K | 每条消息约增加 1-2% CPU 开销 |

**建议**:
- 开发和测试环境：使用 Debug 版本
- 生产环境：使用 Release 版本

## 部署建议

### 开发环境
```bash
# 构建调试版本
zig build -Dbuild-only=broker -Dtarget=arm-linux-gnueabihf -Dstatic-link=true --release=small -Dbroker-debug=true

# 部署到 ARM 设备
scp zig-out/broker/arm/linux/gnueabihf/mqtt_broker user@dev-device:/opt/mqtt/
```

### 生产环境
```bash
# 构建发布版本
zig build -Dbuild-only=broker -Dtarget=arm-linux-gnueabihf -Dstatic-link=true --release=small

# 部署到 ARM 设备
scp zig-out/broker/arm/linux/gnueabihf/mqtt_broker user@prod-device:/opt/mqtt/
```

## 与日志级别的关系

- **WBLOG_DBG/WBLOG_INFO/WBLOG_ERR**: 正常的 broker 日志（始终可用）
- **WOLFMQTT_BROKER_DEBUG**: 额外的详细调试信息（需要启用）

两者可以同时使用，提供不同层次的观测能力。

## 注意事项

1. **Payload 限制**: 十六进制输出最多显示 128 字节，大型消息会被截断
2. **性能**: 调试模式会略微增加 CPU 使用，建议仅在需要时启用
3. **文件大小**: 调试版本会略微增大二进制文件大小
4. **敏感数据**: 调试输出可能包含敏感的负载数据，请谨慎使用

## 故障排查

### 问题：没有看到调试输出

**检查清单**:
1. 确认使用了 `-Dbroker-debug=true` 构建
2. 检查 broker 是否正常运行
3. 验证客户端是否实际发送/订阅了消息

### 问题：输出格式不正确

**可能原因**:
1. 终端不支持 UTF-8 或颜色输出
2. 使用管道重定向时字符编码问题

**解决方案**:
```bash
# 禁用颜色输出
./mqtt_broker 2>&1 | cat

# 或直接查看日志文件
./mqtt_broker > broker.log 2>&1
```

## 高级用法

### 与 tcpdump 结合使用

```bash
# 终端 1: 运行调试模式 broker
./mqtt_broker

# 终端 2: 监控网络流量
tcpdump -i eth0 port 1883 -A -s 0 -w 'tcp[20:4] != 0'

# 对比 broker 日志和网络包，验证消息完整性
```

### 与 MQTT 客户端工具结合

```bash
# 终端 1: 调试模式 broker
./mqtt_broker

# 终端 2: 使用 mosquitto_sub 订阅
mosquitto_sub -h localhost -t 'sensor/#' -v

# 终端 3: 使用 mosquitto_pub 发布
mosquitto_pub -h localhost -t 'sensor/data' -m '{"temp": 25}'

# 观察完整的消息流和路由
```

## 总结

`WOLFMQTT_BROKER_DEBUG` 提供了 broker 内部的详细可见性，帮助：
- ✅ 验证消息是否正确接收和解析
- ✅ 确认消息是否转发给正确的订阅者
- ✅ 监控客户端的订阅/退订活动
- ✅ 检查消息负载的实际内容
- ✅ 调试消息路由问题

这对于开发和测试 MQTT 应用非常有用！

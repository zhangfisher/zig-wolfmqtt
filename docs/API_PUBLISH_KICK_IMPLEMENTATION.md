# Broker HTTP API - Publish and Kick 功能实现

## 概述

本次更新实现了Broker HTTP API中的两个核心功能：
1. **POST /api/publish/<topic>** - 通过HTTP API发布MQTT消息
2. **POST /api/kick/<client_id_or_ip>** - 踢掉指定的MQTT客户端

**注意**：所有API端点都使用 `/api` 前缀，例如 `/api/stats`, `/api/clients`, `/api/configs` 等。

## URL端点列表

### GET 请求
- `/api/stats` - 获取broker统计信息
- `/api/clients` - 获取已连接客户列表
- `/api/topics/<topic>` - 返回主题的订阅者列表、retain信息等
- `/api/configs` - 返回MqttBrokerOptions配置参数（原 `/options`）

### POST 请求
- `/api/publish/<topic>?qos=<n>&retain=<true|false>` - 发布信息到指定主题
- `/api/config/<配置项名称>` - 更新配置数据
- `/api/reset` - 重置broker
- `/api/kick/<客户端ip或clientId>` - 踢掉指定客户端

## 实现细节

### 1. 新增的公共API函数

在 `mqtt_broker.c` 中添加了两个非static的公共函数：

#### BrokerPublish_Message()
```c
int BrokerPublish_Message(MqttBroker* broker, const char* topic,
                          const byte* payload, word16 payload_len,
                          MqttQoS qos, byte retain);
```

**功能**：
- 将消息发布到所有匹配的订阅者
- 支持QoS 0/1/2
- 支持retain标志（如果启用）
- 自动处理静态内存和动态内存模式
- 更新broker统计信息

**实现逻辑**：
1. 遍历所有订阅（subs）
2. 使用 `BrokerTopicMatch()` 匹配主题
3. 为每个匹配的订阅者编码并发送PUBLISH包
4. 如果需要，存储retained消息

#### BrokerKick_Client()
```c
int BrokerKick_Client(MqttBroker* broker, const char* client_identifier);
```

**功能**：
- 根据client_id或IP地址断开指定客户端
- 发布will消息（如果存在）
- 清理订阅关系
- 从broker中移除客户端

**返回值**：
- `MQTT_CODE_SUCCESS` - 成功踢掉客户端
- `MQTT_CODE_ERROR_NOT_FOUND` - 未找到指定客户端
- `MQTT_CODE_ERROR_BAD_ARG` - 参数无效

### 2. HTTP API端点实现

#### POST /api/publish/<topic>?qos=<n>&retain=<true|false>

**请求示例**：
```bash
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -H "Content-Type: text/plain" \
  -d "Hello from HTTP API!" \
  "http://localhost:8081/api/publish/test/topic?qos=1&retain=false"
```

**响应示例**：
```json
{
  "status": "success",
  "message": "Published to topic 'test/topic'"
}
```

**参数说明**：
- `topic` - MQTT主题（URL路径中）
- `qos` - QoS级别（0, 1, 或 2），默认0
- `retain` - 是否保留消息（true/false），默认false
- `body` - 消息payload（HTTP请求体）

**实现流程**：
1. 解析URL提取topic和查询参数
2. 解析qos和retain参数
3. 修剪payload的前后空白字符
4. 调用 `BrokerPublish_Message()` 发布消息
5. 返回JSON响应

#### POST /api/kick/<client_id_or_ip>

**请求示例**：
```bash
# 通过client_id踢掉客户端
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  "http://localhost:8081/api/kick/client123"

# 通过IP地址踢掉客户端
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  "http://localhost:8081/api/kick/192.168.1.100"
```

**成功响应**：
```json
{
  "status": "success",
  "message": "Client 'client123' kicked"
}
```

**失败响应（客户端不存在）**：
```json
{
  "error": "Client 'client123' not found"
}
```

**实现流程**：
1. 解析URL提取client_identifier
2. 遍历所有连接的客户端
3. 匹配client_id或IP地址
4. 如果找到：
   - 发布will消息（如果存在）
   - 清理订阅关系
   - 移除客户端
5. 返回相应的JSON响应

## 编译和测试

### 编译带API功能的Broker

**注意**：HTTP API功能现在默认启用，无需额外的编译参数。

```bash
# 标准编译（API功能自动包含）
zig build broker -Dstatic-link=true -Dwebsocket=true --release=small

# 如果需要禁用API功能（可选）
zig build broker -Dstatic-link=true -Dwebsocket=true -Dbroker-api=false --release=small
```

**重要说明**：
- API功能默认启用，不需要 `-Dbroker-api=true` 参数
- 如果确实需要禁用API，可以使用 `-Dbroker-api=false`
- CMake构建系统也默认启用API（WOLFMQTT_BROKER_API=yes）
- Autotools构建系统也默认启用API（--enable-broker-api为默认值）

### 启动Broker并启用API

```bash
./mqtt_broker-musleabihf -api-token "testtoken12345" -port 1883
```

或者禁用API认证（不推荐生产环境）：
```bash
./mqtt_broker-musleabihf -api-disable
```

### 运行测试脚本

```bash
chmod +x test_api.sh
./test_api.sh
```

## API特性

### 支持的HTTP方法
- GET: /stats, /clients, /topics, /options
- POST: /publish/<topic>, /config/<item>, /reset, /kick/<client>

### 认证
- 使用HTTP Basic Authentication
- Token长度至少12个字符
- 通过 `-api-token` 命令行参数配置
- 未认证请求返回401 Unauthorized

### 日志系统

#### API启动/停止日志
API服务器启动和停止时会输出INFO级别的日志：
```
[INFO ] 1970-01-22 05:00:36 - listening on port 8081 (HttpApi)
[INFO ] 1970-01-22 05:00:36 - HTTP API server stopped
```

#### 请求日志
每个API请求都会输出DEBUG级别的日志，包含：
- HTTP方法（GET/POST）
- URL路径
- 查询参数
- Socket描述符

示例：
```
[DEBUG] 1970-01-22 05:00:37 - API Request: method=GET url=/api/stats sock=5
[DEBUG] 1970-01-22 05:00:37 - API Response: status=200 body_len=120 total_sent=120
```

#### 响应日志
每个API响应都会输出DEBUG级别的日志，包含：
- HTTP状态码
- Body长度
- 总发送字节数

**注意**：DEBUG级别日志需要启用调试模式才能看到。

### 日志
- 使用与broker相同的日志系统
- API操作会记录到broker日志
- 可通过运行时选项控制日志级别

## 技术要点

### 1. 内存管理
- 同时支持 `WOLFMQTT_STATIC_MEMORY` 和动态内存模式
- 在两种模式下都能正确工作

### 2. 主题匹配
- 使用broker内部的 `BrokerTopicMatch()` 函数
- 支持通配符主题匹配（+ 和 #）

### 3. QoS处理
- 正确处理QoS降级（取发布者和订阅者的最小值）
- 为QoS 1/2消息分配packet ID

### 4. Retain消息
- 如果启用了 `WOLFMQTT_BROKER_RETAINED`，支持存储retain消息
- 不会将retain标志转发给订阅者（符合MQTT规范）

### 5. 客户端断开
- 完整清理客户端资源
- 正确处理clean_session标志
- 发布will消息（如果配置了）

## 限制和注意事项

1. **Publish功能**：
   - 当前实现是简化版本
   - 不支持共享订阅的轮询选择
   - 不支持No Local标志
   - 不支持属性转发（MQTT v5）

2. **Kick功能**：
   - 只能踢掉当前连接的客户端
   - 不会持久化黑名单
   - 被踢掉的客户端可以重新连接

3. **性能考虑**：
   - API处理在主循环中进行
   - 大量并发API请求可能影响broker性能
   - 建议在生产环境中限制API访问频率

## 未来改进方向

1. 实现完整的MQTT v5属性支持
2. 添加共享订阅的完整支持
3. 实现客户端黑名单机制
4. 添加API速率限制
5. 支持批量操作（批量发布、批量踢人）
6. 添加WebSocket API支持

## 相关文件

- `src/mqtt_broker.c` - 核心实现（BrokerPublish_Message, BrokerKick_Client）
- `src/mqtt_broker_api.c` - HTTP API处理
- `wolfmqtt/mqtt_broker.h` - API声明
- `test_api.sh` - 测试脚本

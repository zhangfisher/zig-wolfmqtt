# HTTP API 功能实现说明

## 概述

HTTP REST API 已成功集成到 MQTT Broker 中，提供以下功能：

### 已实现的功能

1. **统计信息查询** (`GET /api/stats`)
   - 当前连接数
   - 接收/发送消息数
   - 接收/发送字节数
   - 保留消息数
   - 订阅数
   - 运行时间

2. **客户端列表** (`GET /api/clients`)
   - 列出所有已连接的客户端
   - 显示客户端 ID 和 IP 地址

3. **主题订阅查询** (`GET /api/topics/<topic>`)
   - 查询特定主题的订阅者
   - 显示订阅的 QoS 级别

4. **配置查询** (`GET /api/configs`)
   - 查询当前 broker 配置
   - 包括端口、缓冲区大小、最大客户端数等

5. **消息发布** (`POST /api/publish/<topic>`)
   - 通过 HTTP 发布 MQTT 消息
   - 支持 QoS 0/1/2
   - 支持 retain 标志
   - 查询参数：`qos=<0-1-2>&retain=<true-false>`

6. **批量配置更新** (`POST /api/configs`)
   - 动态更新 broker 配置
   - URL 编码格式：`key1=value1&key2=value2`
   - 支持所有可配置的 broker 选项

7. **重置统计** (`POST /api/reset`)
   - 重置 broker 统计计数器

8. **踢出客户端** (`POST /api/kick/<client_id>`)
   - 通过 client_id 或 IP 地址断开客户端

## 实现详情

### 新增函数

在 `src/mqtt_broker.c` 中实现了两个 API 接口函数：

#### `BrokerPublish_Message()`
```c
int BrokerPublish_Message(MqttBroker* broker, const char* topic,
                         const byte* payload, word16 payload_len,
                         MqttQoS qos, byte retain)
```
- 从外部源（如 HTTP API）发布消息到 MQTT 订阅者
- 支持保留消息
- 自动处理 QoS 级别
- 实现主题匹配和消息转发

#### `BrokerKick_Client()`
```c
int BrokerKick_Client(MqttBroker* broker, const char* client_identifier)
```
- 通过 client_id 或 IP 地址断开客户端
- 清理订阅和资源
- 触发断开回调

### Broker 集成

在 `src/mqtt_broker.c` 中修改了以下函数：

1. **`MqttBroker_Start()`**
   - 初始化 HTTP API 监听器
   - 分配 API 上下文
   - 在指定端口（默认 8081）启动 HTTP 服务器

2. **`MqttBroker_Step()`**
   - 在每个事件循环中处理 HTTP API 请求
   - 接受新的 HTTP 连接
   - 调用 API 请求处理器

3. **`MqttBroker_Free()`**
   - 清理 HTTP API 资源
   - 关闭 API 监听器
   - 释放 API 上下文

### API 源文件

`src/mqtt_broker_api.c` 提供完整的 HTTP REST API 实现：
- HTTP 请求解析
- JSON 响应生成
- API Token 认证
- 错误处理
- 端点路由

## 配置选项

### 编译时选项

以下选项在构建时已自动启用（通过 `build/utils/options.zig`）：

```zig
broker_api: bool = true  // HTTP API 支持（默认启用）
```

### 运行时配置

HTTP API 由以下 broker 选项控制：

- `enable_api`: 启用/禁用 HTTP API（默认：1）
- `api_port`: HTTP API 端口（默认：8081）
- `api_token`: API 认证令牌（可选，默认：无认证）

## 使用示例

### 1. 启动 Broker

```bash
./mqtt_broker-musleabihf
# HTTP API 将自动在端口 8081 启动
```

### 2. 查询统计信息

```bash
curl http://localhost:8081/api/stats
```

响应：
```json
{
  "conns": 5,
  "rx_msgs": 1234,
  "tx_msgs": 5678,
  "rx_bytes": 123456,
  "tx_bytes": 789012,
  "retained": 12,
  "subs": 25,
  "uptime_seconds": 3600
}
```

### 3. 发布消息

```bash
curl -X POST http://localhost:8081/api/publish/test/topic \
  -H "Content-Type: application/json" \
  -d '{"message": "Hello from HTTP API"}'
```

带 QoS 和 retain：
```bash
curl -X POST "http://localhost:8081/api/publish/test/topic?qos=1&retain=true" \
  -d "Retained message"
```

### 4. 查询客户端列表

```bash
curl http://localhost:8081/api/clients
```

响应：
```json
[
  {"id": "client1", "ip": "192.168.1.100"},
  {"id": "client2", "ip": "192.168.1.101"}
]
```

### 5. 踢出客户端

```bash
curl -X POST http://localhost:8081/api/kick/client1
```

### 6. 更新配置

```bash
curl -X POST http://localhost:8081/api/configs \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "log_level=0&max_clients=100"
```

### 7. 重置统计

```bash
curl -X POST http://localhost:8081/api/reset
```

## API 认证

### 设置 API Token

在代码中设置（需要修改代码）：
```c
MqttBrokerApi_SetToken(broker->api_ctx, "your-secure-token-here");
```

### 使用 Token 认证

```bash
curl http://localhost:8081/api/stats \
  -H "Authorization: Basic your-secure-token-here"
```

## 测试

使用提供的测试脚本：
```bash
bash test_broker.sh
```

测试脚本会验证：
- MQTT 基本连接
- QoS 支持
- 保留消息
- 通配符订阅
- HTTP API 统计接口
- HTTP API 发布接口

## 故障排查

### HTTP API 没有启动

检查日志输出，应该看到：
```
[INFO] listening on port 1883 (no TLS)
[INFO] listening on port 8080 (WebSocket)
[INFO] listening on port 8081 (HTTP API)  <-- 这个必须出现
```

如果缺少 HTTP API 日志：
1. 检查编译时是否启用了 `WOLFMQTT_BROKER_API`
2. 检查 `broker->enable_api` 是否为 1
3. 检查 `broker->api_port` 是否有效

### 端口冲突

如果 8081 端口被占用，可以更改：
```c
broker->api_port = 8082;  // 使用其他端口
```

### API 请求失败

1. 检查 URL 路径是否正确（必须以 `/api/` 开头）
2. 检查 HTTP 方法（GET/POST）
3. 查看日志中的错误信息
4. 验证 JSON 格式（对于 POST 请求）

## 性能考虑

- HTTP API 使用非阻塞 I/O，不会阻塞 MQTT 事件循环
- 每个 HTTP 请求在处理后立即关闭连接
- API 认证使用简单的 Basic Auth，适合内网使用
- 对于高并发场景，建议使用反向代理（如 nginx）

## 安全建议

1. **生产环境**：始终设置 API Token
2. **网络隔离**：HTTP API 仅在内网访问
3. **TLS 加密**：使用反向代理提供 HTTPS
4. **速率限制**：配置反向代理限制请求速率
5. **日志审计**：定期检查 API 访问日志

## 技术细节

### HTTP 协议
- 支持 HTTP/1.1
- 每个请求后关闭连接（Connection: close）
- Content-Type: application/json

### JSON 格式
- 使用紧凑格式（无空格）
- 所有数字使用整数
- 字符串使用双引号

### 错误响应
- 400 Bad Request：无效的请求参数
- 401 Unauthorized：认证失败
- 404 Not Found：端点不存在
- 500 Internal Server Error：服务器内部错误

## 构建

```bash
# 标准构建（包含 HTTP API）
zig build broker -Dstatic-link=true --release=small

# 禁用 HTTP API（如果需要）
zig build broker -Dbroker-api=false --release=small
```

## 文件大小影响

启用 HTTP API 后：
- 无 HTTP API: ~110KB
- 有 HTTP API: ~120KB
- 增加: ~10KB

## 后续改进建议

1. **分页支持**：客户端列表使用分页
2. **WebSocket API**：提供实时事件推送
3. **批量操作**：支持批量发布消息
4. **CORS 支持**：跨域访问控制
5. **OpenAPI 规范**：自动生成 API 文档
6. **Prometheus 指标**：标准化监控指标

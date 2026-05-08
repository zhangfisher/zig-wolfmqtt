# HTTP API 快速验证指南

## 问题已修复

之前 HTTP API 没有启动的原因：`MqttBroker_InitEx()` 函数中没有初始化 `enable_api` 和 `api_port` 字段，导致它们的值都是 0，所以启动条件不满足。

## 修复内容

### 1. 添加 API 端口常量 (`mqtt_socket.h`)
```c
#define MQTT_API_PORT       8081    /* HTTP API default port */
```

### 2. 初始化 HTTP API (`mqtt_broker.c` MqttBroker_InitEx)
```c
broker->api_ctx = NULL;          /* API context allocated on start */
broker->enable_api = 1;          /* Enable HTTP API by default */
broker->api_port = MQTT_API_PORT; /* Default API port: 8081 */
```

## 验证步骤

### 1. 重新构建（已完成）
```bash
zig build broker -Dstatic-link=true --release=small
```

新二进制文件：`mqtt_broker-musleabihf` (120KB, 2026-05-08 09:35)

### 2. 在 ARM 设备上运行

上传并运行 broker：
```bash
chmod +x mqtt_broker-musleabihf
./mqtt_broker-musleabihf
```

### 3. 检查日志输出

**期望看到**：
```
[INFO ] listening on port 1883 (no TLS)
[INFO ] listening on port 8080 (WebSocket)
[INFO ] listening on port 8081 (HTTP API)  ← 这个必须出现
```

如果只看到前两行，说明还有问题。

### 4. 测试 HTTP API

在**另一个终端**中测试：

#### 查询统计信息
```bash
curl http://localhost:8081/api/stats
```

期望输出：
```json
{"conns":0,"rx_msgs":0,"tx_msgs":0,"rx_bytes":0,"tx_bytes":0,"retained":0,"subs":0,"uptime_seconds":<number>}
```

#### 发布消息
```bash
# Terminal 1: 订阅主题
mosquitto_sub -h localhost -t "test/api"

# Terminal 2: 通过 HTTP API 发布
curl -X POST http://localhost:8081/api/publish/test/api -d "Hello from HTTP API"
```

期望在 Terminal 1 中看到：`Hello from HTTP API`

#### 查看客户端列表
```bash
curl http://localhost:8081/api/clients
```

#### 踢出客户端
```bash
# 首先连接一个客户端
mosquitto_sub -h localhost -t "test" -i "test_client"

# 然后通过 API 踢出
curl -X POST http://localhost:8081/api/kick/test_client
```

## 故障排查

### 如果 HTTP API 日志仍然没有出现

1. **检查二进制文件日期**
   ```bash
   ls -lh mqtt_broker-musleabihf
   ```
   应该显示：`May  8 09:35`

2. **检查是否包含 API 代码**
   ```bash
   grep -a "listening on port %d (HTTP API)" mqtt_broker-musleabihf
   ```
   应该输出该字符串。

3. **检查编译时宏**
   查看 build 输出，应该包含：
   - `WOLFMQTT_BROKER_API`
   - `ENABLE_MQTT_WEBSOCKET`

4. **检查端口是否可用**
   ```bash
   netstat -tuln | grep 8081
   ```
   应该看到端口 8081 正在监听。

### 如果 API 请求失败

1. **检查 URL 是否正确**
   - 必须以 `/api/` 开头
   - 端口是 8081（不是 8080）

2. **检查 HTTP 方法**
   - GET: `/api/stats`, `/api/clients`, `/api/topics/<topic>`, `/api/configs`
   - POST: `/api/publish/<topic>`, `/api/configs`, `/api/reset`, `/api/kick/<client_id>`

3. **查看 broker 日志**
   应该看到类似：
   ```
   [INFO] New API connection accepted on sock=<number>
   [DEBUG] API Request: method=GET url=/api/stats sock=<number>
   [DEBUG] API Response: status=200 body_len=<number> total_sent=<number>
   ```

## 完整测试脚本

使用 `test_broker.sh` 进行完整测试：
```bash
bash test_broker.sh
```

该脚本会测试：
- MQTT 基本连接
- QoS 支持
- 保留消息
- 通配符订阅
- HTTP API 统计接口
- HTTP API 发布接口

## API 端点总结

| 端点 | 方法 | 功能 |
|------|------|------|
| `/api/stats` | GET | 查询统计信息 |
| `/api/clients` | GET | 查询客户端列表 |
| `/api/topics/<topic>` | GET | 查询主题订阅者 |
| `/api/configs` | GET | 查询 broker 配置 |
| `/api/publish/<topic>` | POST | 发布消息 |
| `/api/configs` | POST | 批量更新配置 |
| `/api/reset` | POST | 重置统计 |
| `/api/kick/<client_id>` | POST | 踢出客户端 |

## 端口分配

- **1883**: MQTT (无 TLS)
- **8080**: WebSocket
- **8081**: HTTP API ⭐ 新增

## 技术细节

### 代码变更
1. `mqtt_socket.h`: 添加 `MQTT_API_PORT` 常量
2. `mqtt_broker.c`:
   - `MqttBroker_InitEx()`: 添加 API 初始化
   - `MqttBroker_Start()`: 启动 API 监听器（已有）
   - `MqttBroker_Step()`: 处理 API 请求（已有）
   - `MqttBroker_Free()`: 清理 API 资源（已有）
   - `BrokerPublish_Message()`: 发布消息接口（已有）
   - `BrokerKick_Client()`: 踢出客户端接口（已有）

### 初始化流程
```
MqttBroker_Init()
  → MqttBroker_InitEx()
    → XMEMSET(broker, 0, sizeof(*broker))  // 清零
    → broker->enable_api = 1               // ✅ 启用 API
    → broker->api_port = MQTT_API_PORT     // ✅ 设置端口
    → broker->api_ctx = NULL               // ✅ 初始化为空
```

### 启动流程
```
MqttBroker_Run()
  → MqttBroker_Start()
    → if (broker->enable_api && broker->api_port > 0)  // ✅ 条件满足
      → MqttBrokerApi_Init(broker, broker->api_ctx, broker->api_port)
      → WBLOG_INFO("listening on port 8081 (HTTP API)")
```

## 成功标志

✅ 日志中出现 "listening on port 8081 (HTTP API)"
✅ `curl http://localhost:8081/api/stats` 返回 JSON
✅ 可以通过 HTTP API 发布消息
✅ 可以查询客户端列表
✅ 可以踢出客户端

## 下一步

如果测试成功，可以考虑：
1. 设置 API Token（生产环境必须）
2. 配置反向代理（nginx）提供 HTTPS
3. 添加认证和授权
4. 配置速率限制
5. 监控 API 访问日志

如果仍有问题，请提供：
1. 完整的 broker 启动日志
2. `curl -v http://localhost:8081/api/stats` 的详细输出
3. `netstat -tuln` 的输出

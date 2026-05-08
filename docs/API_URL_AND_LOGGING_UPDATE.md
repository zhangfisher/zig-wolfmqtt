# HTTP API URL路径更新和日志增强

## 修改概述

本次更新对Broker HTTP API进行了以下改进：

1. **所有URL添加 `/api` 前缀** - 统一API路径命名规范
2. **GET /options 更名为 GET /configs** - 更清晰的语义
3. **增加API启动/停止日志** - INFO级别
4. **增加请求/响应DEBUG日志** - 便于调试和监控

## 详细变更

### 1. URL路径变更

#### 旧路径 → 新路径

| 旧路径 | 新路径 | 说明 |
|--------|--------|------|
| `/stats` | `/api/stats` | 获取broker统计信息 |
| `/clients` | `/api/clients` | 获取已连接客户端列表 |
| `/topics/<topic>` | `/api/topics/<topic>` | 获取主题订阅者信息 |
| `/options` | `/api/configs` | 获取broker配置参数（重命名） |
| `/publish/<topic>` | `/api/publish/<topic>` | 发布消息 |
| `/config/<item>` | `/api/config/<item>` | 更新配置 |
| `/reset` | `/api/reset` | 重置broker |
| `/kick/<client>` | `/api/kick/<client>` | 踢掉客户端 |

#### 变更原因

- **统一命名空间**：所有API端点都在 `/api` 路径下，便于识别和管理
- **避免冲突**：防止与未来可能添加的其他HTTP端点冲突
- **RESTful规范**：符合REST API的最佳实践

### 2. GET /options → GET /configs

**变更原因**：
- `configs` 比 `options` 更清晰地表达"配置参数"的含义
- 与POST `/api/config/<item>` 保持一致的命名风格
- 避免与MQTT协议中的"options"概念混淆

### 3. API启动/停止日志

#### 启动日志

当API服务器成功启动时，输出INFO级别日志：

```
[INFO ] 1970-01-22 05:00:36 - listening on port 8081 (HttpApi)
```

**位置**：`src/mqtt_broker.c` - `MqttBroker_Start()` 函数

**实现**：
```c
WBLOG_INFO(broker, "listening on port 8081 (HttpApi)");
```

#### 停止日志

当API服务器停止时，输出INFO级别日志：

```
[INFO ] 1970-01-22 05:00:36 - HTTP API server stopped
```

**位置**：`src/mqtt_broker.c` - `MqttBroker_Free()` 函数

**实现**：
```c
WBLOG_INFO(broker, "HTTP API server stopped");
```

### 4. 请求/响应DEBUG日志

#### 请求日志

每个API请求都会记录DEBUG级别的详细信息：

```
[DEBUG] 1970-01-22 05:00:37 - API Request: method=GET url=/api/stats sock=5
[DEBUG] 1970-01-22 05:00:37 - API Request: method=POST url=/api/publish/test/topic?qos=1&retain=false sock=6
```

**包含信息**：
- HTTP方法（GET/POST）
- URL路径
- 查询参数（如果有）
- Socket描述符

**位置**：`src/mqtt_broker_api.c` - `handle_http_request()` 函数

**实现**：
```c
BA_LOG_DBG(api_ctx->broker, "API Request: method=%s url=%s%s%s sock=%d",
          method, path, (query[0] ? "?" : ""), query, (int)sock);
```

#### 响应日志

每个API响应都会记录DEBUG级别的信息：

```
[DEBUG] 1970-01-22 05:00:37 - API Response: status=200 body_len=120 total_sent=120
[DEBUG] 1970-01-22 05:00:37 - API Response: status=401 body_len=12 total_sent=12
[DEBUG] 1970-01-22 05:00:37 - API Response: status=404 body_len=9 total_sent=9
```

**包含信息**：
- HTTP状态码（200, 401, 404, 500等）
- Body长度
- 总发送字节数

**位置**：`src/mqtt_broker_api.c` - `http_send_response()` 函数

**实现**：
```c
{
    int status_code = 0;
    if (XSTRNCMP(status_line, "HTTP/1.1 ", 9) == 0) {
        status_code = XATOI(status_line + 9);
    }
    BA_LOG_DBG(broker, "API Response: status=%d body_len=%d total_sent=%d",
              status_code, body_len, total_sent);
}
```

#### 认证失败日志

当API token验证失败时，也会记录DEBUG日志：

```
[DEBUG] 1970-01-22 05:00:37 - API Response: status=401 Unauthorized
```

## 使用示例

### 编译带API功能的Broker

**注意**：HTTP API功能现在默认启用，无需额外的编译参数。

```bash
# 标准编译（API功能自动包含）
zig build broker -Dstatic-link=true -Dwebsocket=true --release=small

# 如果需要禁用API功能（可选）
zig build broker -Dstatic-link=true -Dwebsocket=true -Dbroker-api=false --release=small
```

### 启动Broker并启用API

```bash
./mqtt_broker-musleabihf -api-token "testtoken12345" -port 1883
```

**预期日志输出**：
```
[INFO ] 1970-01-22 05:00:36 - listening on port 1883 (TCP)
[INFO ] 1970-01-22 05:00:36 - listening on port 8081 (HttpApi)
```

### 测试新的API端点

#### 1. 获取统计信息

```bash
curl -H "Authorization: Basic testtoken12345" \
  http://localhost:8081/api/stats
```

**预期DEBUG日志**：
```
[DEBUG] 1970-01-22 05:00:37 - API Request: method=GET url=/api/stats sock=5
[DEBUG] 1970-01-22 05:00:37 - API Response: status=200 body_len=120 total_sent=120
```

#### 2. 获取配置信息（原 /options）

```bash
curl -H "Authorization: Basic testtoken12345" \
  http://localhost:8081/api/configs
```

**注意**：旧路径 `/api/options` 不再有效，会返回404。

#### 3. 发布消息

```bash
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -H "Content-Type: text/plain" \
  -d "Hello from HTTP API!" \
  "http://localhost:8081/api/publish/test/topic?qos=1&retain=false"
```

**预期DEBUG日志**：
```
[DEBUG] 1970-01-22 05:00:37 - API Request: method=POST url=/api/publish/test/topic?qos=1&retain=false sock=6
[DEBUG] 1970-01-22 05:00:37 - API publish to topic=test/topic, qos=1, retain=0, payload_len=18
[DEBUG] 1970-01-22 05:00:37 - API Response: status=200 body_len=60 total_sent=60
```

#### 4. 踢掉客户端

```bash
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  "http://localhost:8081/api/kick/client123"
```

**预期DEBUG日志**：
```
[DEBUG] 1970-01-22 05:00:37 - API Request: method=POST url=/api/kick/client123 sock=7
[INFO ] 1970-01-22 05:00:37 - Kicking client client123 (sock=8, ip=192.168.1.100)
[DEBUG] 1970-01-22 05:00:37 - API Response: status=200 body_len=50 total_sent=50
```

### 停止Broker

当停止Broker时，会看到：

```
[INFO ] 1970-01-22 05:00:36 - HTTP API server stopped
```

## 代码变更文件

### 修改的文件

1. **src/mqtt_broker_api.c**
   - 更新所有URL路由匹配（添加 `/api` 前缀）
   - 将 `/options` 改为 `/configs`
   - 在 `handle_http_request()` 中添加请求日志
   - 在 `http_send_response()` 中添加响应日志

2. **src/mqtt_broker.c**
   - 在 `MqttBroker_Start()` 中更新API启动日志
   - 在 `MqttBroker_Free()` 中添加API停止日志

3. **test_api.sh**
   - 更新所有测试URL以使用新路径

4. **API_PUBLISH_KICK_IMPLEMENTATION.md**
   - 更新文档以反映新的URL路径
   - 添加日志系统说明

## 兼容性说明

### 破坏性变更

⚠️ **这是一个破坏性变更**，所有使用旧URL路径的客户端都需要更新：

- ❌ `GET /stats` → ✅ `GET /api/stats`
- ❌ `GET /options` → ✅ `GET /api/configs`
- ❌ `POST /publish/...` → ✅ `POST /api/publish/...`
- ❌ `POST /kick/...` → ✅ `POST /api/kick/...`

### 迁移指南

如果您的应用程序或脚本使用了旧的API路径，需要进行以下更新：

1. 在所有URL前添加 `/api` 前缀
2. 将 `/options` 替换为 `/configs`

**示例**：
```bash
# 旧代码
curl http://localhost:8081/stats
curl http://localhost:8081/options

# 新代码
curl http://localhost:8081/api/stats
curl http://localhost:8081/api/configs
```

## 调试技巧

### 启用DEBUG日志

要查看API请求和响应的DEBUG日志，需要启用调试模式：

```bash
./mqtt_broker-musleabihf -api-token "testtoken12345" -debug
```

或者在编译时启用调试：

```bash
zig build broker -Dstatic-link=true -Dwebsocket=true -Dbroker-api=true --debug
```

### 日志分析

通过DEBUG日志可以：
- 监控API调用频率
- 诊断认证问题（401错误）
- 追踪特定客户端的API使用
- 分析API性能（通过响应大小和时间）

## 测试

运行更新后的测试脚本：

```bash
chmod +x test_api.sh
./test_api.sh
```

测试脚本已更新为使用新的URL路径，包括：
- GET /api/stats
- POST /api/publish/test/topic
- POST /api/publish/retained/topic
- GET /api/clients
- GET /api/configs
- POST /api/kick/nonexistent_client

## 总结

本次更新提升了API的规范性和可维护性：

✅ **统一的URL命名空间** - 所有API都在 `/api` 路径下  
✅ **更清晰的语义** - `/configs` 比 `/options` 更直观  
✅ **完善的日志系统** - 启动/停止、请求/响应都有日志记录  
✅ **便于调试** - DEBUG级别的详细日志帮助问题排查  
✅ **符合最佳实践** - RESTful API设计规范  

这些改进使得API更加专业、易用和可维护。

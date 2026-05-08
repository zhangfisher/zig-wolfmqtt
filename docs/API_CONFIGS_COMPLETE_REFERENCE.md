# Broker HTTP API 完整配置项参考

## 概述

本文档列出了通过HTTP API `/api/configs`端点可配置的所有Broker选项。

**API端点**: `POST /api/configs`  
**Content-Type**: `application/x-www-form-urlencoded`  
**格式**: `key1=value1&key2=value2`

---

## 📋 配置项清单（32个）

### 1. 功能开关类（4个）

| 配置项 | 类型 | 有效值 | 默认值 | 说明 | 条件编译 |
|--------|------|--------|--------|------|----------|
| `enable_stats` | byte | true/false/1/0 | 1 | 启用统计功能 | - |
| `enable_api` | byte | true/false/1/0 | 1 | 启用HTTP API | - |
| `use_tls` | byte | true/false/1/0 | 0 | 启用TLS支持 | ENABLE_MQTT_TLS |
| `use_ws` | byte | true/false/1/0 | 0 | 启用WebSocket | ENABLE_MQTT_WEBSOCKET |

**示例**：
```bash
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "enable_stats=true&enable_api=true" \
  http://localhost:8081/api/configs
```

---

### 2. 日志与监控类（2个）

| 配置项 | 类型 | 范围 | 默认值 | 说明 |
|--------|------|------|--------|------|
| `log_level` | byte | 0-5 | 1 (INFO) | 日志级别：0=DEBUG, 1=INFO, 2=WARN, 3=ERROR, 4=FATAL, 5=NONE |
| `stats_interval` | word32 | 0-3600 | 20 | 统计消息发送间隔(秒)，0=禁用 |

**示例**：
```bash
# 设置详细日志和频繁统计
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "log_level=0&stats_interval=10" \
  http://localhost:8081/api/configs
```

---

### 3. 网络与端口类（6个）

| 配置项 | 类型 | 范围 | 默认值 | 说明 |
|--------|------|------|--------|------|
| `port` | word16 | 1-65535 | 1883 | MQTT监听端口 |
| `api_port` | word16 | 1-65535 | 8081 | HTTP API监听端口 |
| `port_tls` | word16 | 1-65535 | 8883 | TLS监听端口 |
| `port_ws` | word16 | 1-65535 | 8080 | WebSocket监听端口 |
| `timeout_ms` | word16 | 100-60000 | 1000 | 网络超时时间(毫秒) |
| `listen_backlog` | word16 | 1-1024 | 128 | 监听队列长度 |

**注意**：端口更改后需要重启Broker才能生效。

**示例**：
```bash
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "timeout_ms=5000&listen_backlog=256" \
  http://localhost:8081/api/configs
```

---

### 4. 缓冲区大小类（2个）

| 配置项 | 类型 | 范围 | 默认值 | 说明 |
|--------|------|------|--------|------|
| `rx_buf_sz` | word16 | 256-65535 | 4096 | 每个客户端接收缓冲区大小(字节) |
| `tx_buf_sz` | word16 | 256-65535 | 4096 | 每个客户端发送缓冲区大小(字节) |

**建议**：
- 小消息场景：2048-4096字节
- 大消息场景：8192-16384字节
- 注意内存消耗：总内存 = (rx_buf_sz + tx_buf_sz) × max_clients

**示例**：
```bash
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "rx_buf_sz=8192&tx_buf_sz=8192" \
  http://localhost:8081/api/configs
```

---

### 5. 容量限制类（4个）

| 配置项 | 类型 | 范围 | 默认值 | 说明 |
|--------|------|------|--------|------|
| `max_clients` | word16 | 0-1000 | 0 | 最大并发客户端数，0=无限制 |
| `max_subs` | word16 | 0-10000 | 0 | 最大订阅数，0=无限制 |
| `max_retained` | word16 | 0-1000 | 0 | 最大保留消息数，0=无限制 |
| `max_pending_wills` | word16 | 0-100 | 0 | 最大待处理遗嘱数，0=无限制 |

**重要**：值为0表示无限制，实际受可用内存和静态数组大小限制。

**示例**：
```bash
# 限制最多50个客户端和200个订阅
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "max_clients=50&max_subs=200" \
  http://localhost:8081/api/configs

# 设置为无限制
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "max_clients=0&max_subs=0" \
  http://localhost:8081/api/configs
```

---

### 6. 认证与安全类（3个）

| 配置项 | 类型 | 要求 | 默认值 | 说明 | 条件编译 |
|--------|------|------|--------|------|----------|
| `username` | string | 任意字符串 | NULL | 认证用户名，空字符串=禁用认证 | WOLFMQTT_BROKER_AUTH |
| `password` | string | 任意字符串 | NULL | 认证密码，空字符串=禁用认证 | WOLFMQTT_BROKER_AUTH |
| `api_token` | string | ≥12字符 | "" | API认证token | - |

**安全提示**：
- API token至少12个字符
- 建议使用强密码和随机token
- 通过HTTPS传输敏感信息

**示例**：
```bash
# 设置MQTT认证
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "username=admin&password=securepass123" \
  http://localhost:8081/api/configs

# 更新API token
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "api_token=newtoken123456789" \
  http://localhost:8081/api/configs

# 禁用认证（设置空字符串）
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "username=&password=" \
  http://localhost:8081/api/configs
```

---

### 7. MQTT 5 特性类（2个）

| 配置项 | 类型 | 范围 | 默认值 | 说明 | 条件编译 |
|--------|------|------|--------|------|----------|
| `max_packet_size` | word32 | 0-268435455 | 0 | Broker最大包大小，0=无限制 | WOLFMQTT_V5 |
| `topic_alias_max` | word16 | 0-65535 | 0 | 主题别名最大值 | WOLFMQTT_V5 |

**示例**：
```bash
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "max_packet_size=1048576&topic_alias_max=10" \
  http://localhost:8081/api/configs
```

---

### 8. 会话持久化类（1个）

| 配置项 | 类型 | 范围 | 默认值 | 说明 |
|--------|------|------|--------|------|
| `default_session_expiry_interval` | word32 | 0-4294967295 | 0 | 默认会话过期间隔(秒)，0=永不过期 |

**说明**：当客户端未指定Session Expiry Interval且clean session=0时使用此值。

**示例**：
```bash
# 设置会话过期时间为1小时
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "default_session_expiry_interval=3600" \
  http://localhost:8081/api/configs

# 会话永不过期
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "default_session_expiry_interval=0" \
  http://localhost:8081/api/configs
```

---

### 9. 负载限制类（2个）

| 配置项 | 类型 | 范围 | 默认值 | 说明 |
|--------|------|------|--------|------|
| `max_payload_len` | word16 | 0-65535 | 4096 | 消息负载最大大小(字节) |
| `max_will_payload_len` | word16 | 0-65535 | 256 | 遗嘱消息负载最大大小(字节) |

**示例**：
```bash
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "max_payload_len=8192&max_will_payload_len=512" \
  http://localhost:8081/api/configs
```

---

### 10. TLS配置类（1个）

| 配置项 | 类型 | 有效值 | 默认值 | 说明 | 条件编译 |
|--------|------|--------|--------|------|----------|
| `tls_version` | byte | 0/12/13 | 0 | TLS版本：0=auto, 12=TLS 1.2, 13=TLS 1.3 | ENABLE_MQTT_TLS |

**示例**：
```bash
# 强制使用TLS 1.2
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "tls_version=12" \
  http://localhost:8081/api/configs

# 自动选择最佳版本
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "tls_version=0" \
  http://localhost:8081/api/configs
```

---

## 🔧 批量配置示例

### 示例1：生产环境优化配置

```bash
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "log_level=2&stats_interval=60&timeout_ms=5000&rx_buf_sz=8192&tx_buf_sz=8192&max_clients=100&max_subs=500&max_retained=50" \
  http://localhost:8081/api/configs
```

**说明**：
- 警告级别日志，减少日志输出
- 每分钟发送统计
- 5秒超时，适合不稳定网络
- 8KB缓冲区，适合中等消息
- 限制100客户端、500订阅、50保留消息

### 示例2：开发环境调试配置

```bash
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "log_level=0&stats_interval=10&timeout_ms=10000&max_clients=0&max_subs=0&max_retained=0" \
  http://localhost:8081/api/configs
```

**说明**：
- DEBUG级别日志，详细信息
- 每10秒发送统计
- 10秒超时，方便调试
- 无限制，便于测试

### 示例3：高安全性配置

```bash
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "username=broker_admin&password=S3cur3P@ss!&api_token=Str0ngT0ken12345&max_clients=50&max_payload_len=4096" \
  http://localhost:8081/api/configs
```

**说明**：
- 启用MQTT认证
- 强API token
- 限制客户端数量
- 限制消息大小

### 示例4：高性能配置

```bash
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "rx_buf_sz=16384&tx_buf_sz=16384&listen_backlog=512&timeout_ms=2000&max_clients=500" \
  http://localhost:8081/api/configs
```

**说明**：
- 16KB大缓冲区
- 512连接队列
- 2秒快速超时
- 支持500客户端

---

## ⚠️ 注意事项

### 1. 运行时 vs 启动时配置

**可立即生效的配置**：
- log_level, stats_interval, enable_stats
- timeout_ms, rx_buf_sz, tx_buf_sz
- max_clients, max_subs, max_retained, max_pending_wills
- max_payload_len, max_will_payload_len
- default_session_expiry_interval
- username, password, api_token
- enable_api, enable_stats

**需要重启生效的配置**：
- port, api_port, port_tls, port_ws
- listen_backlog
- use_tls, use_ws
- tls_version

### 2. 内存考虑

配置较大的缓冲区和较多的客户端会显著增加内存使用：

```
总内存 ≈ (rx_buf_sz + tx_buf_sz) × max_clients + 其他开销
```

**示例计算**：
- rx_buf_sz=4096, tx_buf_sz=4096, max_clients=100
- 内存 ≈ (4096 + 4096) × 100 = 819,200 字节 ≈ 800 KB

### 3. 静态内存模式限制

在 `WOLFMQTT_STATIC_MEMORY` 模式下，即使设置更大的值，实际限制仍受编译时数组大小约束：

```c
// 编译时：BROKER_MAX_CLIENTS = 8
broker->max_clients = 100;  // 仍然只能使用8个槽位
```

### 4. 错误处理

API返回三种状态：

**全部成功**：
```json
{
  "status": "success",
  "message": "Updated 5 configuration(s)",
  "updated": 5
}
```

**部分成功**：
```json
{
  "status": "partial",
  "message": "Updated 3 configuration(s) with errors",
  "updated": 3,
  "errors": "invalid_option='xyz' invalid; log_level=99 out of range [0-5]"
}
```

**全部失败**：
```json
{
  "status": "error",
  "message": "Failed to update configurations",
  "errors": "unknown option 'bad_field'"
}
```

---

## 📊 配置项优先级建议

### P0 - 核心配置（推荐所有用户）
- `log_level` - 调整日志详细程度
- `stats_interval` - 控制监控频率
- `max_clients` - 防止资源耗尽
- `api_token` - 保护API访问

### P1 - 性能调优（根据负载调整）
- `rx_buf_sz` / `tx_buf_sz` - 优化消息处理
- `timeout_ms` - 适应网络条件
- `max_subs` / `max_retained` - 控制订阅规模

### P2 - 安全加固（生产环境必需）
- `username` / `password` - MQTT认证
- `max_payload_len` - 防止大消息攻击
- `enable_api` - 控制API访问

### P3 - 高级特性（特殊需求）
- MQTT 5 特性
- 会话持久化
- TLS配置

---

## 🔍 获取当前配置

虽然目前没有GET /api/configs端点返回所有配置，但可以通过以下方式查看：

1. **查看日志** - 启动时会显示关键配置
2. **检查统计** - GET /api/stats 显示运行状态
3. **源代码** - 查看MqttBroker结构体定义

未来可以添加 GET /api/configs 端点来查询当前配置。

---

## 📝 总结

✅ **32个可配置项** - 覆盖所有重要方面  
✅ **批量更新** - 一次请求更新多个配置  
✅ **严格验证** - 每个字段都有范围检查  
✅ **灵活控制** - 从基础到高级全面支持  
✅ **向后兼容** - 不影响现有功能  

这些配置项使得Broker可以在运行时灵活调整，无需重新编译！

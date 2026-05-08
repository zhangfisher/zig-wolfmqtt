# Broker HTTP API 批量配置更新功能实现

## 概述

实现了Broker HTTP API的批量配置更新功能，支持通过单个请求更新多个配置项。

## 主要变更

### 1. URL路径变更

**旧路径**：`POST /api/config/<option_name>`  
**新路径**：`POST /api/configs`

### 2. 请求格式

**Content-Type**: `application/x-www-form-urlencoded`

**Body格式**：URL-encoded键值对
```
key1=value1&key2=value2&key3=value3
```

### 3. 支持的配置项

| 配置项 | 类型 | 范围 | 说明 |
|--------|------|------|------|
| `log_level` | byte | 0-5 | 日志级别 |
| `stats_interval` | uint | 1-3600 | 统计间隔(秒) |
| `enable_stats` | bool | true/false/1/0 | 启用统计 |
| `timeout_ms` | word16 | 100-60000 | 超时时间(毫秒) |
| `max_clients` | word16 | 1-1000 | 最大客户端数 |
| `rx_buf_sz` | word16 | 256-65535 | 接收缓冲区大小 |
| `tx_buf_sz` | word16 | 256-65535 | 发送缓冲区大小 |
| `listen_backlog` | word16 | 1-128 | 监听队列长度 |

## 实现细节

### 函数签名

```c
static int handle_post_configs(MqttBrokerApiContext* api_ctx, 
                               BROKER_SOCKET_T sock, 
                               const char* body, 
                               int body_len)
```

### 解析逻辑

1. **复制body到本地缓冲区**（最大1024字节）
2. **使用strtok_r分割** `&` 分隔的键值对
3. **使用strchr查找** `=` 分隔符
4. **去除前后空白字符**
5. **验证配置项名称** - 必须是MqttBroker结构体中的字段
6. **验证值的范围** - 每个字段都有有效的取值范围
7. **更新配置** - 直接修改broker结构体字段

### 响应格式

#### 全部成功
```json
{
  "status": "success",
  "message": "Updated 3 configuration(s)",
  "updated": 3
}
```

#### 部分成功
```json
{
  "status": "partial",
  "message": "Updated 2 configuration(s) with errors",
  "updated": 2,
  "errors": "invalid_option='xyz' invalid; log_level=99 out of range [0-5]"
}
```

#### 全部失败
```json
{
  "status": "error",
  "message": "Failed to update configurations",
  "errors": "unknown option 'invalid_field'; unknown option 'bad_name'"
}
```

### 错误处理

- ✅ **未知配置项** - 返回错误并列出所有无效项
- ✅ **超出范围的值** - 返回错误并指明有效范围
- ✅ **无效的布尔值** - 提示使用true/false/1/0
- ✅ **空请求体** - 返回400错误

### 日志策略

**仅ERROR级别日志**：
- broker不可用
- 请求体为空
- 配置项验证失败

正常操作不产生任何日志输出。

## 使用示例

### curl命令

#### 更新单个配置
```bash
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "log_level=3" \
  http://localhost:8081/api/configs
```

#### 批量更新多个配置
```bash
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "log_level=2&stats_interval=60&enable_stats=true&timeout_ms=10000" \
  http://localhost:8081/api/configs
```

### VS Code REST Client

```http
### 批量更新配置
POST {{baseUrl}}/api/configs
Authorization: Basic {{apiToken}}
Content-Type: application/x-www-form-urlencoded

log_level=2&stats_interval=60&enable_stats=true
```

## 测试用例

测试文件 `broker.api.rest` 包含以下测试用例：

1. ✅ 更新单个配置：日志级别
2. ✅ 更新单个配置：启用统计
3. ✅ 批量更新多个配置
4. ✅ 批量更新缓冲区大小和客户端限制
5. ✅ 更新所有常用配置
6. ✅ 测试无效配置项（应该返回错误）
7. ✅ 测试混合有效和无效配置（部分成功）
8. ✅ 测试超出范围的值

## 优势

### 1. 简洁高效
- ✅ 无需JSON解析库
- ✅ 简单的字符串分割即可解析
- ✅ 零额外依赖

### 2. 灵活性强
- ✅ 支持单个配置更新
- ✅ 支持批量配置更新
- ✅ 一次请求可更新任意数量的配置项

### 3. 严格验证
- ✅ 只接受MqttBroker结构体中的字段
- ✅ 每个字段都有范围验证
- ✅ 未知字段会明确报错

### 4. 清晰的反馈
- ✅ 区分全部成功、部分成功、全部失败
- ✅ 错误信息详细具体
- ✅ 包含更新的配置项数量

### 5. 易于扩展
添加新配置项只需：
```c
else if (XSTRCMP(key, "new_option") == 0) {
    // 验证value
    // 更新broker->new_option
    updated_count++;
}
```

## 性能考虑

- **内存占用**：最多1024字节的临时缓冲区
- **CPU开销**：简单的字符串操作，非常轻量
- **网络效率**：URL-encoded格式比JSON更紧凑

## 兼容性

### 向后兼容性

⚠️ **破坏性变更**：旧的 `/api/config/<option_name>` 路径已移除

**迁移方法**：
- 旧：`POST /api/config/log_level` with JSON body
- 新：`POST /api/configs` with `log_level=3`

### 构建系统

- ✅ Zig构建：默认启用
- ✅ CMake：默认启用（WOLFMQTT_BROKER_API=yes）
- ✅ Autotools：默认启用（--enable-broker-api）

## 文件大小

编译后的大小增加约 **2KB**（从114,080字节增加到116,512字节），这是因为添加了配置验证逻辑。

## 相关文件

- `src/mqtt_broker_api.c` - 实现文件
- `broker.api.rest` - REST Client测试文件
- `API_PUBLISH_KICK_IMPLEMENTATION.md` - API实现文档
- `BROKER_API_REST_CLIENT_GUIDE.md` - REST Client使用指南

## 总结

批量配置更新功能提供了：
- ✅ 简洁的URL-encoded格式
- ✅ 严格的字段验证
- ✅ 灵活的批量更新能力
- ✅ 清晰的错误反馈
- ✅ 极低的实现成本

这是一个实用、高效且易于维护的配置管理方案。

# HTTP Basic 认证实施总结

## 概述

将 wolfMQTT Broker 的 HTTP API 服务器从简单的 token 认证升级为标准的 HTTP Basic 认证，支持用户名和密码验证。

## 主要变更

### 1. 字段重命名

#### MqttBroker 结构 (wolfmqtt/mqtt_broker.h)
- `enable_api` → `enable_http` (启用 HTTP 服务器)
- `api_token[64]` → `http_username[64]` + `http_password[64]` (HTTP Basic 认证凭据)

#### MqttBrokerApiContext 结构 (wolfmqtt/mqtt_broker.h)
- `api_token[64]` → `http_username[64]` + `http_password[64]`

### 2. 默认值变更 (src/mqtt_broker.c)

**之前:**
```c
broker->enable_api = 1;
XSTRNCPY(broker->api_token, "22182666", ...);
```

**现在:**
```c
broker->enable_http = 1;
XSTRNCPY(broker->http_username, "admin", ...);
XSTRNCPY(broker->http_password, "22182666", ...);
```

### 3. 认证逻辑重构 (src/mqtt_broker_api.c)

#### 新增函数
- `validate_http_basic_auth()` - 验证 HTTP Basic 认证
  - 解码 Base64 编码的 `username:password`
  - 与配置的凭据进行对比
  - 支持禁用认证（username 为空时）

#### 修改函数
- `MqttBrokerApi_SetToken()` → `MqttBrokerApi_SetCredentials()`
  - 参数从单个 token 改为 username 和 password
  - 允许单独设置或清除凭据

#### 配置项更新
- `enable_api` → `enable_http`
- `api_token` → `http_username` + `http_password`

### 4. 认证流程

所有 HTTP 请求（包括 `/api/*` 和静态文件）都使用统一的 HTTP Basic 认证：

```
客户端请求
    ↓
检查 Authorization 头
    ↓
提取 "Basic <base64_credentials>"
    ↓
Base64 解码得到 "username:password"
    ↓
与配置的 http_username/http_password 对比
    ↓
匹配 → 允许访问
不匹配 → 返回 401 Unauthorized
```

## API 变更

### 旧 API
```c
// 设置 token
int MqttBrokerApi_SetToken(MqttBrokerApiContext* api_ctx, const char* token);
```

### 新 API
```c
// 设置 HTTP Basic 认证凭据
int MqttBrokerApi_SetCredentials(MqttBrokerApiContext* api_ctx, 
                                 const char* username, 
                                 const char* password);
```

## 使用示例

### C 代码配置

```c
MqttBroker broker;
MqttBroker_Init(&broker);

// 方法 1: 直接设置字段
XSTRNCPY(broker.http_username, "myuser", sizeof(broker.http_username) - 1);
XSTRNCPY(broker.http_password, "mypassword", sizeof(broker.http_password) - 1);

// 方法 2: 使用 API 函数
MqttBrokerApi_SetCredentials(broker.api_ctx, "myuser", "mypassword");

// 禁用认证
broker.http_username[0] = '\0';
```

### HTTP API 动态配置

```bash
# 修改用户名和密码
curl -u admin:22182666 -X POST http://localhost:8081/api/configs \
  -d "http_username=newuser&http_password=newpass"

# 禁用认证
curl -u admin:22182666 -X POST http://localhost:8081/api/configs \
  -d "http_username="
```

### curl 访问示例

```bash
# API 端点
curl -u admin:22182666 http://localhost:8081/api/stats

# 静态文件
curl -u admin:22182666 http://localhost:8081/index.html

# 目录（自动映射到 index.html）
curl -u admin:22182666 http://localhost:8081/about/
```

### JavaScript (浏览器)

```javascript
// 使用 fetch API
fetch('http://localhost:8081/api/stats', {
  headers: {
    'Authorization': 'Basic ' + btoa('admin:22182666')
  }
})
.then(response => response.json())
.then(data => console.log(data));
```

## 安全性考虑

### 优点
1. **标准协议** - HTTP Basic 是 RFC 7617 标准，广泛支持
2. **简单实现** - 无需复杂的会话管理
3. **通用兼容** - 所有 HTTP 客户端都支持

### 注意事项
1. **Base64 不是加密** - 凭据只是编码，不是加密
2. **需要 HTTPS** - 生产环境应配合 TLS 使用
3. **明文传输** - 在不安全的网络上可能被窃听

### 建议
- ✅ 开发/测试环境：可以直接使用
- ⚠️ 生产环境：必须启用 TLS (`use_tls=1`)
- 🔒 敏感数据：建议使用更强的认证机制

## 向后兼容性

### 破坏性变更
- ❌ 旧的 `api_token` 字段不再存在
- ❌ `MqttBrokerApi_SetToken()` 函数已移除
- ❌ 配置文件中的 `api_token` 键名需改为 `http_username` 和 `http_password`

### 迁移指南
如果之前使用了 `api_token`:

```c
// 旧代码
broker.api_token[0] = '\0';  // 禁用认证
XSTRNCPY(broker.api_token, "mytoken", ...);  // 设置 token

// 新代码
broker.http_username[0] = '\0';  // 禁用认证
XSTRNCPY(broker.http_username, "admin", ...);  // 设置用户名
XSTRNCPY(broker.http_password, "mytoken", ...);  // 密码可以用原来的 token
```

## 测试

提供了测试脚本：`scripts/test_http_basic_auth.sh`

运行测试：
```bash
chmod +x scripts/test_http_basic_auth.sh
./scripts/test_http_basic_auth.sh
```

测试覆盖：
1. ✅ 无认证的 API 请求（应失败）
2. ✅ 正确凭据的 API 请求（应成功）
3. ✅ 错误凭据的 API 请求（应失败）
4. ✅ 无认证的静态文件请求（应失败）
5. ✅ 正确凭据的静态文件请求（应成功）
6. ✅ 目录访问（应映射到 index.html）
7. ✅ CSS 文件访问
8. ✅ 不存在文件的处理

## 代码量统计

- **修改文件**: 3 个
  - `wolfmqtt/mqtt_broker.h` - 结构定义
  - `src/mqtt_broker.c` - 初始化代码
  - `src/mqtt_broker_api.c` - 认证逻辑和配置

- **新增代码**: ~60 行
  - HTTP Basic 认证验证函数
  - Base64 解码集成
  - 凭据设置函数

- **删除代码**: ~20 行
  - 旧的 token 验证逻辑

- **净增加**: ~40 行

## 日志输出

### 启动时
```
INFO: API server started on port 8081, static directory: www
INFO: HTTP Basic auth credentials set (username: admin)
```

### 认证失败
```
DEBUG: API Response: status=401 Unauthorized
```

### 认证成功
```
DEBUG: API Request GET /api/stats from=127.0.0.1
DEBUG: API Response: status=200
```

## 相关文件

- [STATIC_FILE_SERVING.md](STATIC_FILE_SERVING.md) - 静态文件服务文档（已更新）
- [scripts/test_http_basic_auth.sh](../scripts/test_http_basic_auth.sh) - 测试脚本
- [HTTP_BASIC_AUTH_IMPLEMENTATION.md](HTTP_BASIC_AUTH_IMPLEMENTATION.md) - 本文档

## 总结

本次实施将 wolfMQTT Broker 的 HTTP API 认证从自定义 token 方案升级为标准的 HTTP Basic 认证，提供了更好的兼容性和标准化支持。所有 HTTP 请求（API 和静态文件）都使用统一的认证机制，简化了安全模型。

**关键特性:**
- ✅ 标准 HTTP Basic 认证 (RFC 7617)
- ✅ 统一认证（API + 静态文件）
- ✅ 可配置的用户名和密码
- ✅ 支持禁用认证
- ✅ 动态配置支持
- ✅ 完整的测试覆盖

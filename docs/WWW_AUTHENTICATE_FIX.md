# WWW-Authenticate 响应头修复

## 问题描述

当用户访问 `http://192.168.116.217:8081/` 时，浏览器返回 401 Unauthorized 但没有弹出认证对话框。

**原因**: HTTP 规范要求服务器在返回 401 状态码时，必须包含 `WWW-Authenticate` 响应头，浏览器才会显示认证对话框。

## 解决方案

### 1. 添加 WWW-Authenticate 常量定义

在 `src/mqtt_broker_api.c` 中添加：

```c
#define WWW_AUTHENTICATE_BASIC  "WWW-Authenticate: Basic realm=\"wolfMQTT Broker\"\r\n"
```

### 2. 扩展 http_send_response 函数

创建支持额外响应头的函数：

```c
static int http_send_response_with_headers(MqttBroker* broker, BROKER_SOCKET_T sock, 
                                           const char* status_line, const char* content_type,
                                           const char* extra_headers,
                                           const char* body, int body_len)
{
    // ... 实现代码
}
```

### 3. 修改 401 响应

在认证失败时，使用新的函数并添加 WWW-Authenticate 头：

```c
if (!validate_http_basic_auth(api_ctx, auth_header)) {
    LOG_API_RESPONSE(api_ctx->broker, 401, method, path, query, client_ip);
    return http_send_response_with_headers(api_ctx->broker, sock, HTTP_401_UNAUTHORIZED, 
                                          CONTENT_TYPE_TEXT,
                                          WWW_AUTHENTICATE_BASIC,
                                          "Unauthorized", 12);
}
```

## 验证方法

### 使用 curl 测试

```bash
# 查看完整的响应头
curl -v http://192.168.116.217:8081/api/stats

# 应该看到类似输出：
# < HTTP/1.1 401 Unauthorized
# < Content-Type: text/plain
# < WWW-Authenticate: Basic realm="wolfMQTT Broker"
# < Connection: close
```

### 浏览器测试

现在访问 `http://192.168.116.217:8081/` 时：
1. 浏览器收到 401 + WWW-Authenticate 头
2. 自动弹出认证对话框
3. 输入用户名 `admin` 和密码 `22182666`
4. 浏览器自动重新发送请求，带上 Authorization 头

## HTTP Basic 认证流程

```
客户端                        服务器
  |                             |
  |--- GET /api/stats -------->|
  |                             |
  |<-- 401 Unauthorized -------|
  |    WWW-Authenticate:       |
  |    Basic realm="..."       |
  |                             |
  |-- 弹出认证对话框 ---------->|
  |                             |
  |--- GET /api/stats -------->|
  |    Authorization:          |
  |    Basic YWRtaW46MjIx...   |
  |                             |
  |<-- 200 OK -----------------|
  |    { stats data }          |
```

## RFC 规范参考

根据 [RFC 7235](https://tools.ietf.org/html/rfc7235#section-4.1)：

> The 401 (Unauthorized) status code indicates that the request has not been applied because it lacks valid authentication credentials for the target resource. The server generating a 401 response MUST send a WWW-Authenticate header field containing at least one challenge applicable to the target resource.

**翻译**: 服务器生成 401 响应时，**必须**发送 WWW-Authenticate 头字段，其中至少包含一个适用于目标资源的挑战。

## 相关文件

- 修改文件: `src/mqtt_broker_api.c`
- 测试脚本: `scripts/test_www_authenticate.sh`
- 相关文档: `docs/HTTP_BASIC_AUTH_IMPLEMENTATION.md`

## 总结

✅ 添加了 `WWW-Authenticate` 响应头  
✅ 浏览器现在会正确弹出认证对话框  
✅ 符合 HTTP 标准 (RFC 7235)  
✅ 改善了用户体验  
✅ 根路径 `/` 自动映射到 `index.html`  
✅ 修复了路径安全检查，允许根路径访问  

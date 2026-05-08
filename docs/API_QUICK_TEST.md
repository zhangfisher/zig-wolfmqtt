# Broker API 快速测试指南

## 🚀 快速开始

### 1. 安装扩展
在 VS Code 中安装 **REST Client** 扩展（搜索 "REST Client" by Huachao Mao）

### 2. 配置变量
打开 `broker.api.rest` 文件，修改顶部的变量：

```http
@baseUrl = http://localhost:8081        # Broker地址
@apiToken = testtoken12345              # API Token
```

### 3. 发送请求
将光标放在任意请求上，点击 **"Send Request"** 或按 `Ctrl+Alt+R`

---

## 📋 常用测试

### ✅ 健康检查
```http
GET {{baseUrl}}/api/stats
Authorization: Basic {{apiToken}}
```

### 📤 发布消息
```http
POST {{baseUrl}}/api/publish/test/topic?qos=1
Authorization: Basic {{apiToken}}
Content-Type: text/plain

Hello World!
```

### 👥 查看客户端
```http
GET {{baseUrl}}/api/clients
Authorization: Basic {{apiToken}}
```

### ⚙️ 查看配置
```http
GET {{baseUrl}}/api/configs
Authorization: Basic {{apiToken}}
```

### 🚫 踢掉客户端
```http
POST {{baseUrl}}/api/kick/client123
Authorization: Basic {{apiToken}}
```

---

## 🔧 环境变量说明

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `@baseUrl` | `http://localhost:8081` | API服务器地址 |
| `@apiToken` | `testtoken12345` | 认证token |
| `@contentType` | `application/json` | 内容类型 |

---

## 📝 测试用例分类

- **1-4**: GET请求（stats, clients, topics, configs）
- **5-8**: POST发布消息（不同QoS和retain选项）
- **9-10**: 踢掉客户端（通过ID或IP）
- **11-12**: 管理操作（reset, config）
- **13-16**: 错误测试（401, 404）
- **17-21**: 高级场景（批量、性能等）

---

## 💡 快捷键

| 操作 | Windows/Linux | Mac |
|------|--------------|-----|
| 发送请求 | `Ctrl+Alt+R` | `Cmd+Option+R` |
| 发送所有请求 | - | - |
| 清除历史 | - | - |

---

## ⚠️ 常见问题

### 401 Unauthorized
- ✓ 检查 token 是否正确
- ✓ 确认每个请求都有 Authorization 头

### 404 Not Found
- ✓ 确认 URL 以 `/api` 开头
- ✓ 检查 broker 是否启动

### 连接被拒绝
- ✓ 确认 broker 正在运行
- ✓ 检查端口是否为 8081

---

## 📖 更多信息

详细文档请查看：
- `BROKER_API_REST_CLIENT_GUIDE.md` - 完整使用指南
- `API_PUBLISH_KICK_IMPLEMENTATION.md` - API实现文档
- `API_URL_AND_LOGGING_UPDATE.md` - URL和日志更新说明

---

**提示**: 所有测试用例都已包含在 `broker.api.rest` 文件中，直接点击 "Send Request" 即可测试！

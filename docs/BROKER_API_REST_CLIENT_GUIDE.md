# Broker HTTP API - VS Code REST Client 使用指南

## 概述

`broker.api.rest` 文件是用于在 VS Code 中测试 Broker HTTP API 的 REST Client 测试脚本。它提供了完整的API端点测试用例，包括正常场景和错误场景。

## 前置要求

### 1. 安装 VS Code REST Client 扩展

在 VS Code 中安装 **REST Client** 扩展：

1. 打开 VS Code
2. 按 `Ctrl+Shift+X` 打开扩展市场
3. 搜索 "REST Client"
4. 安装由 **Huachao Mao** 开发的 REST Client 扩展

或者直接访问：https://marketplace.visualstudio.com/items?itemName=humao.rest-client

### 2. 启动 Broker

确保 Broker 已经启动并启用了 HTTP API：

```bash
# 编译（API功能默认启用）
zig build broker -Dstatic-link=true --release=small

# 启动 Broker（启用API认证）
./zig-out/broker/linux-arm/mqtt_broker-musleabihf -api-token "testtoken12345"
```

## 配置变量

在 `broker.api.rest` 文件的顶部，有三个可配置的变量：

```http
@baseUrl = http://localhost:8081
@apiToken = testtoken12345
@contentType = application/json
```

### 变量说明

| 变量名 | 默认值 | 说明 |
|--------|--------|------|
| `baseUrl` | `http://localhost:8081` | Broker API服务器的地址和端口 |
| `apiToken` | `testtoken12345` | API认证token（与启动时的 `-api-token` 参数一致） |
| `contentType` | `application/json` | 默认内容类型 |

### 修改示例

如果您的Broker运行在不同的地址或使用了不同的token：

```http
@baseUrl = http://192.168.1.100:8081
@apiToken = mysecrettoken123
@contentType = application/json
```

## 使用方法

### 基本操作

1. **打开文件**：在 VS Code 中打开 `broker.api.rest`

2. **发送请求**：
   - 将光标放在某个请求上
   - 点击请求上方的 **"Send Request"** 链接
   - 或使用快捷键 `Ctrl+Alt+R` (Windows/Linux) / `Cmd+Option+R` (Mac)

3. **查看响应**：
   - 响应会显示在右侧的新标签页中
   - 包含状态码、响应头和响应体
   - JSON响应会自动格式化

### 示例截图

```
GET http://localhost:8081/api/stats
Authorization: Basic testtoken12345

[Send Request]  ← 点击这里
```

## API 端点列表

### GET 请求

| 编号 | 端点 | 说明 |
|------|------|------|
| 1 | `/api/stats` | 获取Broker统计信息 |
| 2 | `/api/clients` | 获取已连接客户端列表 |
| 3 | `/api/topics/<topic>` | 获取特定主题的订阅者信息 |
| 4 | `/api/configs` | 获取Broker配置参数 |

### POST 请求

| 编号 | 端点 | 说明 |
|------|------|------|
| 5-8 | `/api/publish/<topic>` | 发布消息到指定主题 |
| 9-10 | `/api/kick/<client>` | 踢掉指定客户端 |
| 11 | `/api/reset` | 重置Broker统计信息 |
| 12 | `/api/config/<item>` | 更新配置项 |

## 测试用例分类

### 1. 基础功能测试（1-12）

测试所有主要的API端点，包括：
- 获取统计信息
- 查看客户端列表
- 发布不同QoS级别的消息
- 发布保留消息
- 发布JSON格式消息
- 踢掉客户端
- 重置统计

### 2. 错误处理测试（13-16）

测试API的错误处理能力：
- 未授权访问（401）
- 错误的token（401）
- 不存在的路径（404）
- 不存在的客户端（404）

### 3. 高级测试场景（17-20）

更复杂的测试场景：
- 批量发布消息
- 不同QoS级别测试
- 特殊字符主题
- 空payload测试

### 4. 性能测试（21）

简单的性能测试用例，可以手动快速连续发送。

## 常见使用场景

### 场景 1: 快速验证API是否正常工作

```http
### 发送这个请求
GET {{baseUrl}}/api/stats
Authorization: Basic {{apiToken}}
```

**预期响应**：
```json
{
  "conns": 0,
  "rx_msgs": 0,
  "tx_msgs": 0,
  "rx_bytes": 0,
  "tx_bytes": 0,
  "retained": 0,
  "subs": 0,
  "uptime_seconds": 123
}
```

### 场景 2: 测试消息发布

```http
### 发布一条测试消息
POST {{baseUrl}}/api/publish/test/topic?qos=1&retain=false
Authorization: Basic {{apiToken}}
Content-Type: text/plain

Hello from REST Client!
```

**预期响应**：
```json
{
  "status": "success",
  "message": "Published to topic 'test/topic'"
}
```

### 场景 3: 查看当前连接的客户端

```http
### 获取客户端列表
GET {{baseUrl}}/api/clients
Authorization: Basic {{apiToken}}
```

**预期响应**：
```json
[
  {"id": "client1", "ip": "192.168.1.100"},
  {"id": "client2", "ip": "192.168.1.101"}
]
```

### 场景 4: 踢掉异常客户端

```http
### 踢掉指定的客户端
POST {{baseUrl}}/api/kick/client123
Authorization: Basic {{apiToken}}
```

**预期响应**：
```json
{
  "status": "success",
  "message": "Client 'client123' kicked"
}
```

## 调试技巧

### 1. 查看请求详情

REST Client会在VS Code的输出面板中显示详细的请求信息：
- 完整的请求URL
- 请求头
- 请求体
- 响应时间

### 2. 保存响应到文件

在响应标签页中：
1. 右键点击响应内容
2. 选择 "Save Response Body"
3. 保存到文件以便后续分析

### 3. 使用环境变量

如果需要频繁切换环境，可以使用VS Code的环境变量功能：

创建 `.env` 文件：
```env
BROKER_HOST=localhost
BROKER_PORT=8081
API_TOKEN=testtoken12345
```

在 `.rest` 文件中引用：
```http
@baseUrl = http://{{$dotenv BROKER_HOST}}:{{$dotenv BROKER_PORT}}
@apiToken = {{$dotenv API_TOKEN}}
```

### 4. 批量测试

可以使用 REST Client 的批量执行功能：
1. 选择多个请求
2. 右键选择 "Run All Requests"
3. 查看所有请求的结果

## 自定义测试

### 添加新的测试用例

在文件末尾添加新的请求块：

```http
### 我的自定义测试

POST {{baseUrl}}/api/publish/my/custom/topic?qos=1
Authorization: Basic {{apiToken}}
Content-Type: application/json

{
  "custom_field": "value"
}
```

### 修改现有测试

直接编辑对应的请求块，修改URL、headers或body。

## 常见问题

### Q1: 收到 401 Unauthorized 错误

**原因**：Token不正确或未提供

**解决方法**：
1. 检查 `@apiToken` 变量的值是否与Broker启动时的 `-api-token` 参数一致
2. 确保每个请求都包含了 `Authorization: Basic {{apiToken}}` 头

### Q2: 收到 404 Not Found 错误

**原因**：URL路径错误或API未启用

**解决方法**：
1. 检查 `@baseUrl` 是否正确
2. 确认Broker启动时启用了API（默认启用）
3. 确认使用的是正确的路径（所有路径都以 `/api` 开头）

### Q3: 连接被拒绝

**原因**：Broker未启动或端口不正确

**解决方法**：
1. 确认Broker正在运行
2. 检查 `@baseUrl` 中的端口是否正确（默认8081）
3. 检查防火墙设置

### Q4: JSON响应未格式化

**解决方法**：
1. 确保响应的 Content-Type 是 `application/json`
2. REST Client 会自动格式化JSON响应
3. 如果仍未格式化，尝试重新发送请求

## 高级功能

### 1. 请求链（Request Chaining）

可以在请求之间传递数据：

```http
### 第一个请求：获取统计
GET {{baseUrl}}/api/stats
Authorization: Basic {{apiToken}}

### 第二个请求：使用第一个请求的结果
# （REST Client支持从响应中提取变量）
```

### 2. 预请求脚本

REST Client不支持JavaScript脚本，但可以使用：
- 环境变量
- 文件变量
- 动态变量（如时间戳）

### 3. 认证自动化

对于Basic Auth，可以直接在URL中包含：

```http
GET http://user:pass@localhost:8081/api/stats
```

但对于我们的API，使用token更合适。

## 最佳实践

1. **保持变量集中**：所有配置都在文件顶部
2. **添加注释**：每个测试用例都有清晰的说明
3. **分组管理**：相关测试放在一起
4. **定期清理**：删除不再需要的测试用例
5. **版本控制**：将 `.rest` 文件纳入Git管理（但不包含敏感token）

## 相关资源

- [REST Client 扩展文档](https://github.com/Huachao/vscode-restclient)
- [HTTP 协议规范](https://httpwg.org/specs/)
- [wolfMQTT 文档](https://www.wolfssl.com/docs/wolfmqtt/)

## 总结

`broker.api.rest` 文件提供了一个完整、易用的API测试环境，无需额外的工具即可在VS Code中进行全面的API测试。通过合理使用变量和分组，可以高效地测试和调试Broker的HTTP API功能。

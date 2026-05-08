# 静态文件服务功能

## 概述

wolfMQTT Broker 的 HTTP API 服务器现在支持静态文件服务功能，可以方便地提供 Web 前端页面、CSS、JavaScript 等资源文件。

## 配置

### 设置静态文件目录

在 `MqttBroker` 结构中，可以通过 `static_dir` 字段指定静态文件目录：

```c
MqttBroker broker;
broker.static_dir = "/path/to/static/files";  // 自定义目录
// 或者
broker.static_dir = NULL;  // 使用默认目录 "./www"
```

### HTTP Basic 认证配置

HTTP API 服务器使用 HTTP Basic 认证，默认凭据为：
- 用户名: `admin`
- 密码: `22182666`

可以通过以下方式修改：

```c
// 方法 1: 直接设置 Broker 字段
XSTRNCPY(broker.http_username, "myuser", sizeof(broker.http_username) - 1);
XSTRNCPY(broker.http_password, "mypassword", sizeof(broker.http_password) - 1);

// 方法 2: 使用 API 函数
MqttBrokerApi_SetCredentials(api_ctx, "myuser", "mypassword");
```

也可以通过 HTTP API 配置接口动态修改：

```bash
curl -u admin:22182666 -X POST http://localhost:8081/api/configs \
  -d "http_username=newuser&http_password=newpass"
```

### 默认行为

- 如果 `static_dir` 为 `NULL`（默认值），则使用当前目录下的 `www` 文件夹
- 如果 `http_username` 为空字符串，则禁用认证（允许匿名访问）
- 启动时会在日志中输出静态目录配置信息

## 功能特性

### 1. 自动 MIME 类型识别

根据文件扩展名自动设置正确的 `Content-Type`：

| 扩展名 | MIME Type |
|--------|-----------|
| .html, .htm | text/html |
| .css | text/css |
| .js | application/javascript |
| .json | application/json |
| .png | image/png |
| .jpg, .jpeg | image/jpeg |
| .gif | image/gif |
| .svg | image/svg+xml |
| .txt | text/plain |
| .xml | application/xml |
| .pdf | application/pdf |
| .ico | image/x-icon |
| .woff, .woff2 | font/woff, font/woff2 |
| .ttf | font/ttf |
| .mp4, .webm | video/mp4, video/webm |
| .mp3, .wav | audio/mpeg, audio/wav |
| 其他 | application/octet-stream |

### 2. 目录自动映射到 index.html

当请求的 URL 对应一个文件夹时，自动尝试访问该文件夹下的 `index.html`：

```
请求: GET /about/
实际访问: www/about/index.html
```

### 3. 流式文件传输

- 使用 4KB 缓冲区分块读取和发送文件
- 不会将整个文件加载到内存，适合大文件和嵌入式系统
- 边读边发，降低内存占用

### 4. 安全性保护

- **防止目录遍历攻击**：拒绝包含 `..` 的路径
- **拒绝绝对路径**：只允许相对路径
- **文件存在性检查**：文件不存在时返回 404

## 使用示例

### 目录结构

```
www/
├── index.html          # 首页
├── style.css           # CSS 文件
├── app.js              # JavaScript 文件
├── images/
│   └── logo.png        # 图片文件
└── about/
    └── index.html      # 关于页面
```

### 访问方式

假设 API 服务器运行在端口 8081，使用默认凭据（admin:22182666）：

```bash
# 访问首页（需要 HTTP Basic 认证）
curl -u admin:22182666 http://localhost:8081/
# 实际访问: www/index.html

# 访问 CSS 文件
curl -u admin:22182666 http://localhost:8081/style.css
# 实际访问: www/style.css

# 访问图片
curl -u admin:22182666 http://localhost:8081/images/logo.png
# 实际访问: www/images/logo.png

# 访问目录（自动映射到 index.html）
curl -u admin:22182666 http://localhost:8081/about/
# 实际访问: www/about/index.html

# API 请求（同样需要 HTTP Basic 认证）
curl -u admin:22182666 http://localhost:8081/api/stats
# 返回 JSON 格式的统计数据
```

**注意**: 
- `/api/*` 路径和静态文件都使用相同的 HTTP Basic 认证
- 认证信息通过 `Authorization: Basic <base64(username:password)>` 头传递

## 日志输出

### 启动时

```
INFO: API server started on port 8081, static directory: www
```

或自定义目录：

```
INFO: API server started on port 8081, static directory: /var/www/html
```

### 文件访问（DEBUG 级别）

成功服务文件：
```
DEBUG: Static file served: /about/ (2048 bytes, total_sent=2048)
```

检测到目录映射：
```
DEBUG: Directory detected, mapping to: www/about/index.html
```

文件未找到：
```
DEBUG: Static file not found: www/nonexistent.html
```

不安全路径被拒绝：
```
WARN: Static file request rejected (unsafe path): ../etc/passwd
```

## 路由规则

HTTP API 服务器的路由优先级：

1. `/api/*` 路径 → API 端点（stats, clients, topics, configs, publish, reset, kick）
2. 其他 GET 请求 → 静态文件服务
3. 未知 API 端点 → 返回 404

## 注意事项

1. **路径安全**：URL 路径不能以 `/` 开头，不能包含 `..`
2. **文件大小**：支持任意大小的文件，采用流式传输
3. **并发访问**：每个请求独立处理，支持并发访问
4. **HTTP Basic 认证**：所有 HTTP 请求（包括 API 和静态文件）都需要 HTTP Basic 认证
5. **禁用认证**：将 `http_username` 设置为空字符串可禁用认证
6. **错误处理**：
   - 认证失败 → 401 Unauthorized
   - 文件不存在 → 404 Not Found
   - 路径不安全 → 400 Bad Request
   - IO 错误 → 500 Internal Server Error

## 代码量统计

本次功能实现增加了约 **220 行代码**：

- MIME 类型映射表：~35 行
- 辅助函数（get_mime_type, is_safe_path, is_directory）：~40 行
- 静态文件处理函数（handle_static_file）：~120 行
- 路由逻辑修改：~10 行
- 头文件和初始化：~15 行

## 兼容性

- ✅ Windows 和 POSIX 系统兼容
- ✅ 支持所有常见 Web 文件类型
- ✅ 与现有 API 端点完全兼容
- ✅ 不影响 MQTT Broker 核心功能

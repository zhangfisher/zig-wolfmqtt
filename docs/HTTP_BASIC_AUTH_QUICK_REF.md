# HTTP Basic 认证快速参考

## 默认配置

- **用户名**: `admin`
- **密码**: `22182666`
- **端口**: `8081`

## curl 命令示例

### API 端点

```bash
# 获取统计信息
curl -u admin:22182666 http://localhost:8081/api/stats

# 获取客户端列表
curl -u admin:22182666 http://localhost:8081/api/clients

# 发布消息
curl -u admin:22182666 -X POST http://localhost:8081/api/publish/test/topic \
  -d "Hello World"

# 修改配置
curl -u admin:22182666 -X POST http://localhost:8081/api/configs \
  -d "http_username=newuser&http_password=newpass"
```

### 静态文件

```bash
# 访问首页
curl -u admin:22182666 http://localhost:8081/

# 访问 CSS 文件
curl -u admin:22182666 http://localhost:8081/style.css

# 访问目录（自动映射到 index.html）
curl -u admin:22182666 http://localhost:8081/about/
```

## C 代码示例

### 设置凭据

```c
#include "wolfmqtt/mqtt_broker.h"

MqttBroker broker;
MqttBroker_Init(&broker);

// 方法 1: 直接设置
XSTRNCPY(broker.http_username, "myuser", sizeof(broker.http_username) - 1);
XSTRNCPY(broker.http_password, "mypass", sizeof(broker.http_password) - 1);

// 方法 2: 使用 API 函数
MqttBrokerApi_SetCredentials(broker.api_ctx, "myuser", "mypass");

// 禁用认证
broker.http_username[0] = '\0';
```

### JavaScript (浏览器)

```javascript
// Fetch API
fetch('http://localhost:8081/api/stats', {
  headers: {
    'Authorization': 'Basic ' + btoa('admin:22182666')
  }
})
.then(res => res.json())
.then(data => console.log(data));

// XMLHttpRequest
var xhr = new XMLHttpRequest();
xhr.open('GET', 'http://localhost:8081/api/stats');
xhr.setRequestHeader('Authorization', 'Basic ' + btoa('admin:22182666'));
xhr.onload = function() {
  console.log(xhr.responseText);
};
xhr.send();
```

### Python

```python
import requests

# API 请求
response = requests.get(
    'http://localhost:8081/api/stats',
    auth=('admin', '22182666')
)
print(response.json())

# 静态文件
response = requests.get(
    'http://localhost:8081/index.html',
    auth=('admin', '22182666')
)
print(response.text)
```

## 常见错误

| 状态码 | 原因 | 解决方案 |
|--------|------|----------|
| 401 | 未提供认证或凭据错误 | 检查用户名和密码 |
| 404 | 文件或 API 端点不存在 | 检查 URL 路径 |
| 400 | 路径不安全（包含 `..`） | 使用合法路径 |

## 禁用认证

```bash
# 通过 API
curl -u admin:22182666 -X POST http://localhost:8081/api/configs \
  -d "http_username="

# C 代码
broker.http_username[0] = '\0';
```

禁用后，所有请求都不需要认证。

## 启用 TLS（生产环境推荐）

```c
broker.use_tls = 1;
broker.tls_cert = "/path/to/cert.pem";
broker.tls_key = "/path/to/key.pem";
```

然后使用 HTTPS：
```bash
curl -u admin:22182666 https://localhost:8081/api/stats --insecure
```

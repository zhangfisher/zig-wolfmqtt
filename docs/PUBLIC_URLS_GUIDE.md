# HTTP API 免认证 URL 配置指南

## 功能说明

HTTP API 支持配置免认证 URL 列表，访问这些 URL 时不需要提供 HTTP Basic 认证凭证。

**默认配置**：`/login.html` 默认在免认证列表中，无需额外配置即可公开访问。

## 使用方法

### 1. 在代码中配置免认证 URL

**注意**：`/login.html` 已默认配置为免认证 URL，如需添加其他 URL，可以使用以下方法：

```c
#include "wolfmqtt/mqtt_broker.h"

/* 定义免认证 URL 列表 */
const char* public_urls[] = {
    "/login.html",
    "/register.html",
    "/public/info",
    "/health",
    "/status"
};

/* 在初始化 API 后设置免认证 URL */
MqttBrokerApiContext api_ctx;
MqttBroker broker;

// ... broker 初始化代码 ...

// 初始化 API
MqttBrokerApi_Init(&broker, &api_ctx, 8080);

// 设置免认证 URL（4 个路径）
MqttBrokerApi_SetPublicUrls(&api_ctx, public_urls, 4);

// 或者清除所有免认证 URL
// MqttBrokerApi_SetPublicUrls(&api_ctx, NULL, 0);
```

### 2. 工作原理

- **匹配规则**：精确匹配 URL 路径（不包含查询参数）
- **认证跳过**：如果请求的 URL 在免认证列表中，直接跳过 HTTP Basic 认证
- **日志记录**：DEBUG 级别日志会记录免认证 URL 的访问

### 3. 示例场景

#### 场景 1：登录页面
```c
const char* auth_urls[] = {
    "/login.html",
    "/api/login"
};
MqttBrokerApi_SetPublicUrls(&api_ctx, auth_urls, 2);
```

访问 `http://192.168.116.217:8080/login.html` 不需要认证。

#### 场景 2：健康检查端点
```c
const char* health_urls[] = {
    "/health",
    "/status",
    "/ping"
};
MqttBrokerApi_SetPublicUrls(&api_ctx, health_urls, 3);
```

用于负载均衡器或监控系统定期检查服务状态。

#### 场景 3：静态资源
```c
const char* static_urls[] = {
    "/index.html",
    "/css/style.css",
    "/js/app.js",
    "/images/logo.png"
};
MqttBrokerApi_SetPublicUrls(&api_ctx, static_urls, 4);
```

允许公开访问静态网站资源。

## API 函数说明

### MqttBrokerApi_SetPublicUrls

```c
int MqttBrokerApi_SetPublicUrls(
    MqttBrokerApiContext* api_ctx,
    const char** urls,
    int count
);
```

**参数：**
- `api_ctx`: API 上下文指针
- `urls`: URL 路径字符串数组
- `count`: URL 数量

**返回值：**
- `MQTT_CODE_SUCCESS`: 成功
- `MQTT_CODE_ERROR_BAD_ARG`: 参数错误
- `MQTT_CODE_ERROR_MEMORY`: 内存分配失败

**注意：**
- `/login.html` 默认已配置为免认证 URL
- 每次调用会替换之前的配置（包括默认配置）
- 传入 `NULL` 和 `count=0` 可以清除所有免认证 URL
- URL 路径必须以 `/` 开头
- 不支持通配符或正则表达式

## 测试方法

### 使用 curl 测试

```bash
# 访问免认证 URL（不需要 -u 参数）
curl http://192.168.116.217:8080/login.html

# 访问需要认证的 URL（需要 -u 参数）
curl -u admin:22182666 http://192.168.116.217:8080/api/stats

# 访问需要认证但未提供凭证的 URL（返回 401）
curl http://192.168.116.217:8080/api/stats
```

### 使用 VS Code REST Client

```rest
### 访问免认证 URL（无需认证）
GET http://192.168.116.217:8080/login.html

###

### 访问需要认证的 URL
GET http://192.168.116.217:8080/api/stats
Authorization: Basic YWRtaW46MjIxODI2NjY=
```

## 注意事项

1. **安全性**：谨慎配置免认证 URL，确保不会暴露敏感信息
2. **路径匹配**：只支持精确匹配，`/api/login` 不会匹配 `/api/login/extra`
3. **查询参数**：URL 匹配不考虑查询参数，`/page?id=1` 和 `/page?id=2` 都会匹配 `/page`
4. **内存管理**：函数会自动复制 URL 字符串，调用者可以安全释放原始字符串数组
5. **动态更新**：可以随时调用此函数更新免认证 URL 列表

## 完整示例

```c
#include <stdio.h>
#include "wolfmqtt/mqtt_broker.h"

int main(void)
{
    MqttBroker broker;
    MqttBrokerApiContext api_ctx;
    
    /* 初始化 broker */
    MqttBroker_Init(&broker);
    
    /* 配置免认证 URL */
    const char* public_urls[] = {
        "/",              /* 首页 */
        "/login.html",    /* 登录页面 */
        "/register.html", /* 注册页面 */
        "/health",        /* 健康检查 */
        "/favicon.ico"    /* 网站图标 */
    };
    
    /* 启动 HTTP API */
    if (MqttBrokerApi_Init(&broker, &api_ctx, 8080) == MQTT_CODE_SUCCESS) {
        /* 设置免认证 URL */
        MqttBrokerApi_SetPublicUrls(&api_ctx, public_urls, 5);
        
        printf("HTTP API started on port 8080\n");
        printf("Public URLs configured:\n");
        for (int i = 0; i < 5; i++) {
            printf("  - %s\n", public_urls[i]);
        }
        
        /* 运行 broker 主循环 */
        // Broker_Run(&broker);
    }
    
    /* 清理资源 */
    MqttBrokerApi_Free(&api_ctx);
    MqttBroker_Free(&broker);
    
    return 0;
}
```

## 调试技巧

启用 DEBUG 日志可以看到免认证 URL 的访问记录：

```
[DEBUG] API Request GET /login.html from=192.168.1.100
[DEBUG] Public URL accessed without auth: /login.html
[DEBUG] API Response 200 GET /login.html to=192.168.1.100
```

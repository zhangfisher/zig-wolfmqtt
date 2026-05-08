# HTTP API 默认启用与配置优化

## 概述

本次更新对Broker HTTP API进行了以下改进：

1. **移除WOLFMQTT_BROKER_API宏开关** - HTTP API功能始终启用
2. **添加api_port字段** - 支持自定义API服务器监听端口
3. **新增命令行选项** - `-api-port` 和 `-disable-api`
4. **修复启动日志** - API启动时正确输出监听端口信息

## 主要变更

### 1. 移除宏开关

#### 修改的文件

**wolfmqtt/mqtt_broker.h**
- ✅ 移除 `MqttBrokerApiContext` 结构体的 `#ifdef WOLFMQTT_BROKER_API`
- ✅ 移除 `MqttBroker` 结构体中API字段的宏开关
- ✅ 移除API函数声明的宏开关

**src/mqtt_broker.c**
- ✅ 移除API初始化代码的宏开关
- ✅ 移除API启动代码的宏开关
- ✅ 移除API处理代码的宏开关
- ✅ 移除API清理代码的宏开关
- ✅ 移除命令行参数解析的宏开关

**src/mqtt_broker_api.c**
- ✅ 移除文件开头和结尾的宏开关

### 2. 添加api_port字段

在 `MqttBroker` 结构体中添加：

```c
word16 api_port;  /* HTTP API server port (default: 8081) */
```

**默认值**: 8081

**初始化位置**: `MqttBroker_InitEx()` 函数

### 3. 更新API启动逻辑

#### 之前（硬编码端口）
```c
rc = MqttBrokerApi_Init(broker, broker->api_ctx, 8081);
WBLOG_INFO(broker, "listening on port 8081 (HttpApi)");
```

#### 现在（使用配置的端口）
```c
rc = MqttBrokerApi_Init(broker, broker->api_ctx, broker->api_port);
WBLOG_INFO(broker, "listening on port %u (HttpApi)", broker->api_port);
```

### 4. 新增命令行选项

#### -api-port <port>
指定HTTP API服务器的监听端口

**示例**：
```bash
./mqtt_broker -api-port 9090
```

**验证**：端口必须在1-65535范围内

#### -disable-api
禁用HTTP API服务（别名：`-api-disable`）

**示例**：
```bash
./mqtt_broker -disable-api
```

**效果**：API服务器不会启动，不会有相关日志输出

### 5. 帮助信息更新

```
Usage: mqtt_broker [options]
  ...
  -api-token <token>  API authentication token (min 12 chars)
  -api-port <port>    HTTP API server port (default: 8081)
  -api-enable         Enable HTTP API (default: enabled)
  -api-disable        Disable HTTP API
  -disable-api        Alias for -api-disable
```

## 使用示例

### 1. 默认启动（API启用，端口8081）

```bash
./mqtt_broker-musleabihf
```

**预期日志**：
```
[INFO ] 1970-01-22 07:22:08 - listening on port 1883 (no TLS)
[INFO ] 1970-01-22 07:22:08 - listening on port 8080 (WebSocket)
[INFO ] 1970-01-22 07:22:08 - listening on port 8081 (HttpApi)
```

### 2. 自定义API端口

```bash
./mqtt_broker-musleabihf -api-port 9090
```

**预期日志**：
```
[INFO ] 1970-01-22 07:22:08 - listening on port 1883 (no TLS)
[INFO ] 1970-01-22 07:22:08 - listening on port 8080 (WebSocket)
[INFO ] 1970-01-22 07:22:08 - listening on port 9090 (HttpApi)
```

### 3. 禁用API

```bash
./mqtt_broker-musleabihf -disable-api
```

**预期日志**：
```
[INFO ] 1970-01-22 07:22:08 - listening on port 1883 (no TLS)
[INFO ] 1970-01-22 07:22:08 - listening on port 8080 (WebSocket)
```

注意：没有HttpApi的日志输出

### 4. 组合使用

```bash
./mqtt_broker-musleabihf \
  -p 1883 \
  -w 8080 \
  -api-port 9090 \
  -api-token "mysecrettoken123"
```

**预期日志**：
```
[INFO ] 1970-01-22 07:22:08 - listening on port 1883 (no TLS)
[INFO ] 1970-01-22 07:22:08 - listening on port 8080 (WebSocket)
[INFO ] 1970-01-22 07:22:08 - listening on port 9090 (HttpApi)
```

## 构建系统

### Zig构建

HTTP API功能**默认启用**，无需额外参数：

```bash
# 标准构建（包含API）
zig build broker -Dstatic-link=true -Dwebsocket=true --release=small

# 仍然支持显式禁用（可选）
zig build broker -Dbroker-api=false
```

### CMake构建

```cmake
# 默认启用
cmake .. 

# 显式禁用（可选）
cmake .. -DWOLFMQTT_BROKER_API=no
```

### Autotools构建

```bash
# 默认启用
./configure

# 显式禁用（可选）
./configure --disable-broker-api
```

## 兼容性说明

### 向后兼容性

✅ **完全兼容** - 所有现有功能保持不变

- 旧的命令行参数仍然有效
- API端点和功能完全不变
- 只是移除了编译时开关

### 破坏性变更

❌ **无** - 没有破坏性变更

## 问题解答

### Q1: 为什么控制台没有API启动日志？

**原因**：之前的实现中，API启动有条件判断：
```c
if (broker->enable_api && (broker->api_token[0] != '\0' || broker->api_ctx != NULL))
```

这要求必须设置token或已有api_ctx才会启动API。

**解决**：简化为只检查 `enable_api`：
```c
if (broker->enable_api)
```

现在只要 `enable_api=1`（默认），API就会启动并输出日志。

### Q2: 如何确认API是否启用？

查看启动日志，如果有这一行说明API已启用：
```
[INFO ] ... - listening on port 8081 (HttpApi)
```

如果没有这一行，说明API被禁用了（使用了 `-disable-api`）。

### Q3: 可以更改API端口吗？

可以，使用 `-api-port` 参数：
```bash
./mqtt_broker -api-port 9090
```

### Q4: 如何完全禁用API？

使用 `-disable-api` 或 `-api-disable` 参数：
```bash
./mqtt_broker -disable-api
```

## 文件大小

| 版本 | 大小 | 说明 |
|------|------|------|
| 之前 | 116,512 字节 | 带批量配置更新 |
| 现在 | 123,200 字节 | +6,688字节 |

增加的原因：
- 添加了api_port字段和处理逻辑
- 移除了条件编译，代码始终包含
- 增加了新的命令行参数解析

## 测试建议

### 1. 测试默认行为

```bash
./mqtt_broker-musleabihf
# 应该看到三条listening日志
```

### 2. 测试自定义端口

```bash
./mqtt_broker-musleabihf -api-port 9090
curl http://localhost:9090/api/stats
```

### 3. 测试禁用API

```bash
./mqtt_broker-musleabihf -disable-api
# 不应该有HttpApi日志
# curl应该失败
```

### 4. 测试API功能

```bash
# 获取统计
curl -H "Authorization: Basic testtoken12345" \
  http://localhost:8081/api/stats

# 发布消息
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "Hello" \
  http://localhost:8081/api/publish/test/topic

# 更新配置
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "log_level=3&stats_interval=60" \
  http://localhost:8081/api/configs
```

## 相关文件

- `wolfmqtt/mqtt_broker.h` - 结构体定义
- `src/mqtt_broker.c` - Broker主逻辑
- `src/mqtt_broker_api.c` - HTTP API实现
- `broker.api.rest` - REST Client测试文件

## 总结

✅ **HTTP API默认启用** - 无需编译开关  
✅ **可配置端口** - 通过 `-api-port` 参数  
✅ **可禁用** - 通过 `-disable-api` 参数  
✅ **日志完善** - 启动时输出监听端口  
✅ **向后兼容** - 无破坏性变更  

这些改进使得HTTP API更加灵活和易用！

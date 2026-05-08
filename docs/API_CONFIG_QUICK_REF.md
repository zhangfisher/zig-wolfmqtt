# HTTP API 配置 - 快速参考

## 命令行选项

### -api-port <port>
设置HTTP API服务器监听端口（默认：8081）

```bash
./mqtt_broker -api-port 9090
```

### -disable-api / -api-disable
禁用HTTP API服务

```bash
./mqtt_broker -disable-api
```

### -api-token <token>
设置API认证token（最少12字符）

```bash
./mqtt_broker -api-token "mysecrettoken123"
```

### -api-enable
显式启用API（默认已启用，通常不需要）

```bash
./mqtt_broker -api-enable
```

## 启动日志

### API启用（默认）
```
[INFO ] ... - listening on port 1883 (no TLS)
[INFO ] ... - listening on port 8080 (WebSocket)
[INFO ] ... - listening on port 8081 (HttpApi)  ← API已启动
```

### API禁用
```
[INFO ] ... - listening on port 1883 (no TLS)
[INFO ] ... - listening on port 8080 (WebSocket)
                                                  ← 没有HttpApi日志
```

## 常用组合

### 标准启动
```bash
./mqtt_broker
# API: 启用, 端口: 8081
```

### 自定义API端口
```bash
./mqtt_broker -api-port 9090
# API: 启用, 端口: 9090
```

### 禁用API
```bash
./mqtt_broker -disable-api
# API: 禁用
```

### 完整配置
```bash
./mqtt_broker \
  -p 1883 \
  -w 8080 \
  -api-port 9090 \
  -api-token "secret12345678"
```

## API测试

### 健康检查
```bash
curl http://localhost:8081/api/stats
```

### 带认证
```bash
curl -H "Authorization: Basic testtoken12345" \
  http://localhost:8081/api/stats
```

### 发布消息
```bash
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "Hello" \
  http://localhost:8081/api/publish/test/topic
```

### 批量配置
```bash
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "log_level=3&stats_interval=60" \
  http://localhost:8081/api/configs
```

## 常见问题

### Q: 为什么看不到API日志？
A: 可能使用了 `-disable-api` 参数

### Q: 如何更改API端口？
A: 使用 `-api-port <port>` 参数

### Q: 默认端口是多少？
A: 8081

### Q: 可以完全移除API吗？
A: 编译时使用 `-Dbroker-api=false`（Zig）或相应构建系统的禁用选项

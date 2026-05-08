# HTTP API /clients 端点增强

## 概述

增强了 `GET /api/clients` 端点的返回内容，添加了客户端传输类型（type）字段。

## 修改内容

### 修改前
```json
[
  {"id":"client1","ip":"127.0.0.1"},
  {"id":"client2","ip":"192.168.1.100"}
]
```

### 修改后
```json
[
  {"id":"client1","ip":"127.0.0.1","type":"TCP"},
  {"id":"client2","ip":"192.168.1.100","type":"WebSocket"},
  {"id":"client3","ip":"10.0.0.5","type":"TLS"}
]
```

## 新增字段说明

### type 字段
- **类型**: string
- **可能的值**:
  - `"TCP"`: 标准TCP连接
  - `"TLS"`: TLS加密连接（如果启用了TLS支持）
  - `"WebSocket"`: WebSocket连接（如果启用了WebSocket支持）

## 使用示例

### 获取所有客户端列表

```bash
curl -u admin:password http://localhost:8080/api/clients
```

**响应示例**:
```json
[
  {"id":"mqtt-client-1","ip":"127.0.0.1","type":"TCP"},
  {"id":"web-client-1","ip":"192.168.1.100","type":"WebSocket"},
  {"id":"secure-client-1","ip":"10.0.0.5","type":"TLS"}
]
```

### JavaScript 处理示例

```javascript
fetch('http://localhost:8080/api/clients', {
  headers: {
    'Authorization': 'Basic ' + btoa('admin:password')
  }
})
.then(response => response.json())
.then(clients => {
  clients.forEach(client => {
    console.log(`Client: ${client.id}`);
    console.log(`  IP: ${client.ip}`);
    console.log(`  Type: ${client.type}`);
    
    // 根据类型显示不同图标
    if (client.type === 'WebSocket') {
      console.log('  🌐 WebSocket client');
    } else if (client.type === 'TLS') {
      console.log('  🔒 Secure TLS client');
    } else {
      console.log('  📡 TCP client');
    }
  });
});
```

### Python 处理示例

```python
import requests
import json

response = requests.get(
    'http://localhost:8080/api/clients',
    auth=('admin', 'password')
)

clients = response.json()

for client in clients:
    print(f"Client ID: {client['id']}")
    print(f"  IP Address: {client['ip']}")
    print(f"  Connection Type: {client['type']}")
    print()
```

## 应用场景

### 1. 客户端监控仪表板
在Web仪表板中显示客户端列表时，可以根据 `type` 字段显示不同的图标或颜色：
- TCP: 蓝色图标
- TLS: 绿色锁图标（表示安全连接）
- WebSocket: 紫色 globe 图标

### 2. 连接类型统计
统计不同类型连接的客户端数量：

```javascript
const typeCounts = clients.reduce((acc, client) => {
  acc[client.type] = (acc[client.type] || 0) + 1;
  return acc;
}, {});

console.log('Connection Types:', typeCounts);
// Output: { TCP: 5, WebSocket: 3, TLS: 2 }
```

### 3. 过滤特定类型的客户端
只显示WebSocket客户端：

```javascript
const wsClients = clients.filter(c => c.type === 'WebSocket');
```

### 4. 安全审计
识别未使用TLS加密的客户端连接，提醒用户升级：

```python
insecure_clients = [c for c in clients if c['type'] == 'TCP']
if insecure_clients:
    print(f"Warning: {len(insecure_clients)} clients using unencrypted connections")
```

## 技术实现

### 代码位置
- **文件**: `src/mqtt_broker_api.c`
- **函数**: `handle_get_clients()`
- **行号**: 约619-656行

### 关键修改
1. 增加了 `transport_type` 变量存储传输类型
2. 调用 `BrokerTransport_GetName(bc)` 获取客户端的传输类型名称
3. 在JSON格式化字符串中添加 `"type":"%s"` 字段
4. 将响应缓冲区从2048字节增加到4096字节以容纳更多数据

### 依赖函数
- `BrokerTransport_GetName()`: 定义在 `src/mqtt_broker_transport.c`
  - 返回 "TCP"、"TLS" 或 "WebSocket"
  - 根据客户端的传输层类型自动识别

## 兼容性

- ✅ 向后兼容：添加了新字段，不影响现有解析逻辑
- ✅ 与 `$sys/broker/clients/connected` 消息格式一致
- ✅ 支持所有传输层类型（TCP、TLS、WebSocket）

## 注意事项

1. **缓冲区大小**: 响应缓冲区已增加到4096字节，可以容纳约100个客户端的信息
2. **性能影响**: 每次调用都会遍历所有客户端并获取传输类型，对性能影响极小
3. **空值处理**: 如果客户端ID为空，会返回空字符串 `""`

## 相关功能

此修改与以下功能保持一致：
- `$sys/broker/clients/connected` 系统消息
- `$sys/broker/clients/disconnected` 系统消息
- Broker日志中的客户端连接信息

所有这些功能现在都使用相同的传输类型标识方式，确保数据一致性。

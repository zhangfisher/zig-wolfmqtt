# MQTT v5 响应主题和关联数据支持

## 概述

wolfMQTT Broker 已完整支持 MQTT v5 协议中的**响应主题(Response Topic)**和**关联数据(Correlation Data)**属性，这使得实现请求/响应模式变得更加容易。

## 功能说明

### 响应主题 (Response Topic)
- **属性类型**: `MQTT_PROP_RESP_TOPIC` (值: 8)
- **数据类型**: 字符串
- **用途**: 客户端在发布请求消息时，可以指定一个响应主题，指示服务端应该将响应发布到哪个主题

### 关联数据 (Correlation Data)
- **属性类型**: `MQTT_PROP_CORRELATION_DATA` (值: 9)
- **数据类型**: 二进制数据
- **用途**: 用于关联请求和响应，通常是一个唯一的标识符（如 UUID）

## 使用场景

### 1. 请求/响应模式

```
客户端 A                     Broker                     服务端 B
   |                           |                          |
   |--PUBLISH requests/proc      |                          |
   |   + Response Topic:         |                          |
   |     "responses/A"           |                          |
   |   + Correlation Data:        |                          |
   |     "req-123"                |                          |
   |                           |                          |
   |                           |--PUBLISH requests/proc    |
   |                           |   + Response Topic:         |
   |                           |     "responses/A"           |
   |                           |   + Correlation Data:        |
   |                           |     "req-123"                |
   |                           |                          |
   |                           |                          |--处理请求
   |                           |                          |
   |                           |--PUBLISH responses/A       |
   |                           |   + Correlation Data:        |
   |                           |     "req-123"                |
   |                           |   + Payload: 响应数据        |
   |                          |                          |
   |--收到响应 (匹配关联数据)---|                          |
```

### 2. 工作流程

1. **客户端订阅响应主题**: `responses/my_client_id`
2. **客户端发布请求**: 
   - 主题: `requests/service`
   - 响应主题: `responses/my_client_id`
   - 关联数据: `unique-request-id`
3. **服务端订阅请求主题**: `requests/#`
4. **服务端收到请求**:
   - 检查 `Response Topic` 属性
   - 处理请求
   - 向 `Response Topic` 发布响应
   - 包含相同的 `Correlation Data`
5. **客户端匹配关联数据**: 将响应对应到原始请求

## Broker 支持

### 自动转发属性

Broker 会自动将所有 MQTT v5 属性（包括响应主题和关联数据）转发给订阅者，无需额外配置。

### 调试日志

当收到带有响应主题或关联数据的消息时，broker 会输出日志：

```
broker: PUBLISH has Response Topic: responses/client123
broker: PUBLISH has Correlation Data: 8 bytes
```

## 测试示例

编译并运行测试程序：

```bash
cd tests
gcc -o v5_response_example v5_response_example.c \
    -I../wolfmqtt -L../zig-out/lib -lmqtt -lpthread

./v5_response_example localhost 1883
```

## 代码示例

### 客户端 - 发送请求

```c
#include "mqtt_client.h"
#include "mqtt_packet.h"

// 创建属性列表
MqttProp* props = NULL;

// 添加响应主题
MqttProp* resp_topic = MqttProps_Add(&props, MQTT_PROP_RESP_TOPIC);
resp_topic->data_str.str = "responses/my_client";
resp_topic->data_str.len = strlen("responses/my_client");

// 添加关联数据
const char* corr_id = "req-12345";
MqttProp* corr_data = MqttProps_Add(&props, MQTT_PROP_CORRELATION_DATA);
corr_data->data_bin.data = (byte*)corr_id;
corr_data->data_bin.len = strlen(corr_id);

// 发布消息
MqttPublish publish = {0};
publish.topic_name = "requests/service";
publish.buffer = (byte*)"请处理这个请求";
publish.total_len = 20;
publish.qos = MQTT_QOS_1;
publish.props = props;

MqttClient_Publish(&client, &publish);

// 清理
MqttProps_Free(props);
```

### 服务端 - 处理请求并发送响应

```c
// 在消息回调中处理
int msg_callback(MqttClient* client, MqttMessage* msg,
    byte* new_data, word32 new_data_len, MqttPublishResponse* response)
{
    // 检查是否有响应主题
    if (msg->props != NULL) {
        MqttProp* resp_topic = MqttProps_Find(msg->props, MQTT_PROP_RESP_TOPIC);
        MqttProp* corr_data = MqttProps_Find(msg->props, MQTT_PROP_CORRELATION_DATA);
        
        if (resp_topic != NULL && resp_topic->data_str.str != NULL) {
            // 这是一条请求消息，需要发送响应
            
            // 创建响应消息
            MqttPublish resp_publish = {0};
            resp_publish.topic_name = resp_topic->data_str.str;
            resp_publish.buffer = (byte*)"响应数据";
            resp_publish.total_len = strlen("响应数据");
            resp_publish.qos = MQTT_QOS_0;
            
            // 复制关联数据到响应
            MqttProp* resp_props = NULL;
            if (corr_data != NULL && corr_data->data_bin.data != NULL) {
                MqttProp* resp_corr = MqttProps_Add(&resp_props, MQTT_PROP_CORRELATION_DATA);
                resp_corr->data_bin.data = corr_data->data_bin.data;
                resp_corr->data_bin.len = corr_data->data_bin.len;
            }
            resp_publish.props = resp_props;
            
            // 发送响应
            MqttClient_Publish(&client, &resp_publish);
            
            // 清理
            MqttProps_Free(resp_props);
        }
    }
    
    return 0;
}
```

## 属性限制

根据 MQTT v5 规范：

1. **响应主题**:
   - 必须是 UTF-8 编码的字符串
   - 不能包含通配符（`+` 或 `#`）
   - 长度限制: 1 到 65535 字节

2. **关联数据**:
   - 二进制数据
   - 长度限制: 1 到 65535 字节
   - 建议使用 UUID 或唯一标识符

## 最佳实践

1. **响应主题命名**: 使用 `responses/{client_id}` 模式确保每个客户端有独立的响应主题
2. **关联数据**: 使用 UUID 或其他唯一标识符，确保能正确匹配请求和响应
3. **QoS 级别**: 响应消息通常使用 QoS 0（至少一次），除非需要保证送达
4. **错误处理**: 服务端应检查响应主题是否存在，避免向无效主题发布

## 故障排查

### 问题：响应没有收到

**可能原因**:
1. 客户端没有订阅响应主题
2. 响应主题名称拼写错误
3. 关联数据不匹配

**调试方法**:
- 启用 broker 日志查看属性是否正确传递
- 使用 MQTT 监控工具（如 MQTT Explorer）查看消息属性
- 检查客户端的订阅列表

### 问题：关联数据丢失

**可能原因**:
1. 服务端没有正确复制关联数据属性
2. 属性在编码/解码过程中被释放

**解决方案**:
- 确保在发送响应时重新创建属性对象
- 不要直接引用原始属性的内存，而是复制数据

## 部署

编译后的 broker 已自动包含此功能，无需额外配置：

```bash
zig-out/broker/arm/linux/gnueabihf/mqtt_broker
```

## 参考

- [MQTT v5.0 规范](https://docs.oasis-open.org/mqtt/mqtt/v5.0/mqtt-v5.0.html)
- wolfMQTT 文档
- MQTT v5 属性列表

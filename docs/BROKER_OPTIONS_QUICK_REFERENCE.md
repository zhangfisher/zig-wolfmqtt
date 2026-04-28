# BrokerOptions 快速参考

## 结构体定义

```c
typedef struct BrokerOptions {
    // 缓冲区配置
    word16 rx_buf_sz;           // 接收缓冲区大小(字节)
    word16 tx_buf_sz;           // 发送缓冲区大小(字节)
    word16 timeout_ms;          // 网络超时(毫秒)
    word16 listen_backlog;      // 监听队列长度
    
    // 容量限制
    word16 max_clients;         // 最大客户端数
    word16 max_subs;            // 最大订阅数
    word16 max_retained;        // 最大保留消息数
    word16 max_pending_wills;   // 最大待处理遗嘱数
    
    // 字符串长度限制
    word16 max_client_id_len;   // 客户端ID最大长度
    word16 max_username_len;    // 用户名最大长度
    word16 max_password_len;    // 密码最大长度
    word16 max_filter_len;      // 主题过滤器最大长度
    word16 max_topic_len;       // 主题名称最大长度
    word16 max_payload_len;     // 消息负载最大大小
    word16 max_will_payload_len;// 遗嘱负载最大大小
} BrokerOptions;
```

## API 函数

### 获取选项
```c
BrokerOptions opts;
int rc = MqttBroker_GetOptions(&broker, &opts);
```

### 设置选项
```c
BrokerOptions opts;
// ... 修改 opts ...
int rc = MqttBroker_SetOptions(&broker, &opts);
```

**注意：** 必须在 `MqttBroker_Start()` 之前调用

## 常用配置示例

### 嵌入式设备（资源受限）
```c
opts.max_clients = 4;
opts.max_subs = 16;
opts.rx_buf_sz = 2048;
opts.tx_buf_sz = 2048;
```

### 高性能服务器
```c
opts.max_clients = 100;
opts.max_subs = 500;
opts.rx_buf_sz = 16384;
opts.tx_buf_sz = 16384;
opts.timeout_ms = 3000;
```

### 编译时禁用功能

功能通过编译时宏控制：

```c
#define WOLFMQTT_BROKER_NO_RETAINED   // 禁用保留消息
#define WOLFMQTT_BROKER_NO_WILDCARDS  // 禁用通配符
```

## 默认值

| 参数 | 默认值 |
|------|--------|
| rx_buf_sz | 4096 |
| tx_buf_sz | 4096 |
| timeout_ms | 1000 |
| listen_backlog | 128 |
| max_clients | 8 |
| max_subs | 32 |
| max_retained | 16 |
| max_pending_wills | 4 |
| max_client_id_len | 64 |
| max_username_len | 64 |
| max_password_len | 64 |
| max_filter_len | 128 |
| max_topic_len | 128 |
| max_payload_len | 4096 |
| max_will_payload_len | 256 |

## 完整使用流程

```c
#include "wolfmqtt/mqtt_broker.h"

int main(void) {
    MqttBroker broker;
    BrokerOptions opts;

    // 1. 初始化（使用简化的 Init API）
    MqttBroker_Init(&broker);

    // 2. 获取当前配置
    MqttBroker_GetOptions(&broker, &opts);

    // 3. 修改配置
    opts.max_clients = 16;
    opts.rx_buf_sz = 8192;

    // 4. 应用配置
    MqttBroker_SetOptions(&broker, &opts);

    // 5. 启动broker
    MqttBroker_Start(&broker);

    // 6. 运行...
    MqttBroker_Run(&broker);

    // 7. 清理
    MqttBroker_Free(&broker);

    return 0;
}
```

## 错误码

- `MQTT_CODE_SUCCESS` - 成功
- `MQTT_CODE_ERROR_BAD_ARG` - 参数无效（空指针或值超出范围）
- `MQTT_CODE_ERROR_MUTEX` - broker正在运行，无法修改配置

## 注意事项

⚠️ **重要：**
1. 必须在 broker 启动前设置选项
2. 缓冲区大小不能为0
3. 容量限制不能为0
4. 运行时无法修改配置
5. 静态内存模式下，运行时值不能超过编译时宏定义
6. 功能通过编译时宏 `WOLFMQTT_BROKER_NO_xxx` 控制

## 编译时功能控制

```c
// 启用某个功能
opts.features |= BROKER_FEATURE_RETAINED;

// 禁用某个功能
opts.features &= ~BROKER_FEATURE_WILDCARDS;

// 检查功能是否启用
if (opts.features & BROKER_FEATURE_AUTH) {
    // 身份认证已启用
}

// 启用所有功能
opts.features = 0xFF;

// 禁用所有功能
opts.features = 0x00;
```

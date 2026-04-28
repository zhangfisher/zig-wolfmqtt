# Broker 运行时配置选项

## 概述

wolfMQTT broker 现在支持通过 `BrokerOptions` 结构体进行运行时配置。这允许您在不重新编译库的情况下配置缓冲区大小、容量限制和功能标志。

## BrokerOptions 结构体

```c
typedef struct BrokerOptions {
    /* 缓冲区大小配置 */
    word16 rx_buf_sz;           /* 每个客户端的接收缓冲区大小(字节) */
    word16 tx_buf_sz;           /* 每个客户端的发送缓冲区大小(字节) */
    word16 timeout_ms;          /* 网络超时时间(毫秒) */
    word16 listen_backlog;      /* 监听套接字的等待队列长度 */
    
    /* 容量限制 */
    word16 max_clients;         /* 最大并发客户端数量 */
    word16 max_subs;            /* 最大订阅数量 */
    word16 max_retained;        /* 最大保留消息数量 */
    word16 max_pending_wills;   /* 最大待处理遗嘱消息数量 */
    
    /* 字符串长度限制 */
    word16 max_client_id_len;   /* 客户端ID最大长度 */
    word16 max_username_len;    /* 用户名最大长度 */
    word16 max_password_len;    /* 密码最大长度 */
    word16 max_filter_len;      /* 主题过滤器最大长度 */
    word16 max_topic_len;       /* 主题名称最大长度 */
    word16 max_payload_len;     /* 消息负载最大大小(字节) */
    word16 max_will_payload_len;/* 遗嘱消息负载最大大小(字节) */
} BrokerOptions;
```

## API 函数

### MqttBroker_GetOptions

获取当前 broker 配置选项：

```c
int MqttBroker_GetOptions(MqttBroker* broker, BrokerOptions* options);
```

**参数：**
- `broker` - 指向已初始化的 MqttBroker
- `options` - 指向要填充的 BrokerOptions 结构体

**返回值：** 成功返回 `MQTT_CODE_SUCCESS`，否则返回错误码

### MqttBroker_SetOptions

配置 broker 选项：

```c
int MqttBroker_SetOptions(MqttBroker* broker, const BrokerOptions* options);
```

**参数：**
- `broker` - 指向已初始化的 MqttBroker（必须未运行）
- `options` - 指向包含期望值的 BrokerOptions 结构体

**返回值：** 成功返回 `MQTT_CODE_SUCCESS`，否则返回错误码

**重要提示：** 只能在调用 `MqttBroker_Start()` 或 `MqttBroker_Run()` 之前设置选项。尝试在 broker 运行时更改选项将返回 `MQTT_CODE_ERROR_MUTEX`。

## 使用示例

```c
#include "wolfmqtt/mqtt_broker.h"

void configure_broker(MqttBroker* broker)
{
    BrokerOptions opts;
    int rc;
    
    /* 获取当前（默认）选项 */
    rc = MqttBroker_GetOptions(broker, &opts);
    if (rc != MQTT_CODE_SUCCESS) {
        /* 处理错误 */
    }
    
    /* 自定义选项 */
    opts.max_clients = 32;           /* 支持更多客户端 */
    opts.max_subs = 128;             /* 更多订阅 */
    opts.rx_buf_sz = 8192;           /* 更大的缓冲区 */
    opts.tx_buf_sz = 8192;
    
    /* 应用配置 */
    rc = MqttBroker_SetOptions(broker, &opts);
    if (rc != MQTT_CODE_SUCCESS) {
        /* 处理错误 */
    }
}

int main(void)
{
    MqttBroker broker;

    /* 初始化 broker（使用简化的 Init API） */
    MqttBroker_Init(&broker);

    /* 在启动前配置选项 */
    configure_broker(&broker);

    /* 现在启动 broker */
    MqttBroker_Start(&broker);

    /* 运行 broker 循环... */

    /* 清理 */
    MqttBroker_Free(&broker);
    return 0;
}
```

## 默认值

默认值由 `mqtt_broker.h` 中的编译时宏定义：

| 选项 | 默认宏 | 默认值 |
|------|--------|--------|
| rx_buf_sz | BROKER_RX_BUF_SZ | 4096 |
| tx_buf_sz | BROKER_TX_BUF_SZ | 4096 |
| timeout_ms | BROKER_TIMEOUT_MS | 1000 |
| listen_backlog | BROKER_LISTEN_BACKLOG | 128 |
| max_clients | BROKER_MAX_CLIENTS | 8 |
| max_subs | BROKER_MAX_SUBS | 32 |
| max_retained | BROKER_MAX_RETAINED | 16 |
| max_pending_wills | BROKER_MAX_PENDING_WILLS | 4 |
| max_client_id_len | BROKER_MAX_CLIENT_ID_LEN | 64 |
| max_username_len | BROKER_MAX_USERNAME_LEN | 64 |
| max_password_len | BROKER_MAX_PASSWORD_LEN | 64 |
| max_filter_len | BROKER_MAX_FILTER_LEN | 128 |
| max_topic_len | BROKER_MAX_TOPIC_LEN | 128 |
| max_payload_len | BROKER_MAX_PAYLOAD_LEN | 4096 |
| max_will_payload_len | BROKER_MAX_WILL_PAYLOAD_LEN | 256 |

这些默认值可以通过在包含 `mqtt_broker.h` 之前定义宏来在编译时覆盖，或者在运行时使用 `MqttBroker_SetOptions()` 进行配置。

## 内存考虑

### 静态内存模式 (WOLFMQTT_STATIC_MEMORY)

使用静态内存模式时，数组在编译时根据宏值分配：

```c
BrokerClient clients[BROKER_MAX_CLIENTS];
BrokerSub subs[BROKER_MAX_SUBS];
```

运行时 `max_clients` 和 `max_subs` 选项控制实际使用多少个预分配的槽位。在静态内存模式下，将运行时值设置为高于编译时宏的值无效。

### 动态内存模式

在动态内存模式下，运行时选项直接控制内存分配。每个客户端分配：
- `rx_buf_sz` 字节的接收缓冲区
- `tx_buf_sz` 字节的发送缓冲区
- 额外的字符串内存（client_id、username等），最多达到配置的最大长度

## 从编译时宏迁移

以前，所有 broker 限制都由编译时宏控制。新的运行时选项提供了灵活性，同时保持向后兼容性：

**之前（仅编译时）：**
```c
#define BROKER_MAX_CLIENTS 16
#define BROKER_RX_BUF_SZ 8192
// 任何更改都需要重新编译
```

**之后（运行时可配置）：**
```c
BrokerOptions opts;
MqttBroker_GetOptions(&broker, &opts);
opts.max_clients = 16;
opts.rx_buf_sz = 8192;
MqttBroker_SetOptions(&broker, &opts);
// 无需重新编译
```

编译时宏仍然作为默认值存在，并确定静态内存模式下的数组大小。

## 测试

参见 `tests/test_broker_options_runtime.c` 获取使用运行时选项的完整示例。

构建和运行：
```bash
gcc -o test_opts tests/test_broker_options_runtime.c \
    -I. -Lsrc -lwolfmqtt
./test_opts
```

## 最佳实践

1. **在初始化后立即配置**：在调用 `MqttBroker_Init()` 之后、`MqttBroker_Start()` 之前设置选项
2. **验证选项范围**：确保缓冲区大小不为零，容量限制合理
3. **谨慎禁用功能**：仅在确实不需要时禁用功能标志
4. **考虑内存限制**：在嵌入式系统中，根据可用内存调整缓冲区大小
5. **记录配置**：保存使用的配置以便调试和重现问题

## 常见问题

### Q: 可以在 broker 运行时更改选项吗？
A: 不可以。必须在启动 broker 之前设置所有选项。尝试在运行时更改将返回 `MQTT_CODE_ERROR_MUTEX`。

### Q: 静态内存模式和动态内存模式有什么区别？
A: 静态内存模式在编译时分配固定大小的数组，运行时选项仅控制使用多少。动态内存模式在运行时根据选项分配内存。

### Q: 如何禁用特定功能？
A: 功能通过编译时宏控制。在编译前定义相应的宏来禁用功能：
```c
#define WOLFMQTT_BROKER_NO_RETAINED   // 禁用保留消息
#define WOLFMQTT_BROKER_NO_WILDCARDS  // 禁用通配符
```

### Q: 默认启用哪些功能？
A: 默认情况下，所有功能都启用。可以通过定义 `WOLFMQTT_BROKER_NO_xxx` 宏在编译时禁用特定功能。

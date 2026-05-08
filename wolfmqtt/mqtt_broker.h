/* mqtt_broker.h
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 *
 * This file is part of wolfMQTT.
 *
 * wolfMQTT is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfMQTT is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#ifndef WOLFMQTT_BROKER_H
#define WOLFMQTT_BROKER_H

#include "wolfmqtt/mqtt_types.h"
#include "wolfmqtt/mqtt_socket.h"
#include "wolfmqtt/mqtt_client.h"
#include "wolfmqtt/mqtt_broker_transport.h"

#ifdef __cplusplus
    extern "C" {
#endif

#ifdef WOLFMQTT_BROKER

/* -------------------------------------------------------------------------- */
/* Socket type abstraction - override for non-POSIX platforms                  */
/* -------------------------------------------------------------------------- */
#ifndef BROKER_SOCKET_T
    #define BROKER_SOCKET_T        int
#endif
#ifndef BROKER_SOCKET_INVALID
    #define BROKER_SOCKET_INVALID  (-1)
#endif

/* -------------------------------------------------------------------------- */
/* epoll support (Linux I/O multiplexing)                                      */
/* -------------------------------------------------------------------------- */
#ifdef WOLFMQTT_BROKER_EPOLL
    #include <sys/epoll.h>
    #ifndef BROKER_EPOLL_MAX_EVENTS_DEFAULT
        #define BROKER_EPOLL_MAX_EVENTS_DEFAULT 64  /* Default max events per epoll_wait */
    #endif
#endif

/* -------------------------------------------------------------------------- */
/* Time abstraction - override for platforms without time.h                    */
/* -------------------------------------------------------------------------- */
#ifndef WOLFMQTT_BROKER_TIME_T
    #define WOLFMQTT_BROKER_TIME_T  unsigned long
#endif
/* Note: WOLFMQTT_BROKER_GET_TIME_S() default is defined in mqtt_broker.c
 * because it depends on <time.h> which is only included for POSIX builds.
 * Override this macro for custom platforms. */

/* -------------------------------------------------------------------------- */
/* Log levels                                                                  */
/* -------------------------------------------------------------------------- */
#include "wolfmqtt/logger.h"

/* Log callback function type */
typedef void (*MqttBrokerLogCb)(LogLevel level, const char* format, va_list args);

#ifndef BROKER_LOG_LEVEL_DEFAULT
    #define BROKER_LOG_LEVEL_DEFAULT  LOG_LEVEL_INFO
#endif

/* -------------------------------------------------------------------------- */
/* Buffer and limit defaults (缓冲区和限制默认值宏)                            */
/* 这些宏作为 BrokerOptions 的默认值，也可用于静态内存模式的数组大小          */
/* -------------------------------------------------------------------------- */
#ifndef BROKER_RX_BUF_SZ
    #define BROKER_RX_BUF_SZ       4096
#endif
#ifndef BROKER_TX_BUF_SZ
    #define BROKER_TX_BUF_SZ       4096
#endif
#ifndef BROKER_TIMEOUT_MS
    #define BROKER_TIMEOUT_MS      1000
#endif
#ifndef BROKER_LISTEN_BACKLOG
    #define BROKER_LISTEN_BACKLOG  128
#endif

/* 静态分配限制宏 */
#ifndef BROKER_MAX_CLIENTS
    #define BROKER_MAX_CLIENTS       8
#endif
#ifndef BROKER_MAX_SUBS
    #define BROKER_MAX_SUBS          32
#endif
#ifndef BROKER_MAX_CLIENT_ID_LEN
    #define BROKER_MAX_CLIENT_ID_LEN 64
#endif
#ifndef BROKER_MAX_USERNAME_LEN
    #define BROKER_MAX_USERNAME_LEN  64
#endif
#ifndef BROKER_MAX_PASSWORD_LEN
    #define BROKER_MAX_PASSWORD_LEN  64
#endif
#ifndef BROKER_MAX_FILTER_LEN
    #define BROKER_MAX_FILTER_LEN    128
#endif
#ifndef BROKER_MAX_RETAINED
    #define BROKER_MAX_RETAINED      16
#endif
#ifndef BROKER_MAX_TOPIC_LEN
    #define BROKER_MAX_TOPIC_LEN     128
#endif
#ifndef BROKER_MAX_PAYLOAD_LEN
    #define BROKER_MAX_PAYLOAD_LEN   4096
#endif
#ifndef BROKER_MAX_WILL_PAYLOAD_LEN
    #define BROKER_MAX_WILL_PAYLOAD_LEN 256
#endif
#ifndef BROKER_MAX_PENDING_WILLS
    #define BROKER_MAX_PENDING_WILLS 4
#endif

/* -------------------------------------------------------------------------- */
/* Topic Alias limits (主题别名限制)                                            */
/* -------------------------------------------------------------------------- */
#ifndef BROKER_MAX_TOPIC_ALIASES
    #define BROKER_MAX_TOPIC_ALIASES 16  /* 每个客户端最大主题别名数 */
#endif

/* Broker 默认值宏定义 */
#define MQTT_BROKER_DEFAULTS \
    .rx_buf_sz = BROKER_RX_BUF_SZ, \
    .tx_buf_sz = BROKER_TX_BUF_SZ, \
    .timeout_ms = BROKER_TIMEOUT_MS, \
    .listen_backlog = BROKER_LISTEN_BACKLOG, \
    .max_clients = BROKER_MAX_CLIENTS, \
    .max_subs = BROKER_MAX_SUBS, \
    .max_retained = BROKER_MAX_RETAINED, \
    .max_pending_wills = BROKER_MAX_PENDING_WILLS, \
    .max_client_id_len = BROKER_MAX_CLIENT_ID_LEN, \
    .max_username_len = BROKER_MAX_USERNAME_LEN, \
    .max_password_len = BROKER_MAX_PASSWORD_LEN, \
    .max_filter_len = BROKER_MAX_FILTER_LEN, \
    .max_topic_len = BROKER_MAX_TOPIC_LEN, \
    .max_payload_len = BROKER_MAX_PAYLOAD_LEN, \
    .max_will_payload_len = BROKER_MAX_WILL_PAYLOAD_LEN, \
    .default_session_expiry_interval = 180, \
    .stats_interval = 20, \
    WOLFMQTT_V5_FLOW_CONTROL_DEFAULTS

/* MQTT 5 流控默认值宏 */
#ifdef WOLFMQTT_V5
    #define WOLFMQTT_V5_FLOW_CONTROL_DEFAULTS \
    .max_packet_size = 0, \
    .topic_alias_max = BROKER_MAX_TOPIC_ALIASES, \
    .max_qos = 2, \
    .retain_avail = 1, \
    .wildcard_sub_avail = 1, \
    .sub_id_avail = 1, \
    .shared_sub_avail = 1
#else
    #define WOLFMQTT_V5_FLOW_CONTROL_DEFAULTS
#endif

/* -------------------------------------------------------------------------- */
/* Broker statistics (Broker统计数据)                                           */
/* -------------------------------------------------------------------------- */
typedef struct BrokerStats {
    word32 conns;                  /* 当前活跃连接数 */
    word32 rx_msgs;                /* 接收的消息总数 */
    word32 tx_msgs;                /* 发送的消息总数 */
    word32 rx_bytes;               /* 接收字节数 */
    word32 tx_bytes;               /* 发送字节数 */
    word32 retained;               /* 当前保留消息数 */
    word32 subs;                   /* 当前活跃订阅数 */
    WOLFMQTT_BROKER_TIME_T start;  /* Broker 启动时间 */
} BrokerStats;

/* -------------------------------------------------------------------------- */
/* Broker control commands (Broker控制命令)                                    */
/* -------------------------------------------------------------------------- */
#ifndef WOLFMQTT_BROKER_COMMANDS
#define WOLFMQTT_BROKER_COMMANDS
#endif

/* Broker 命令结果码 */
typedef enum {
    BROKER_CMD_SUCCESS = 0,
    BROKER_CMD_ERROR_INVALID = -1,    /* 无效命令 */
    BROKER_CMD_ERROR_ARGS = -2,       /* 参数错误 */
    BROKER_CMD_ERROR_NOT_FOUND = -3,  /* 客户端未找到 */
    BROKER_CMD_ERROR_DENIED = -4,     /* 权限拒绝 */
    BROKER_CMD_ERROR_FAILED = -5      /* 执行失败 */
} BrokerCommandResult;

/* Broker 命令响应 */
typedef struct BrokerCommandResponse {
    const char* command;      /* 执行的命令 */
    BrokerCommandResult code; /* 结果码 */
    const char* message;      /* 人类可读消息 */
    const char* detail;       /* 额外详情（可选） */
} BrokerCommandResponse;

/* -------------------------------------------------------------------------- */
/* Feature toggles (opt-in: define WOLFMQTT_BROKER_xxx to enable)             */
/* -------------------------------------------------------------------------- */
#ifndef WOLFMQTT_BROKER_RETAINED
    #define WOLFMQTT_BROKER_RETAINED
#endif
/* WOLFMQTT_BROKER_WILL is always enabled */
#ifndef WOLFMQTT_BROKER_WILDCARDS
    #define WOLFMQTT_BROKER_WILDCARDS
#endif
#ifndef WOLFMQTT_BROKER_AUTH
    #define WOLFMQTT_BROKER_AUTH
#endif
#ifndef WOLFMQTT_BROKER_INSECURE
    #define WOLFMQTT_BROKER_INSECURE
#endif

/* -------------------------------------------------------------------------- */
/* Forward declarations                                                        */
/* -------------------------------------------------------------------------- */
typedef struct MqttBroker MqttBroker;

#ifdef ENABLE_MQTT_WEBSOCKET
typedef struct MqttWebSocketContext MqttWebSocketContext;
#endif

/* HTTP API context structure (always enabled) */
typedef struct MqttBrokerApiContext {
    MqttBroker* broker;
    BROKER_SOCKET_T http_listen_sock;  /* Unified HTTP/WebSocket listener socket */
    word16 http_port;                   /* Unified HTTP/WebSocket port (default: 8080) */
    byte use_api;
    char http_username[64];  /* HTTP Basic authentication username */
    char http_password[64];  /* HTTP Basic authentication password */
    
    /* Public URLs that don't require authentication */
    char** public_urls;      /* Array of URL path strings */
    int public_url_count;    /* Number of public URLs */
} MqttBrokerApiContext;

/* -------------------------------------------------------------------------- */
/* Broker client lifecycle callbacks (客户端生命周期回调)                      */
/* -------------------------------------------------------------------------- */
typedef int (*MqttBrokerConnectCb)(MqttBroker* broker,
    BROKER_SOCKET_T sock, const char* client_id, const char* ip);
typedef int (*MqttBrokerDisconnectCb)(MqttBroker* broker,
    BROKER_SOCKET_T sock, const char* client_id, const char* ip, int reason);

/* -------------------------------------------------------------------------- */
/* Broker network callback types                                               */
/* -------------------------------------------------------------------------- */
typedef int (*MqttBrokerNet_ListenCb)(void* ctx, BROKER_SOCKET_T* sock,
    word16 port, int backlog);
typedef int (*MqttBrokerNet_AcceptCb)(void* ctx, BROKER_SOCKET_T listen_sock,
    BROKER_SOCKET_T* client_sock);
typedef int (*MqttBrokerNet_ReadCb)(void* ctx, BROKER_SOCKET_T sock,
    byte* buf, int buf_len, int timeout_ms);
typedef int (*MqttBrokerNet_WriteCb)(void* ctx, BROKER_SOCKET_T sock,
    const byte* buf, int buf_len, int timeout_ms);
typedef int (*MqttBrokerNet_CloseCb)(void* ctx, BROKER_SOCKET_T sock);

typedef struct MqttBrokerNet {
    MqttBrokerNet_ListenCb  listen;
    MqttBrokerNet_AcceptCb  accept;
    MqttBrokerNet_ReadCb    read;
    MqttBrokerNet_WriteCb   write;
    MqttBrokerNet_CloseCb   close;
    void*                   ctx;
} MqttBrokerNet;

/* -------------------------------------------------------------------------- */
/* Broker client tracking                                                      */
/* -------------------------------------------------------------------------- */

/* Topic alias mapping entry (主题别名映射条目) */
#ifdef WOLFMQTT_V5
typedef struct BrokerTopicAlias {
#ifdef WOLFMQTT_STATIC_MEMORY
    byte    in_use;
    char    topic[BROKER_MAX_TOPIC_LEN];  /* 主题名 */
#else
    char*   topic;                         /* 主题名（动态分配） */
    word16  topic_len;                     /* 主题名长度 */
    struct BrokerTopicAlias* next;         /* 链表下一个节点 */
#endif
    word16  alias_id;                      /* 别名ID (1-65535) */
} BrokerTopicAlias;
#endif

typedef struct BrokerClient {
#ifdef WOLFMQTT_STATIC_MEMORY
    byte    in_use;
    char    client_id[BROKER_MAX_CLIENT_ID_LEN];
#ifdef WOLFMQTT_BROKER_AUTH
    char    username[BROKER_MAX_USERNAME_LEN];
    char    password[BROKER_MAX_PASSWORD_LEN];
#endif
    byte    tx_buf[BROKER_TX_BUF_SZ];
    byte    rx_buf[BROKER_RX_BUF_SZ];
    char    will_topic[BROKER_MAX_TOPIC_LEN];
    byte    will_payload[BROKER_MAX_WILL_PAYLOAD_LEN];
#else
    char*   client_id;
#ifdef WOLFMQTT_BROKER_AUTH
    char*   username;
    char*   password;
#endif
    byte*   tx_buf;
    byte*   rx_buf;
    int     tx_buf_len;
    int     rx_buf_len;
    char*   will_topic;
    byte*   will_payload;
    struct BrokerClient* next;
#endif
    BROKER_SOCKET_T sock;
    char    client_ip[64]; /* Client IP address string */
    byte    protocol_level;
    word16  keep_alive_sec;
    WOLFMQTT_BROKER_TIME_T last_rx;
    byte    clean_session;
    byte    connected;       /* set after successful CONNECT handshake */
    byte    has_will;
    word16  will_payload_len;
    MqttQoS will_qos;
    byte    will_retain;
    word32  will_delay_sec;     /* v5 Will Delay Interval (seconds) */
    /* Session persistence (会话持久化 - 适用于 MQTT 3.1.1 和 MQTT 5) */
    word32  session_expiry_interval; /* Session Expiry Interval (seconds, 0 = session ends on disconnect) */
    WOLFMQTT_BROKER_TIME_T disconnect_time; /* When client disconnected (for session expiry calculation) */
#ifdef WOLFMQTT_V5
    word16  topic_alias_maximum;     /* v5 Topic Alias Maximum (客户端支持的最大别名数) */
    word16  receive_maximum;         /* v5 Receive Maximum (客户端未确认消息上限) */
    word16  inflight_count;          /* 当前未确认的 QoS 1/2 消息数 */
    word32  max_packet_size;         /* v5 Maximum Packet Size (客户端最大包大小) */
#ifdef WOLFMQTT_STATIC_MEMORY
    BrokerTopicAlias topic_aliases[BROKER_MAX_TOPIC_ALIASES]; /* 主题别名映射表 */
#else
    BrokerTopicAlias* topic_aliases; /* 主题别名映射表（动态分配链表） */
#endif
#endif
#ifdef WOLFMQTT_BROKER_WILL
#ifdef WOLFMQTT_V5
    MqttProp* will_props;           /* v5 Will Properties (User Properties, etc.) */
#endif
#endif
    MqttNet net;
    MqttClient client;
    struct MqttBroker* broker;  /* back-pointer to parent broker context */
    
    /* Unified transport layer */
    struct BrokerTransport transport;
    
#ifdef ENABLE_MQTT_TLS
    byte    tls_handshake_done;
#endif
#ifdef ENABLE_MQTT_WEBSOCKET
    byte    is_websocket;    /* 是否为 WebSocket 连接 (for backward compatibility) */
#endif
} BrokerClient;

/* -------------------------------------------------------------------------- */
/* Broker subscription tracking                                                */
/* -------------------------------------------------------------------------- */
typedef struct BrokerSub {
#ifdef WOLFMQTT_STATIC_MEMORY
    byte    in_use;
    char    filter[BROKER_MAX_FILTER_LEN];
    char    client_id[BROKER_MAX_CLIENT_ID_LEN]; /* For session persistence */
#else
    char*   filter;
    char*   client_id; /* For session persistence */
    struct BrokerSub* next;
#endif
    struct BrokerClient* client; /* NULL if client disconnected (session persisted) */
    MqttQoS qos;
    /* Session persistence (会话持久化 - 适用于 MQTT 3.1.1 和 MQTT 5) */
    word32  session_expiry_interval; /* Session expiry interval in seconds (0 = session ends on disconnect) */
    WOLFMQTT_BROKER_TIME_T disconnect_time; /* When client disconnected (for session expiry calculation) */
#ifdef WOLFMQTT_V5
    byte    retain_handling;      /* v5 Retain Handling: 0=Send retained, 1=Send without retain, 2=Don't send */
    byte    no_local;            /* v5 No Local: 1=Don't receive own messages, 0=Receive all */
    byte    rap;                  /* v5 Retain As Published: 1=Keep original retain flag */

    /* 共享订阅字段 (Shared Subscription) */
    byte    is_shared;           /* 是否为共享订阅 */
    byte    rr_index;            /* 轮询索引 (Round-Robin index) */
#ifdef WOLFMQTT_STATIC_MEMORY
    char    share_group[BROKER_MAX_FILTER_LEN];  /* 共享组名 */
#else
    char*   share_group;         /* 共享组名 (动态分配) */
#endif
#endif
} BrokerSub;

/* -------------------------------------------------------------------------- */
/* Retained message store                                                      */
/* -------------------------------------------------------------------------- */
#ifdef WOLFMQTT_BROKER_RETAINED
typedef struct BrokerRetainedMsg {
#ifdef WOLFMQTT_STATIC_MEMORY
    byte    in_use;
    char    topic[BROKER_MAX_TOPIC_LEN];
    byte    payload[BROKER_MAX_PAYLOAD_LEN];
#else
    char*   topic;
    byte*   payload;
    struct BrokerRetainedMsg* next;
#endif
    word32  payload_len;
    byte    qos;                        /* Original QoS of the retained message */
    WOLFMQTT_BROKER_TIME_T store_time;  /* when stored (seconds) */
    word32  expiry_sec;                 /* v5 message expiry (0=none) */
} BrokerRetainedMsg;
#endif /* WOLFMQTT_BROKER_RETAINED */

/* -------------------------------------------------------------------------- */
/* Pending will messages (v5 Will Delay Interval)                              */
/* -------------------------------------------------------------------------- */
typedef struct BrokerPendingWill {
#ifdef WOLFMQTT_STATIC_MEMORY
    byte    in_use;
    char    client_id[BROKER_MAX_CLIENT_ID_LEN];
    char    topic[BROKER_MAX_TOPIC_LEN];
    byte    payload[BROKER_MAX_WILL_PAYLOAD_LEN];
#else
    char*   client_id;
    char*   topic;
    byte*   payload;
    struct BrokerPendingWill* next;
#endif
    word16  payload_len;
    MqttQoS qos;
    byte    retain;
    WOLFMQTT_BROKER_TIME_T publish_time; /* absolute time to publish */
} BrokerPendingWill;

/* -------------------------------------------------------------------------- */
/* Broker context                                                              */
/* -------------------------------------------------------------------------- */
typedef struct MqttBroker {
    BROKER_SOCKET_T listen_sock;
    word16  port;
    int     running;
    byte    log_level;         /* 日志级别 */
    MqttBrokerLogCb log;       /* 日志回调函数 (NULL则使用默认输出) */
    const char* username;  /* Authentication username (NULL = no auth) */
    const char* password;  /* Authentication password (NULL = no auth) */
    MqttBrokerNet net;
    word16  next_packet_id;

#ifdef WOLFMQTT_BROKER_EPOLL
    int     epoll_fd;          /* epoll file descriptor */
    struct epoll_event* epoll_events; /* Event array for epoll_wait */
    int     epoll_max_events;  /* Maximum events per epoll_wait (configurable) */
#endif

    /* Buffer sizes (缓冲区大小配置) */
    word16 rx_buf_sz;           /* 每个客户端的接收缓冲区大小(字节) */
    word16 tx_buf_sz;           /* 每个客户端的发送缓冲区大小(字节) */
    word16 timeout_ms;          /* 网络超时时间(毫秒) */
    word16 listen_backlog;      /* 监听套接字的等待队列长度 */

    /* Capacity limits (容量限制) */
    word16 max_clients;         /* 最大并发客户端数量 */
    word16 max_subs;            /* 最大订阅数量 */
    word16 max_retained;        /* 最大保留消息数量 */
    word16 max_pending_wills;   /* 最大待处理遗嘱消息数量 */

    /* String length limits (字符串长度限制) */
    word16 max_client_id_len;   /* 客户端ID最大长度 */
    word16 max_username_len;    /* 用户名最大长度 */
    word16 max_password_len;    /* 密码最大长度 */
    word16 max_filter_len;      /* 主题过滤器最大长度 */
    word16 max_topic_len;       /* 主题名称最大长度 */
    word16 max_payload_len;     /* 消息负载最大大小(字节) */
    word16 max_will_payload_len;/* 遗嘱消息负载最大大小(字节) */

    /* MQTT 5 Flow control settings (MQTT 5 流控设置) */
    word32 max_packet_size;     /* Broker最大包大小(0=无限制) */
    word16 topic_alias_max;     /* Broker主题别名最大值 */
    /* Session persistence defaults (会话持久化默认值) */
    word32 default_session_expiry_interval; /* 默认会话过期间隔(秒), 当客户端未指定Session Expiry Interval且clean=0时使用 */
    /* Statistics settings (统计设置) */
    word32 stats_interval;      /* 统计消息发送间隔(秒), 0=禁用, 默认=5 */

#ifdef ENABLE_MQTT_TLS
    BROKER_SOCKET_T listen_sock_tls; /* TLS listener socket */
    word16       port_tls;           /* TLS port (default 8883) */
    WOLFSSL_CTX* tls_ctx;
    const char*  tls_cert;     /* Server certificate file path */
    const char*  tls_key;      /* Server private key file path */
    const char*  tls_ca;       /* CA cert for mutual auth (optional) */
    byte         use_tls;
    byte         tls_version;  /* 0=auto (v23), 12=TLS 1.2, 13=TLS 1.3 */
    byte         tls_ctx_owned; /* 1 if BrokerTls_Init created tls_ctx */
#endif
#ifdef ENABLE_MQTT_WEBSOCKET
    byte            enable_ws;      /* Enable WebSocket on unified HTTP port (default: 1) */
#endif
    /* Dynamic memory allocation (always used) */
    BrokerClient* clients;
    BrokerSub*    subs;
    BrokerRetainedMsg* retained;
    BrokerPendingWill* pending_wills;
    BrokerStats stats;          /* 统计数据 */
    byte enable_stats;          /* 统计功能启用标志 */
    WOLFMQTT_BROKER_TIME_T last_stats_time; /* 上次发送统计消息的时间 */

    /* 连接/断开回调 (可选) */
    MqttBrokerConnectCb    on_connect;
    MqttBrokerDisconnectCb on_disconnect;
    
    byte enable_http;               /* Enable HTTP server (default: 1) */
    word16 http_port;               /* Unified HTTP/WebSocket server port (default: 8080) */
    const char* static_dir;         /* Static files directory (NULL = "./www") */
    /* HTTP API support (always enabled) */
    MqttBrokerApiContext* api_ctx;  /* HTTP API context */
    char http_username[64];         /* HTTP Basic authentication username */
    char http_password[64];         /* HTTP Basic authentication password */
} MqttBroker;

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

/* Initialize the broker context with default POSIX network callbacks (使用默认POSIX网络回调初始化broker) */
WOLFMQTT_API int MqttBroker_Init(MqttBroker* broker);

/* Initialize the broker context with custom network callbacks (使用自定义网络回调初始化broker) */
WOLFMQTT_API int MqttBroker_InitEx(MqttBroker* broker, MqttBrokerNet* net);

/* Get broker statistics (获取broker统计数据) */
WOLFMQTT_API int MqttBroker_GetStats(MqttBroker* broker, BrokerStats* stats);

/* Reset broker statistics (重置broker统计数据, 保留start时间) */
WOLFMQTT_API int MqttBroker_ResetStats(MqttBroker* broker);

/* Run the broker main loop (blocking) */
WOLFMQTT_API int MqttBroker_Run(MqttBroker* broker);

/* Execute a single iteration of the broker loop (for embedded main loops) */
WOLFMQTT_API int MqttBroker_Step(MqttBroker* broker);

/* Signal the broker loop to stop */
WOLFMQTT_API int MqttBroker_Stop(MqttBroker* broker);

/* Clean up broker resources */
WOLFMQTT_API int MqttBroker_Free(MqttBroker* broker);

/* Start the broker (listen + TLS init). Call once before MqttBroker_Step().
 * For embedded systems that use a cooperative main loop with Step(). */
WOLFMQTT_API int MqttBroker_Start(MqttBroker* broker);

/* Publish message from external source (e.g., HTTP API) */
WOLFMQTT_API int BrokerPublish_Message(MqttBroker* broker, const char* topic,
                                       const byte* payload, word16 payload_len,
                                       MqttQoS qos, byte retain);

/* Kick/disconnect a client by client_id or IP address */
WOLFMQTT_API int BrokerKick_Client(MqttBroker* broker, const char* client_identifier);

#ifdef ENABLE_MQTT_WEBSOCKET
/* Add WebSocket client after successful handshake (for unified port mode) */
WOLFMQTT_API int MqttBroker_AddWebSocketClient(MqttBroker* broker, BROKER_SOCKET_T sock);
#endif

/* wolfIP backend initializer.
 * wolfIP_stack is a (struct wolfIP*) pointer to the wolfIP stack instance. */
#ifdef WOLFMQTT_WOLFIP
WOLFMQTT_API int MqttBrokerNet_wolfIP_Init(MqttBrokerNet* net,
    void* wolfIP_stack);
#endif

/* Default POSIX backend initializer.
 * Only available when WOLFMQTT_BROKER_CUSTOM_NET is NOT defined. */
#if !defined(WOLFMQTT_WOLFIP) && !defined(WOLFMQTT_BROKER_CUSTOM_NET)
WOLFMQTT_API int MqttBrokerNet_Init(MqttBrokerNet* net);
#endif

/* CLI wrapper interface */
WOLFMQTT_API int wolfmqtt_broker(int argc, char** argv);

/* Broker HTTP API functions (always enabled) */
/* Initialize the broker HTTP API */
WOLFMQTT_API int MqttBrokerApi_Init(MqttBroker* broker, MqttBrokerApiContext* api_ctx, word16 port);

/* Set HTTP Basic authentication credentials */
WOLFMQTT_API int MqttBrokerApi_SetCredentials(MqttBrokerApiContext* api_ctx, const char* username, const char* password);

/* Set public URLs that don't require authentication */
WOLFMQTT_API int MqttBrokerApi_SetPublicUrls(MqttBrokerApiContext* api_ctx, const char** urls, int count);

/* Process API events (should be called from main broker loop) */
WOLFMQTT_API int MqttBrokerApi_Process(MqttBrokerApiContext* api_ctx);

/* Cleanup API resources */
WOLFMQTT_API void MqttBrokerApi_Free(MqttBrokerApiContext* api_ctx);

#ifdef WOLFMQTT_BROKER_EPOLL
/* Set epoll max events (must be called before MqttBroker_Start) */
WOLFMQTT_API int MqttBroker_SetEpollMaxEvents(MqttBroker* broker, int max_events);
#endif

#endif /* WOLFMQTT_BROKER */

#ifdef __cplusplus
    } /* extern "C" */
#endif

#endif /* WOLFMQTT_BROKER_H */

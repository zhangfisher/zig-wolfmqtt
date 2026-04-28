/* MQTT v5 会话过期间隔示例
 *
 * 本示例演示如何使用 MQTT v5 的会话过期间隔 (Session Expiry Interval)
 * 实现持久会话和会话过期功能。
 *
 * 编译：gcc -o v5_session_expiry_example v5_session_expiry_example.c \
 *       -I../../wolfmqtt -L../../zig-out/lib -lmqtt -lpthread
 * 运行：./v5_session_expiry_example <broker_ip> <port> <session_expiry>
 *
 * 参数：
 *   broker_ip: Broker IP 地址（默认 localhost）
 *   port: Broker 端口（默认 1883）
 *   session_expiry: 会话过期间隔，秒（默认 30）
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "mqtt_client.h"
#include "mqtt_packet.h"

static int keep_running = 1;

void sigint_handler(int sig) {
    (void)sig;
    keep_running = 0;
    printf("\n收到中断信号，正在退出...\n");
}

/* 回调函数 */
static int mqtt_message_cb(MqttClient* client, MqttMessage* msg,
    byte* new_data, word32 new_data_len, MqttPublishResponse* response)
{
    (void)client;
    (void)new_data;
    (void)new_data_len;
    (void)response;

    printf("\n=== 收到消息 ===\n");
    printf("主题: %s\n", msg->topic_name);
    printf("QoS: %d\n", msg->qos);
    printf("Payload: %.*s\n", (int)msg->total_len, (char*)msg->buffer);

    return 0;
}

int main(int argc, char** argv)
{
    int rc;
    MqttClient client;
    MqttNet net;

    /* 缓冲区 */
    word32 tx_buf_len = 1024;
    word32 rx_buf_len = 1024;
    byte tx_buf[1024];
    byte rx_buf[1024];

    /* 连接参数 */
    const char* host = (argc > 1) ? argv[1] : "localhost";
    word16 port = (argc > 2) ? atoi(argv[2]) : 1883;
    word32 session_expiry = (argc > 3) ? atoi(argv[3]) : 30; /* 默认30秒 */
    const char* client_id = "session_test_client";

    printf("MQTT v5 会话过期间隔示例\n");
    printf("连接到: %s:%d\n", host, port);
    printf("会话过期间隔: %u 秒\n", (unsigned int)session_expiry);

    /* 设置信号处理 */
    signal(SIGINT, sigint_handler);

    /* 初始化客户端 */
    memset(&client, 0, sizeof(client));
    memset(&net, 0, sizeof(net));

    net.connect = MqttSocket_Connect;
    net.read = MqttSocket_Read;
    net.write = MqttSocket_Write;
    net.disconnect = MqttSocket_Disnect;

    rc = MqttClient_Init(&client, &net, mqtt_message_cb,
        tx_buf, tx_buf_len, rx_buf, rx_buf_len, 5000);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("初始化失败: %d\n", rc);
        return 1;
    }

    /* 连接到 broker */
    printf("正在连接...\n");
    rc = MqttClient_NetConnect(&client, host, port, 5000, MQTT_CONNECT_PROTOCOL_LEVEL_5);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("连接失败: %d\n", rc);
        return 1;
    }

    /* 发送 CONNECT 包，包含会话过期间隔属性 */
    {
        MqttConnect connect;
        MqttProp* prop_list = NULL;

        memset(&connect, 0, sizeof(connect));

        connect.keep_alive_sec = 30;
        connect.clean_session = 0; /* 持久会话 */
        connect.protocol_level = MQTT_CONNECT_PROTOCOL_LEVEL_5;
        connect.client_id = client_id;

        /* 添加会话过期间隔属性 */
        MqttProp* expiry_prop = MqttProps_Add(&prop_list, MQTT_PROP_SESSION_EXPIRY_INTERVAL);
        if (expiry_prop != NULL) {
            expiry_prop->data_int = session_expiry;
            printf("设置会话过期间隔: %u 秒\n", (unsigned int)session_expiry);
        }

        connect.props = prop_list;

        rc = MqttClient_Connect(&client, &connect);

        /* 清理属性 */
        if (prop_list != NULL) {
            MqttProps_Free(prop_list);
        }

        if (rc != MQTT_CODE_SUCCESS) {
            printf("CONNECT 失败: %d\n", rc);
            MqttClient_NetDisconnect(&client);
            return 1;
        }

        printf("CONNECT 成功\n");
        printf("返回码: %d\n", connect.ack.return_code);
    }

    /* 订阅主题 */
    {
        MqttSubscribe subscribe;
        MqttTopic topics[1];
        MqttTopic qos_topic;

        memset(&subscribe, 0, sizeof(subscribe));
        memset(&topics, 0, sizeof(topics));
        memset(&qos_topic, 0, sizeof(qos_topic));

        qos_topic.topic = "test/session/#";
        qos_topic.qos = MQTT_QOS_1;
        qos_topic.retain_handle = 0;

        subscribe.packet_id = 1;
        subscribe.topic_count = 1;
        subscribe.topics = topics;

        rc = MqttClient_Subscribe(&client, &subscribe);
        if (rc != MQTT_CODE_SUCCESS) {
            printf("订阅失败: %d\n", rc);
        } else {
            printf("已订阅主题: test/session/#\n");
        }
    }

    printf("\n客户端已连接并订阅主题。\n");
    printf("会话将在断开连接后保留 %u 秒。\n", (unsigned int)session_expiry);
    printf("\n测试场景：\n");
    printf("1. 保持连接运行，接收消息\n");
    printf("2. 按 Ctrl+C 断开连接\n");
    printf("3. 在 %u 秒内重连，订阅将被保留\n", (unsigned int)session_expiry);
    printf("4. 等待超过 %u 秒后重连，订阅将被清除\n", (unsigned int)session_expiry);
    printf("\n按 Ctrl+C 断开连接...\n\n");

    /* 消息循环 */
    while (keep_running) {
        rc = MqttClient_WaitMessage(&client, 1000);
        if (rc == MQTT_CODE_ERROR_NETWORK) {
            printf("网络错误，断开连接\n");
            break;
        }
        else if (rc == MQTT_CODE_ERROR_TIMEOUT) {
            /* 超时是正常的，继续等待 */
            continue;
        }
        else if (rc != MQTT_CODE_SUCCESS) {
            printf("等待消息错误: %d\n", rc);
            break;
        }
    }

    /* 断开连接 */
    printf("\n正在断开连接...\n");
    MqttClient_Disconnect(&client);
    MqttClient_DeInit(&client);

    printf("客户端已断开连接。\n");
    printf("会话将在 %u 秒后过期。\n", (unsigned int)session_expiry);

    return 0;
}

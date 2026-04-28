/* MQTT v5 响应主题和关联数据示例
 *
 * 本示例演示如何使用 MQTT v5 的响应主题(Response Topic)和关联数据(Correlation Data)
 * 实现请求/响应模式。
 *
 * 编译：gcc -o v5_response_example v5_response_example.c -I../../wolfmqtt -L../../zig-out/lib -lmqtt
 * 运行：./v5_response_example <broker_ip> <port>
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mqtt_client.h"
#include "mqtt_packet.h"

/* 回调函数 */
static int mqtt_message_cb(MqttClient* client, MqttMessage* msg,
    byte* new_data, word32 new_data_len, MqttPublishResponse* response)
{
    (void)client;
    (void)new_data;
    (void)new_data_len;

    printf("\n=== 收到消息 ===\n");
    printf("主题: %s\n", msg->topic_name);
    printf("QoS: %d\n", msg->qos);
    printf("Payload: %.*s\n", (int)msg->total_len, (char*)msg->buffer);

#ifdef WOLFMQTT_V5
    /* 检查是否有响应主题 */
    if (msg->props != NULL) {
        MqttProp* prop;

        /* 检查响应主题 */
        prop = MqttProps_Find(msg->props, MQTT_PROP_RESP_TOPIC);
        if (prop != NULL && prop->data_str.str != NULL) {
            printf("响应主题: %s\n", prop->data_str.str);

            /* 如果有响应主题，设置响应属性 */
            if (response != NULL) {
                /* 复制关联数据到响应中 */
                MqttProp* corr_prop = MqttProps_Find(msg->props, MQTT_PROP_CORRELATION_DATA);
                if (corr_prop != NULL && corr_prop->data_bin.data != NULL) {
                    /* 创建关联数据属性 */
                    MqttProp* resp_corr_prop = MqttProps_Add(&response->props,
                        MQTT_PROP_CORRELATION_DATA);
                    if (resp_corr_prop != NULL) {
                        resp_corr_prop->data_bin.len = corr_prop->data_bin.len;
                        resp_corr_prop->data_bin.data = corr_prop->data_bin.data;
                        printf("包含关联数据: %d 字节\n", corr_prop->data_bin.len);
                    }
                }
            }
        }

        /* 检查关联数据 */
        prop = MqttProps_Find(msg->props, MQTT_PROP_CORRELATION_DATA);
        if (prop != NULL && prop->data_bin.data != NULL) {
            printf("关联数据: ");
            for (int i = 0; i < prop->data_bin.len && i < 16; i++) {
                printf("%02x ", prop->data_bin.data[i]);
            }
            printf("\n");
        }
    }
#endif

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

    printf("MQTT v5 响应主题示例\n");
    printf("连接到: %s:%d\n", host, port);

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

    /* 订阅响应主题 */
    {
        MqttSubscribe subscribe;
        MqttTopic topics[1];
        MqttTopic qos_topic;

        memset(&subscribe, 0, sizeof(subscribe));
        memset(&topics, 0, sizeof(topics));
        memset(&qos_topic, 0, sizeof(qos_topic));

        qos_topic.topic = "responses/my_client";
        qos_topic.qos = MQTT_QOS_1;
        qos_topic.retain_handle = 0;

        subscribe.packet_id = 1;
        subscribe.topic_count = 1;
        subscribe.topics = topics;

        rc = MqttClient_Subscribe(&client, &subscribe);
        if (rc != MQTT_CODE_SUCCESS) {
            printf("订阅失败: %d\n", rc);
        } else {
            printf("已订阅主题: responses/my_client\n");
        }
    }

    /* 发布带有响应主题和关联数据的请求 */
    {
        MqttPublish publish;
        MqttProp* prop_list = NULL;
        const char* request_payload = "请处理这个请求";

        memset(&publish, 0, sizeof(publish));

        publish.qos = MQTT_QOS_1;
        publish.retain = 0;
        publish.topic_name = "requests/service";
        publish.packet_id = 2;
        publish.buffer = (byte*)request_payload;
        publish.total_len = strlen(request_payload);
        publish.protocol_level = MQTT_CONNECT_PROTOCOL_LEVEL_5;

        /* 添加响应主题属性 */
        MqttProp* resp_topic_prop = MqttProps_Add(&prop_list, MQTT_PROP_RESP_TOPIC);
        if (resp_topic_prop != NULL) {
            resp_topic_prop->data_str.str = "responses/my_client";
            resp_topic_prop->data_str.len = strlen("responses/my_client");
            printf("设置响应主题: responses/my_client\n");
        }

        /* 添加关联数据属性 */
        const char* corr_data = "req-12345";
        MqttProp* corr_prop = MqttProps_Add(&prop_list, MQTT_PROP_CORRELATION_DATA);
        if (corr_prop != NULL) {
            corr_prop->data_bin.len = strlen(corr_data);
            corr_prop->data_bin.data = (byte*)corr_data;
            printf("设置关联数据: %s\n", corr_data);
        }

        publish.props = prop_list;

        rc = MqttClient_Publish(&client, &publish);

        /* 清理属性 */
        if (prop_list != NULL) {
            MqttProps_Free(prop_list);
        }

        if (rc != MQTT_CODE_SUCCESS) {
            printf("发布失败: %d\n", rc);
        } else {
            printf("已发布请求到: requests/service\n");
        }
    }

    /* 等待响应 */
    printf("\n等待响应...\n");
    printf("按 Ctrl+C 退出\n\n");

    /* 简单的消息循环 */
    while (1) {
        rc = MqttClient_WaitMessage(&client, 1000);
        if (rc == MQTT_CODE_ERROR_NETWORK) {
            printf("网络错误，断开连接\n");
            break;
        }
    }

    /* 清理 */
    MqttClient_Disconnect(&client);
    MqttClient_DeInit(&client);

    return 0;
}

/* Test MQTT persistent session (Clean Session = 0, Session Expiry Interval)
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 *
 * This file is part of wolfMQTT.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef WOLFMQTT_DISABLE_BROKER
#include "wolfmqtt/mqtt_broker.h"

/* Test 1: MQTT 3.1.1 Clean Session = 0
 * 验证客户端断开后订阅保留，重连后恢复 */
int test_clean_session_0(void) {
    printf("\n=== Test 1: MQTT 3.1.1 Clean Session = 0 ===\n");

    MqttBroker broker;
    MqttBrokerNet net;
    int rc;

    rc = MqttBrokerNet_Init(&net);
    if (rc != 0) {
        printf("FAIL: MqttBrokerNet_Init failed: %d\n", rc);
        return 1;
    }

    memset(&broker, 0, sizeof(broker));
    broker.port = 1883;
    rc = MqttBroker_InitEx(&broker, &net);
    if (rc != 0) {
        printf("FAIL: MqttBroker_InitEx failed: %d\n", rc);
        return 1;
    }

    BrokerOptions options = BROKER_OPTIONS_DEFAULTS;
    rc = MqttBroker_SetOptions(&broker, &options);
    if (rc != 0) {
        printf("FAIL: MqttBroker_SetOptions failed: %d\n", rc);
        return 1;
    }

    rc = MqttBroker_Start(&broker);
    if (rc != 0) {
        printf("FAIL: MqttBroker_Start failed: %d\n", rc);
        return 1;
    }

    printf("Broker started on port %d\n", broker.port);

    /* 模拟场景：
     * 1. 客户端 A (clean_session=0) 连接并订阅 topic/test
     * 2. 客户端 A 断开
     * 3. 发布消息到 topic/test
     * 4. 客户端 A 重连 (clean_session=0)
     * 5. 验证客户端 A 收到消息
     */

    printf("\nTest scenario:\n");
    printf("1. Client A (clean_session=0) connects and subscribes to 'topic/test'\n");
    printf("2. Client A disconnects\n");
    printf("3. Message published to 'topic/test'\n");
    printf("4. Client A reconnects (clean_session=0)\n");
    printf("5. Verify Client A receives the message\n");
    printf("\nNote: This test requires manual MQTT client interaction.\n");
    printf("Use mosquitto_sub or similar tool:\n");
    printf("  - mosquitto_sub -h localhost -p 1883 -i client_a -c -t 'topic/test'\n");
    printf("  - Disconnect and reconnect with same client_id\n");
    printf("  - Publish message: mosquitto_pub -h localhost -p 1883 -t 'topic/test' -m 'test'\n");

    /* Run for a while */
    int iterations = 0;
    while (iterations < 30) {
        rc = MqttBroker_TimeOut(&broker, 1000);
        if (rc < 0) {
            printf("FAIL: MqttBroker_TimeOut failed: %d\n", rc);
            break;
        }
        iterations++;
    }

    MqttBroker_Free(&broker);
    printf("\nTest 1 completed (requires manual verification)\n");
    return 0;
}

/* Test 2: MQTT 5 Session Expiry Interval
 * 验证会话过期间隔功能 */
int test_session_expiry_interval(void) {
    printf("\n=== Test 2: MQTT 5 Session Expiry Interval ===\n");

    MqttBroker broker;
    MqttBrokerNet net;
    int rc;

    rc = MqttBrokerNet_Init(&net);
    if (rc != 0) {
        printf("FAIL: MqttBrokerNet_Init failed: %d\n", rc);
        return 1;
    }

    memset(&broker, 0, sizeof(broker));
    broker.port = 1883;
    rc = MqttBroker_InitEx(&broker, &net);
    if (rc != 0) {
        printf("FAIL: MqttBroker_InitEx failed: %d\n", rc);
        return 1;
    }

    BrokerOptions options = BROKER_OPTIONS_DEFAULTS;
    rc = MqttBroker_SetOptions(&broker, &options);
    if (rc != 0) {
        printf("FAIL: MqttBroker_SetOptions failed: %d\n", rc);
        return 1;
    }

    rc = MqttBroker_Start(&broker);
    if (rc != 0) {
        printf("FAIL: MqttBroker_Start failed: %d\n", rc);
        return 1;
    }

    printf("Broker started on port %d\n", broker.port);

    /* 模拟场景：
     * 1. 客户端 B (MQTT 5, session expiry interval=10秒) 连接并订阅
     * 2. 客户端 B 断开
     * 3. 等待 5 秒，会话仍有效
     * 4. 客户端 B 重连，验证订阅恢复
     * 5. 客户端 B 断开
     * 6. 等待 12 秒，会话应过期
     * 7. 客户端 B 重连，验证订阅已清除
     */

    printf("\nTest scenario:\n");
    printf("1. Client B (MQTT 5, session-expiry-interval=10) connects and subscribes\n");
    printf("2. Client B disconnects\n");
    printf("3. Wait 5 seconds - session should still be valid\n");
    printf("4. Client B reconnects - verify subscription restored\n");
    printf("5. Client B disconnects\n");
    printf("6. Wait 12 seconds - session should expire\n");
    printf("7. Client B reconnects - verify subscription cleared\n");
    printf("\nNote: This test requires manual MQTT client interaction.\n");
    printf("Use MQTT 5 client with session-expiry-interval property.\n");

    /* Run for a while */
    int iterations = 0;
    while (iterations < 30) {
        rc = MqttBroker_TimeOut(&broker, 1000);
        if (rc < 0) {
            printf("FAIL: MqttBroker_TimeOut failed: %d\n", rc);
            break;
        }
        iterations++;
    }

    MqttBroker_Free(&broker);
    printf("\nTest 2 completed (requires manual verification)\n");
    return 0;
}

/* Test 3: Clean Session = 1
 * 验证客户端断开后订阅清除 */
int test_clean_session_1(void) {
    printf("\n=== Test 3: Clean Session = 1 ===\n");

    MqttBroker broker;
    MqttBrokerNet net;
    int rc;

    rc = MqttBrokerNet_Init(&net);
    if (rc != 0) {
        printf("FAIL: MqttBrokerNet_Init failed: %d\n", rc);
        return 1;
    }

    memset(&broker, 0, sizeof(broker));
    broker.port = 1883;
    rc = MqttBroker_InitEx(&broker, &net);
    if (rc != 0) {
        printf("FAIL: MqttBroker_InitEx failed: %d\n", rc);
        return 1;
    }

    BrokerOptions options = BROKER_OPTIONS_DEFAULTS;
    rc = MqttBroker_SetOptions(&broker, &options);
    if (rc != 0) {
        printf("FAIL: MqttBroker_SetOptions failed: %d\n", rc);
        return 1;
    }

    rc = MqttBroker_Start(&broker);
    if (rc != 0) {
        printf("FAIL: MqttBroker_Start failed: %d\n", rc);
        return 1;
    }

    printf("Broker started on port %d\n", broker.port);

    /* 模拟场景：
     * 1. 客户端 C (clean_session=1) 连接并订阅 topic/test
     * 2. 客户端 C 断开
     * 3. 发布消息到 topic/test
     * 4. 客户端 C 重连
     * 5. 验证客户端 C 未收到消息（订阅已清除）
     */

    printf("\nTest scenario:\n");
    printf("1. Client C (clean_session=1) connects and subscribes to 'topic/test'\n");
    printf("2. Client C disconnects\n");
    printf("3. Message published to 'topic/test'\n");
    printf("4. Client C reconnects (clean_session=1)\n");
    printf("5. Verify Client C does NOT receive the message\n");
    printf("\nNote: This test requires manual MQTT client interaction.\n");
    printf("Use mosquitto_sub or similar tool:\n");
    printf("  - mosquitto_sub -h localhost -p 1883 -i client_c -t 'topic/test'\n");

    /* Run for a while */
    int iterations = 0;
    while (iterations < 30) {
        rc = MqttBroker_TimeOut(&broker, 1000);
        if (rc < 0) {
            printf("FAIL: MqttBroker_TimeOut failed: %d\n", rc);
            break;
        }
        iterations++;
    }

    MqttBroker_Free(&broker);
    printf("\nTest 3 completed (requires manual verification)\n");
    return 0;
}

int main(int argc, char** argv) {
    int test_num = 0;

    printf("=====================================================\n");
    printf("MQTT Persistent Session Test Suite\n");
    printf("=====================================================\n");

    if (argc > 1) {
        test_num = atoi(argv[1]);
    }

    int result = 0;
    switch (test_num) {
        case 1:
            result = test_clean_session_0();
            break;
        case 2:
            result = test_session_expiry_interval();
            break;
        case 3:
            result = test_clean_session_1();
            break;
        default:
            printf("Running all tests...\n");
            result |= test_clean_session_0();
            result |= test_session_expiry_interval();
            result |= test_clean_session_1();
            break;
    }

    printf("\n=====================================================\n");
    if (result == 0) {
        printf("All tests completed (requires manual verification)\n");
    } else {
        printf("Some tests FAILED!\n");
    }
    printf("=====================================================\n");

    return result;
}

#else

int main(int argc, char** argv) {
    printf("Broker support is disabled in this build.\n");
    printf("Please rebuild with WOLFMQTT_BROKER defined.\n");
    return 1;
}

#endif

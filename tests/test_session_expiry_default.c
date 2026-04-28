/* Test default session expiry interval for MQTT 3.1.1 persistent sessions
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

/* Test 1: MQTT 3.1.1 clean=0 should use default session expiry interval
 * 验证 MQTT 3.1.1 客户端设置 clean=0 时使用默认会话过期间隔 */
int test_mqtt311_default_session_expiry(void) {
    printf("\n=== Test 1: MQTT 3.1.1 Default Session Expiry Interval ===\n");

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
    printf("Broker options default_session_expiry_interval: %u seconds\n",
           (unsigned int)options.default_session_expiry_interval);

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
    printf("\nTest scenario:\n");
    printf("1. MQTT 3.1.1 client connects with clean=0\n");
    printf("2. Client subscribes to 'topic/test'\n");
    printf("3. Client disconnects (subscription should be persisted for 180 seconds)\n");
    printf("4. Reconnect within 180 seconds with same client_id\n");
    printf("5. Verify subscription is restored\n");
    printf("\nNote: This test requires manual MQTT client interaction.\n");
    printf("Use mosquitto_sub or similar tool:\n");
    printf("  - mosquitto_sub -h localhost -p 1883 -i test_client -c -t 'topic/test'\n");
    printf("  - Disconnect and reconnect within 3 minutes\n");
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

/* Test 2: Custom default session expiry interval
 * 验证自定义默认会话过期间隔 */
int test_custom_session_expiry(void) {
    printf("\n=== Test 2: Custom Default Session Expiry Interval ===\n");

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
    options.default_session_expiry_interval = 600;  /* 10 minutes */

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

    printf("Broker started with custom default_session_expiry_interval: %u seconds (10 minutes)\n",
           (unsigned int)options.default_session_expiry_interval);
    printf("\nTest scenario:\n");
    printf("1. MQTT 3.1.1 client connects with clean=0\n");
    printf("2. Client disconnects (subscription persisted for 600 seconds)\n");
    printf("3. Reconnect within 10 minutes\n");
    printf("4. Verify subscription is restored\n");

    /* Run for a while */
    int iterations = 0;
    while (iterations < 10) {
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

/* Test 3: MQTT 5 with Session Expiry Interval property
 * 验证 MQTT 5 客户端指定 Session Expiry Interval 时优先使用客户端值 */
int test_mqtt5_property_override(void) {
    printf("\n=== Test 3: MQTT 5 Property Override ===\n");

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
    options.default_session_expiry_interval = 180;  /* 3 minutes default */

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

    printf("Broker started with default default_session_expiry_interval: 180 seconds\n");
    printf("\nTest scenario:\n");
    printf("1. MQTT 5 client connects with session-expiry-interval=3600\n");
    printf("2. Verify client value (3600) overrides broker default (180)\n");
    printf("\nNote: This test requires MQTT 5 client with session-expiry-interval property.\n");

    /* Run for a while */
    int iterations = 0;
    while (iterations < 10) {
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
    printf("Default Session Expiry Interval Test Suite\n");
    printf("=====================================================\n");

    if (argc > 1) {
        test_num = atoi(argv[1]);
    }

    int result = 0;
    switch (test_num) {
        case 1:
            result = test_mqtt311_default_session_expiry();
            break;
        case 2:
            result = test_custom_session_expiry();
            break;
        case 3:
            result = test_mqtt5_property_override();
            break;
        default:
            printf("Running all tests...\n");
            result |= test_mqtt311_default_session_expiry();
            result |= test_custom_session_expiry();
            result |= test_mqtt5_property_override();
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

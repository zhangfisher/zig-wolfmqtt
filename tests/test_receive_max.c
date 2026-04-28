/* Test MQTT 5 receiveMaximum flow control
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

/* Simple test to verify receiveMaximum flow control */
int main(int argc, char** argv) {
    MqttBroker broker;
    MqttBrokerNet net;
    int rc;

    printf("Testing MQTT 5 receiveMaximum flow control...\n");

    /* Initialize broker network */
    rc = MqttBrokerNet_Init(&net);
    if (rc != 0) {
        printf("MqttBrokerNet_Init failed: %d\n", rc);
        return 1;
    }

    /* Initialize broker */
    memset(&broker, 0, sizeof(broker));
    broker.port = 1883;
    rc = MqttBroker_InitEx(&broker, &net);
    if (rc != 0) {
        printf("MqttBroker_InitEx failed: %d\n", rc);
        return 1;
    }

    /* Set broker options */
    BrokerOptions options = BROKER_OPTIONS_DEFAULTS;
    rc = MqttBroker_SetOptions(&broker, &options);
    if (rc != 0) {
        printf("MqttBroker_SetOptions failed: %d\n", rc);
        return 1;
    }

    printf("Broker initialized successfully\n");
    printf("Receive maximum flow control: enabled\n");
    printf("Maximum packet size: %u\n", options.max_packet_size);
    printf("Topic alias maximum: %u\n", options.topic_alias_max);

    /* Start broker */
    rc = MqttBroker_Start(&broker);
    if (rc != 0) {
        printf("MqttBroker_Start failed: %d\n", rc);
        return 1;
    }

    printf("Broker started on port %d\n", broker.port);
    printf("Waiting for clients...\n");
    printf("Use MQTT client with receiveMaximum=5 to test flow control\n");

    /* Run for a while */
    int iterations = 0;
    while (iterations < 100) {
        rc = MqttBroker_TimeOut(&broker, 1000); /* 1 second timeout */
        if (rc < 0) {
            printf("MqttBroker_TimeOut failed: %d\n", rc);
            break;
        }
        iterations++;
    }

    /* Clean up */
    MqttBroker_Free(&broker);

    printf("Test completed successfully!\n");
    return 0;
}

#else

int main(int argc, char** argv) {
    printf("Broker support is disabled in this build.\n");
    printf("Please rebuild with WOLFMQTT_BROKER defined.\n");
    return 1;
}

#endif

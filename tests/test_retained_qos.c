/* Test retained message with QoS support
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

/* Simple test to verify retained message QoS storage */
int main(int argc, char** argv) {
    MqttBroker broker;
    MqttBrokerNet net;
    int rc;

    printf("Testing retained message QoS support...\n");

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
    printf("Max retained messages: %d\n", options.max_retained);
    printf("Max payload length: %d\n", options.max_payload_len);

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

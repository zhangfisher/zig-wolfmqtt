/* unit_test.c
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

/* Include the autoconf generated config.h */
#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#include "wolfmqtt/mqtt_packet.h"
#include "wolfmqtt/mqtt_client.h"

static int test_count = 0;
static int fail_count = 0;

#define CHECK(cond, msg) do { \
    test_count++; \
    if (!(cond)) { \
        PRINTF("FAIL: %s (line %d)", msg, __LINE__); \
        fail_count++; \
    } else { \
        PRINTF("  ok: %s", msg); \
    } \
} while (0)

/* -------------------------------------------------------------------------- */
/* MqttDecode_Vbi / MqttEncode_Vbi tests                                     */
/* -------------------------------------------------------------------------- */
static void test_vbi(void)
{
    byte buf[8];
    word32 value;
    int rc;

    PRINTF("--- MqttDecode_Vbi / MqttEncode_Vbi ---");

    /* 1-byte VBI: value 0 */
    buf[0] = 0x00;
    rc = MqttDecode_Vbi(buf, &value, sizeof(buf));
    CHECK(rc == 1, "1-byte VBI (0): rc == 1");
    CHECK(value == 0, "1-byte VBI (0): value == 0");

    /* 1-byte VBI: value 127 */
    buf[0] = 0x7F;
    rc = MqttDecode_Vbi(buf, &value, sizeof(buf));
    CHECK(rc == 1, "1-byte VBI (127): rc == 1");
    CHECK(value == 127, "1-byte VBI (127): value == 127");

    /* 2-byte VBI: value 128 (0x80 0x01) */
    buf[0] = 0x80; buf[1] = 0x01;
    rc = MqttDecode_Vbi(buf, &value, sizeof(buf));
    CHECK(rc == 2, "2-byte VBI (128): rc == 2");
    CHECK(value == 128, "2-byte VBI (128): value == 128");

    /* 2-byte VBI: value 200 (0xC8 0x01) */
    buf[0] = 0xC8; buf[1] = 0x01;
    rc = MqttDecode_Vbi(buf, &value, sizeof(buf));
    CHECK(rc == 2, "2-byte VBI (200): rc == 2");
    CHECK(value == 200, "2-byte VBI (200): value == 200");

    /* 2-byte VBI: value 16383 (0xFF 0x7F) - max 2-byte */
    buf[0] = 0xFF; buf[1] = 0x7F;
    rc = MqttDecode_Vbi(buf, &value, sizeof(buf));
    CHECK(rc == 2, "2-byte VBI (16383 max): rc == 2");
    CHECK(value == 16383, "2-byte VBI (16383 max): value == 16383");

    /* 3-byte VBI: value 2,097,151 = 0xFF 0xFF 0x7F - max 3-byte */
    buf[0] = 0xFF; buf[1] = 0xFF; buf[2] = 0x7F;
    rc = MqttDecode_Vbi(buf, &value, sizeof(buf));
    CHECK(rc == 3, "3-byte VBI (2097151 max): rc == 3");
    CHECK(value == 2097151, "3-byte VBI (2097151 max): value == 2097151");

    /* 4-byte VBI: value 268,435,455 = 0xFF 0xFF 0xFF 0x7F (max MQTT) */
    buf[0] = 0xFF; buf[1] = 0xFF; buf[2] = 0xFF; buf[3] = 0x7F;
    rc = MqttDecode_Vbi(buf, &value, sizeof(buf));
    CHECK(rc == 4, "4-byte VBI (268435455 max): rc == 4");
    CHECK(value == 268435455, "4-byte VBI (268435455 max): value correct");

    /* 5-byte malformed: all continuation bits set (5th byte needed) */
    buf[0] = 0xFF; buf[1] = 0xFF; buf[2] = 0xFF; buf[3] = 0xFF; buf[4] = 0x00;
    rc = MqttDecode_Vbi(buf, &value, sizeof(buf));
    CHECK(rc == MQTT_CODE_ERROR_MALFORMED_DATA,
          "5-byte malformed VBI: returns MALFORMED_DATA");

    /* Buffer too small for multi-byte VBI */
    buf[0] = 0x80;
    rc = MqttDecode_Vbi(buf, &value, 1);
    CHECK(rc == MQTT_CODE_ERROR_OUT_OF_BUFFER,
          "VBI buffer too small: returns OUT_OF_BUFFER");

    /* Encode/Decode roundtrip: max value 268,435,455 */
    rc = MqttEncode_Vbi(buf, 268435455);
    CHECK(rc == 4, "Encode max VBI: 4 bytes");
    rc = MqttDecode_Vbi(buf, &value, sizeof(buf));
    CHECK(rc == 4, "Decode max VBI roundtrip: rc == 4");
    CHECK(value == 268435455, "Decode max VBI roundtrip: value correct");

    /* Encode/Decode roundtrip: 2-byte boundary 128 */
    rc = MqttEncode_Vbi(buf, 128);
    CHECK(rc == 2, "Encode VBI 128: 2 bytes");
    rc = MqttDecode_Vbi(buf, &value, sizeof(buf));
    CHECK(rc == 2, "Decode VBI 128 roundtrip: rc == 2");
    CHECK(value == 128, "Decode VBI 128 roundtrip: value correct");

    /* Encode/Decode roundtrip: 3-byte boundary 16384 */
    rc = MqttEncode_Vbi(buf, 16384);
    CHECK(rc == 3, "Encode VBI 16384: 3 bytes");
    rc = MqttDecode_Vbi(buf, &value, sizeof(buf));
    CHECK(rc == 3, "Decode VBI 16384 roundtrip: rc == 3");
    CHECK(value == 16384, "Decode VBI 16384 roundtrip: value correct");

    /* Encode/Decode roundtrip: 4-byte boundary 2097152 */
    rc = MqttEncode_Vbi(buf, 2097152);
    CHECK(rc == 4, "Encode VBI 2097152: 4 bytes");
    rc = MqttDecode_Vbi(buf, &value, sizeof(buf));
    CHECK(rc == 4, "Decode VBI 2097152 roundtrip: rc == 4");
    CHECK(value == 2097152, "Decode VBI 2097152 roundtrip: value correct");

    /* [MQTT-1.5.5-1] Overlong encodings must be rejected */
    /* Overlong 2-byte encoding of 0: [0x80, 0x00] */
    buf[0] = 0x80; buf[1] = 0x00;
    rc = MqttDecode_Vbi(buf, &value, sizeof(buf));
    CHECK(rc == MQTT_CODE_ERROR_MALFORMED_DATA,
          "Overlong 2-byte VBI (0): returns MALFORMED_DATA");

    /* Overlong 3-byte encoding of 0: [0x80, 0x80, 0x00] */
    buf[0] = 0x80; buf[1] = 0x80; buf[2] = 0x00;
    rc = MqttDecode_Vbi(buf, &value, sizeof(buf));
    CHECK(rc == MQTT_CODE_ERROR_MALFORMED_DATA,
          "Overlong 3-byte VBI (0): returns MALFORMED_DATA");

    /* Overlong 4-byte encoding of 0: [0x80, 0x80, 0x80, 0x00] */
    buf[0] = 0x80; buf[1] = 0x80; buf[2] = 0x80; buf[3] = 0x00;
    rc = MqttDecode_Vbi(buf, &value, sizeof(buf));
    CHECK(rc == MQTT_CODE_ERROR_MALFORMED_DATA,
          "Overlong 4-byte VBI (0): returns MALFORMED_DATA");

    /* Overlong 2-byte encoding of 127: [0xFF, 0x00] */
    buf[0] = 0xFF; buf[1] = 0x00;
    rc = MqttDecode_Vbi(buf, &value, sizeof(buf));
    CHECK(rc == MQTT_CODE_ERROR_MALFORMED_DATA,
          "Overlong 2-byte VBI (127): returns MALFORMED_DATA");
}

/* -------------------------------------------------------------------------- */
/* MqttEncode_Publish tests                                                   */
/* -------------------------------------------------------------------------- */
static void test_encode_publish(void)
{
    byte tx_buf[256];
    MqttPublish pub;
    int rc;

    PRINTF("--- MqttEncode_Publish ---");

    /* QoS 1 with packet_id == 0 must fail */
    XMEMSET(&pub, 0, sizeof(pub));
    pub.topic_name = "test/topic";
    pub.qos = MQTT_QOS_1;
    pub.packet_id = 0;
    rc = MqttEncode_Publish(tx_buf, (int)sizeof(tx_buf), &pub, 0);
    CHECK(rc == MQTT_CODE_ERROR_PACKET_ID,
          "QoS 1 packet_id=0: returns ERROR_PACKET_ID");

    /* QoS 2 with packet_id == 0 must fail */
    XMEMSET(&pub, 0, sizeof(pub));
    pub.topic_name = "test/topic";
    pub.qos = MQTT_QOS_2;
    pub.packet_id = 0;
    rc = MqttEncode_Publish(tx_buf, (int)sizeof(tx_buf), &pub, 0);
    CHECK(rc == MQTT_CODE_ERROR_PACKET_ID,
          "QoS 2 packet_id=0: returns ERROR_PACKET_ID");

    /* QoS 0 with packet_id == 0 must succeed (no packet_id needed) */
    XMEMSET(&pub, 0, sizeof(pub));
    pub.topic_name = "test/topic";
    pub.qos = MQTT_QOS_0;
    pub.packet_id = 0;
    rc = MqttEncode_Publish(tx_buf, (int)sizeof(tx_buf), &pub, 0);
    CHECK(rc > 0, "QoS 0 packet_id=0: succeeds");

    /* QoS 1 with valid packet_id must succeed */
    XMEMSET(&pub, 0, sizeof(pub));
    pub.topic_name = "test/topic";
    pub.qos = MQTT_QOS_1;
    pub.packet_id = 1;
    rc = MqttEncode_Publish(tx_buf, (int)sizeof(tx_buf), &pub, 0);
    CHECK(rc > 0, "QoS 1 packet_id=1: succeeds");
}

/* -------------------------------------------------------------------------- */
/* MqttDecode_ConnectAck tests                                                */
/* -------------------------------------------------------------------------- */
static void test_decode_connack(void)
{
    byte buf[8];
    MqttConnectAck ack;
    int rc;

    PRINTF("--- MqttDecode_ConnectAck ---");

    /* Valid CONNACK: remain_len=2, flags=0, return_code=0 */
    buf[0] = MQTT_PACKET_TYPE_SET(MQTT_PACKET_TYPE_CONNECT_ACK); /* 0x20 */
    buf[1] = 2;    /* remain_len */
    buf[2] = 0;    /* flags */
    buf[3] = 0;    /* return_code (success) */
    XMEMSET(&ack, 0, sizeof(ack));
    rc = MqttDecode_ConnectAck(buf, 4, &ack);
    CHECK(rc > 0, "CONNACK remain_len=2: succeeds");
    CHECK(ack.return_code == 0, "CONNACK remain_len=2: return_code == 0");

    /* Malformed CONNACK: remain_len=0 */
    buf[0] = MQTT_PACKET_TYPE_SET(MQTT_PACKET_TYPE_CONNECT_ACK);
    buf[1] = 0;    /* remain_len = 0 */
    XMEMSET(&ack, 0, sizeof(ack));
    rc = MqttDecode_ConnectAck(buf, 2, &ack);
    CHECK(rc == MQTT_CODE_ERROR_MALFORMED_DATA,
          "CONNACK remain_len=0: returns MALFORMED_DATA");

    /* Malformed CONNACK: remain_len=1 */
    buf[0] = MQTT_PACKET_TYPE_SET(MQTT_PACKET_TYPE_CONNECT_ACK);
    buf[1] = 1;    /* remain_len = 1 */
    buf[2] = 0;    /* only flags, missing return_code */
    XMEMSET(&ack, 0, sizeof(ack));
    rc = MqttDecode_ConnectAck(buf, 3, &ack);
    CHECK(rc == MQTT_CODE_ERROR_MALFORMED_DATA,
          "CONNACK remain_len=1: returns MALFORMED_DATA");
}

/* -------------------------------------------------------------------------- */
/* MqttEncode_Subscribe tests                                                 */
/* -------------------------------------------------------------------------- */
static void test_encode_subscribe(void)
{
    byte tx_buf[256];
    MqttSubscribe sub;
    MqttTopic topic;
    int rc;

    PRINTF("--- MqttEncode_Subscribe ---");

    /* packet_id == 0 must fail */
    XMEMSET(&sub, 0, sizeof(sub));
    XMEMSET(&topic, 0, sizeof(topic));
    topic.topic_filter = "test/topic";
    sub.topics = &topic;
    sub.topic_count = 1;
    sub.packet_id = 0;
    rc = MqttEncode_Subscribe(tx_buf, (int)sizeof(tx_buf), &sub);
    CHECK(rc == MQTT_CODE_ERROR_PACKET_ID,
          "Subscribe packet_id=0: returns ERROR_PACKET_ID");

    /* packet_id != 0 must succeed */
    XMEMSET(&sub, 0, sizeof(sub));
    XMEMSET(&topic, 0, sizeof(topic));
    topic.topic_filter = "test/topic";
    sub.topics = &topic;
    sub.topic_count = 1;
    sub.packet_id = 1;
    rc = MqttEncode_Subscribe(tx_buf, (int)sizeof(tx_buf), &sub);
    CHECK(rc > 0, "Subscribe packet_id=1: succeeds");
}

/* -------------------------------------------------------------------------- */
/* MqttEncode_Unsubscribe tests                                               */
/* -------------------------------------------------------------------------- */
static void test_encode_unsubscribe(void)
{
    byte tx_buf[256];
    MqttUnsubscribe unsub;
    MqttTopic topic;
    int rc;

    PRINTF("--- MqttEncode_Unsubscribe ---");

    /* packet_id == 0 must fail */
    XMEMSET(&unsub, 0, sizeof(unsub));
    XMEMSET(&topic, 0, sizeof(topic));
    topic.topic_filter = "test/topic";
    unsub.topics = &topic;
    unsub.topic_count = 1;
    unsub.packet_id = 0;
    rc = MqttEncode_Unsubscribe(tx_buf, (int)sizeof(tx_buf), &unsub);
    CHECK(rc == MQTT_CODE_ERROR_PACKET_ID,
          "Unsubscribe packet_id=0: returns ERROR_PACKET_ID");

    /* packet_id != 0 must succeed */
    XMEMSET(&unsub, 0, sizeof(unsub));
    XMEMSET(&topic, 0, sizeof(topic));
    topic.topic_filter = "test/topic";
    unsub.topics = &topic;
    unsub.topic_count = 1;
    unsub.packet_id = 1;
    rc = MqttEncode_Unsubscribe(tx_buf, (int)sizeof(tx_buf), &unsub);
    CHECK(rc > 0, "Unsubscribe packet_id=1: succeeds");
}

/* -------------------------------------------------------------------------- */
/* MqttEncode/Decode_PublishResp v5 roundtrip tests                           */
/* -------------------------------------------------------------------------- */
#ifdef WOLFMQTT_V5
static void test_publish_resp_v5_roundtrip(void)
{
    byte buf[256];
    MqttPublishResp enc, dec;
    MqttProp prop;
    int enc_len, dec_len;
    char reason_str[] = "ok";

    PRINTF("--- MqttEncode/Decode_PublishResp v5 roundtrip ---");

    /* Case: reason_code=SUCCESS, props=non-NULL
     * This is the bug case: encoder must include reason_code byte when
     * properties are present, even if reason_code is SUCCESS. */
    XMEMSET(&enc, 0, sizeof(enc));
    XMEMSET(&prop, 0, sizeof(prop));
    prop.type = MQTT_PROP_REASON_STR;
    prop.data_str.str = reason_str;
    prop.data_str.len = (word16)XSTRLEN(reason_str);
    prop.next = NULL;
    enc.packet_id = 1;
    enc.protocol_level = MQTT_CONNECT_PROTOCOL_LEVEL_5;
    enc.reason_code = MQTT_REASON_SUCCESS;
    enc.props = &prop;

    enc_len = MqttEncode_PublishResp(buf, (int)sizeof(buf),
                  MQTT_PACKET_TYPE_PUBLISH_ACK, &enc);
    CHECK(enc_len > 0, "v5 PUBACK SUCCESS+props: encode succeeds");

    /* Decode and verify roundtrip */
    XMEMSET(&dec, 0, sizeof(dec));
    dec.protocol_level = MQTT_CONNECT_PROTOCOL_LEVEL_5;
    dec_len = MqttDecode_PublishResp(buf, enc_len,
                  MQTT_PACKET_TYPE_PUBLISH_ACK, &dec);
    CHECK(dec_len > 0, "v5 PUBACK SUCCESS+props: decode succeeds");
    CHECK(dec.packet_id == 1,
          "v5 PUBACK SUCCESS+props: packet_id roundtrip");
    CHECK(dec.reason_code == MQTT_REASON_SUCCESS,
          "v5 PUBACK SUCCESS+props: reason_code roundtrip");
    if (dec.props) {
        MqttProps_Free(dec.props);
        dec.props = NULL;
    }

    /* Case: reason_code=non-SUCCESS, props=NULL (baseline, should work) */
    XMEMSET(&enc, 0, sizeof(enc));
    enc.packet_id = 2;
    enc.protocol_level = MQTT_CONNECT_PROTOCOL_LEVEL_5;
    enc.reason_code = 0x80; /* Unspecified error */
    enc.props = NULL;

    enc_len = MqttEncode_PublishResp(buf, (int)sizeof(buf),
                  MQTT_PACKET_TYPE_PUBLISH_ACK, &enc);
    CHECK(enc_len > 0, "v5 PUBACK non-SUCCESS no-props: encode succeeds");

    XMEMSET(&dec, 0, sizeof(dec));
    dec.protocol_level = MQTT_CONNECT_PROTOCOL_LEVEL_5;
    dec_len = MqttDecode_PublishResp(buf, enc_len,
                  MQTT_PACKET_TYPE_PUBLISH_ACK, &dec);
    CHECK(dec_len > 0, "v5 PUBACK non-SUCCESS no-props: decode succeeds");
    CHECK(dec.reason_code == 0x80,
          "v5 PUBACK non-SUCCESS no-props: reason_code roundtrip");

    /* Case: reason_code=SUCCESS, props=NULL (minimal, no reason_code byte) */
    XMEMSET(&enc, 0, sizeof(enc));
    enc.packet_id = 3;
    enc.protocol_level = MQTT_CONNECT_PROTOCOL_LEVEL_5;
    enc.reason_code = MQTT_REASON_SUCCESS;
    enc.props = NULL;

    enc_len = MqttEncode_PublishResp(buf, (int)sizeof(buf),
                  MQTT_PACKET_TYPE_PUBLISH_ACK, &enc);
    CHECK(enc_len > 0, "v5 PUBACK SUCCESS no-props: encode succeeds");

    XMEMSET(&dec, 0, sizeof(dec));
    dec.protocol_level = MQTT_CONNECT_PROTOCOL_LEVEL_5;
    dec_len = MqttDecode_PublishResp(buf, enc_len,
                  MQTT_PACKET_TYPE_PUBLISH_ACK, &dec);
    CHECK(dec_len > 0, "v5 PUBACK SUCCESS no-props: decode succeeds");
    CHECK(dec.reason_code == MQTT_REASON_SUCCESS,
          "v5 PUBACK SUCCESS no-props: reason_code roundtrip");
}
#endif /* WOLFMQTT_V5 */

/* -------------------------------------------------------------------------- */
/* MqttEncode_Connect tests                                                   */
/* -------------------------------------------------------------------------- */
static void test_encode_connect(void)
{
    byte tx_buf[256];
    MqttConnect conn;
    int rc;

    PRINTF("--- MqttEncode_Connect ---");

    /* Password without username must fail [MQTT-3.1.2-22] */
    XMEMSET(&conn, 0, sizeof(conn));
    conn.client_id = "test_client";
    conn.username = NULL;
    conn.password = "secret";
    rc = MqttEncode_Connect(tx_buf, (int)sizeof(tx_buf), &conn);
    CHECK(rc == MQTT_CODE_ERROR_BAD_ARG,
          "password without username: returns ERROR_BAD_ARG");

    /* Both username and password must succeed */
    XMEMSET(&conn, 0, sizeof(conn));
    conn.client_id = "test_client";
    conn.username = "user";
    conn.password = "secret";
    rc = MqttEncode_Connect(tx_buf, (int)sizeof(tx_buf), &conn);
    CHECK(rc > 0, "username+password: succeeds");

    /* Username only (no password) must succeed */
    XMEMSET(&conn, 0, sizeof(conn));
    conn.client_id = "test_client";
    conn.username = "user";
    conn.password = NULL;
    rc = MqttEncode_Connect(tx_buf, (int)sizeof(tx_buf), &conn);
    CHECK(rc > 0, "username only: succeeds");

    /* Neither username nor password must succeed */
    XMEMSET(&conn, 0, sizeof(conn));
    conn.client_id = "test_client";
    conn.username = NULL;
    conn.password = NULL;
    rc = MqttEncode_Connect(tx_buf, (int)sizeof(tx_buf), &conn);
    CHECK(rc > 0, "no credentials: succeeds");
}

/* -------------------------------------------------------------------------- */
/* QoS 2 next-ack packet_type+1 arithmetic                                    */
/* -------------------------------------------------------------------------- */
static void test_qos2_ack_arithmetic(void)
{
    PRINTF("--- QoS 2 next-ack arithmetic ---");

    /* PUBLISH_REC + 1 must equal PUBLISH_REL */
    CHECK(MQTT_PACKET_TYPE_PUBLISH_REC + 1 == MQTT_PACKET_TYPE_PUBLISH_REL,
          "PUBLISH_REC + 1 == PUBLISH_REL");

    /* PUBLISH_REL + 1 must equal PUBLISH_COMP */
    CHECK(MQTT_PACKET_TYPE_PUBLISH_REL + 1 == MQTT_PACKET_TYPE_PUBLISH_COMP,
          "PUBLISH_REL + 1 == PUBLISH_COMP");

    /* Verify the actual enum values for safety */
    CHECK(MQTT_PACKET_TYPE_PUBLISH_REC == 5, "PUBLISH_REC == 5");
    CHECK(MQTT_PACKET_TYPE_PUBLISH_REL == 6, "PUBLISH_REL == 6");
    CHECK(MQTT_PACKET_TYPE_PUBLISH_COMP == 7, "PUBLISH_COMP == 7");
}

/* -------------------------------------------------------------------------- */
/* MqttDecode_SubscribeAck tests                                              */
/* -------------------------------------------------------------------------- */
static void test_decode_suback(void)
{
    byte buf[8];
    MqttSubscribeAck ack;
    int rc;

    PRINTF("--- MqttDecode_SubscribeAck ---");

    /* Valid SUBACK: remain_len=3 (packet_id=2 bytes + 1 return code) */
    buf[0] = MQTT_PACKET_TYPE_SET(MQTT_PACKET_TYPE_SUBSCRIBE_ACK); /* 0x90 */
    buf[1] = 3;    /* remain_len */
    buf[2] = 0;    /* packet_id MSB */
    buf[3] = 1;    /* packet_id LSB */
    buf[4] = 0;    /* return code: QoS 0 granted */
    XMEMSET(&ack, 0, sizeof(ack));
    rc = MqttDecode_SubscribeAck(buf, 5, &ack);
    CHECK(rc > 0, "SUBACK remain_len=3: succeeds");
    CHECK(ack.packet_id == 1, "SUBACK remain_len=3: packet_id == 1");

    /* Malformed SUBACK: remain_len=0 */
    buf[0] = MQTT_PACKET_TYPE_SET(MQTT_PACKET_TYPE_SUBSCRIBE_ACK);
    buf[1] = 0;    /* remain_len = 0 */
    XMEMSET(&ack, 0, sizeof(ack));
    rc = MqttDecode_SubscribeAck(buf, 2, &ack);
    CHECK(rc == MQTT_CODE_ERROR_MALFORMED_DATA,
          "SUBACK remain_len=0: returns MALFORMED_DATA");

    /* Malformed SUBACK: remain_len=1 */
    buf[0] = MQTT_PACKET_TYPE_SET(MQTT_PACKET_TYPE_SUBSCRIBE_ACK);
    buf[1] = 1;    /* remain_len = 1 */
    buf[2] = 0;
    XMEMSET(&ack, 0, sizeof(ack));
    rc = MqttDecode_SubscribeAck(buf, 3, &ack);
    CHECK(rc == MQTT_CODE_ERROR_MALFORMED_DATA,
          "SUBACK remain_len=1: returns MALFORMED_DATA");
}

/* -------------------------------------------------------------------------- */
/* MqttDecode_PublishResp tests                                               */
/* -------------------------------------------------------------------------- */
static void test_decode_publish_resp(void)
{
    byte buf[8];
    MqttPublishResp resp;
    int rc;

    PRINTF("--- MqttDecode_PublishResp ---");

    /* Valid PUBACK: remain_len=2 (packet_id only) */
    buf[0] = MQTT_PACKET_TYPE_SET(MQTT_PACKET_TYPE_PUBLISH_ACK); /* 0x40 */
    buf[1] = 2;    /* remain_len */
    buf[2] = 0;    /* packet_id MSB */
    buf[3] = 1;    /* packet_id LSB */
    XMEMSET(&resp, 0, sizeof(resp));
    rc = MqttDecode_PublishResp(buf, 4, MQTT_PACKET_TYPE_PUBLISH_ACK, &resp);
    CHECK(rc > 0, "PUBACK remain_len=2: succeeds");
    CHECK(resp.packet_id == 1, "PUBACK remain_len=2: packet_id == 1");

    /* Malformed PUBACK: remain_len=0 */
    buf[0] = MQTT_PACKET_TYPE_SET(MQTT_PACKET_TYPE_PUBLISH_ACK);
    buf[1] = 0;
    XMEMSET(&resp, 0, sizeof(resp));
    rc = MqttDecode_PublishResp(buf, 2, MQTT_PACKET_TYPE_PUBLISH_ACK, &resp);
    CHECK(rc == MQTT_CODE_ERROR_MALFORMED_DATA,
          "PUBACK remain_len=0: returns MALFORMED_DATA");

    /* Malformed PUBACK: remain_len=1 */
    buf[0] = MQTT_PACKET_TYPE_SET(MQTT_PACKET_TYPE_PUBLISH_ACK);
    buf[1] = 1;
    buf[2] = 0;
    XMEMSET(&resp, 0, sizeof(resp));
    rc = MqttDecode_PublishResp(buf, 3, MQTT_PACKET_TYPE_PUBLISH_ACK, &resp);
    CHECK(rc == MQTT_CODE_ERROR_MALFORMED_DATA,
          "PUBACK remain_len=1: returns MALFORMED_DATA");
}

/* -------------------------------------------------------------------------- */
/* MqttDecode_UnsubscribeAck tests                                            */
/* -------------------------------------------------------------------------- */
static void test_decode_unsuback(void)
{
    byte buf[8];
    MqttUnsubscribeAck ack;
    int rc;

    PRINTF("--- MqttDecode_UnsubscribeAck ---");

    /* Valid UNSUBACK: remain_len=2 (packet_id) */
    buf[0] = MQTT_PACKET_TYPE_SET(MQTT_PACKET_TYPE_UNSUBSCRIBE_ACK); /* 0xB0 */
    buf[1] = 2;    /* remain_len */
    buf[2] = 0;    /* packet_id MSB */
    buf[3] = 1;    /* packet_id LSB */
    XMEMSET(&ack, 0, sizeof(ack));
    rc = MqttDecode_UnsubscribeAck(buf, 4, &ack);
    CHECK(rc > 0, "UNSUBACK remain_len=2: succeeds");
    CHECK(ack.packet_id == 1, "UNSUBACK remain_len=2: packet_id == 1");

    /* Malformed UNSUBACK: remain_len=0 */
    buf[0] = MQTT_PACKET_TYPE_SET(MQTT_PACKET_TYPE_UNSUBSCRIBE_ACK);
    buf[1] = 0;
    XMEMSET(&ack, 0, sizeof(ack));
    rc = MqttDecode_UnsubscribeAck(buf, 2, &ack);
    CHECK(rc == MQTT_CODE_ERROR_MALFORMED_DATA,
          "UNSUBACK remain_len=0: returns MALFORMED_DATA");

    /* Malformed UNSUBACK: remain_len=1 */
    buf[0] = MQTT_PACKET_TYPE_SET(MQTT_PACKET_TYPE_UNSUBSCRIBE_ACK);
    buf[1] = 1;
    buf[2] = 0;
    XMEMSET(&ack, 0, sizeof(ack));
    rc = MqttDecode_UnsubscribeAck(buf, 3, &ack);
    CHECK(rc == MQTT_CODE_ERROR_MALFORMED_DATA,
          "UNSUBACK remain_len=1: returns MALFORMED_DATA");
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    PRINTF("wolfMQTT Unit Tests");

#ifdef WOLFMQTT_V5
    MqttProps_Init();
#endif

    test_vbi();
    test_encode_publish();
    test_decode_connack();
    test_encode_subscribe();
    test_encode_unsubscribe();
    test_encode_connect();
    test_qos2_ack_arithmetic();
    test_decode_suback();
    test_decode_publish_resp();
    test_decode_unsuback();
#ifdef WOLFMQTT_V5
    test_publish_resp_v5_roundtrip();
    MqttProps_ShutDown();
#endif

    PRINTF("=== Results: %d/%d passed ===",
           test_count - fail_count, test_count);

    return fail_count > 0 ? 1 : 0;
}

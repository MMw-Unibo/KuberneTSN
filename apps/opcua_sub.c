/* This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information.
 *
 * Copyright (c) 2019 Kalycito Infotech Private Limited
 */

/**
 * .. _pubsub-subscribe-tutorial:
 *
 * **IMPORTANT ANNOUNCEMENT**
 *
 * The PubSub Subscriber API is currently not finished. This Tutorial will be
 * continuously extended during the next PubSub batches. More details about the
 * PubSub extension and corresponding open62541 API are located here: :ref:`pubsub`.
 *
 * Subscribing Fields
 * ^^^^^^^^^^^^^^^^^^
 * The PubSub subscribe example demonstrates the simplest way to receive
 * information over two transport layers such as UDP and Ethernet, that are
 * published by tutorial_pubsub_publish example and update values in the
 * TargetVariables of Subscriber Information Model.
 *
 * Run step of the application is as mentioned below:
 *
 * ./bin/examples/tutorial_pubsub_subscribe
 *
 * **Connection handling**
 *
 * PubSubConnections can be created and deleted on runtime. More details about
 * the system preconfiguration and connection can be found in
 * ``tutorial_pubsub_connection.c``. */

#include <open62541/plugin/log_stdout.h>
#include <open62541/plugin/pubsub_udp.h>
#include <open62541/server.h>
#include <open62541/plugin/pubsub_ethernet.h>

#include "ua_pubsub_networkmessage.h"

#include <stdio.h>

UA_Boolean running = true;
static UA_StatusCode
customDecodeAndProcessCallback(UA_PubSubChannel *psc, void *ctx, const UA_ByteString *buffer);
static void stopHandler(int sign)
{
    (void)sign; /* unused */
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
                "received ctrl-c");
    running = false;
}

static UA_StatusCode
subscriberListen(UA_PubSubChannel *psc)
{
    UA_ByteString buffer;
    UA_StatusCode retval = UA_ByteString_allocBuffer(&buffer, 512);
    if (retval != UA_STATUSCODE_GOOD)
    {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
                     "Message buffer allocation failed!");
        return retval;
    }

    /* Receive the message. Blocks for 100ms */
    UA_StatusCode rv = psc->receive(psc, NULL, customDecodeAndProcessCallback, NULL, 100);
    if (retval != UA_STATUSCODE_GOOD)
    {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
                     "Message receive failed!");
        return retval;
    }

    UA_ByteString_clear(&buffer);
    return rv;
}
static UA_StatusCode
customDecodeAndProcessCallback(UA_PubSubChannel *psc, void *ctx, const UA_ByteString *buffer)
{
    (void)ctx; /* unused */
    (void)psc; /* unused */

    /* Decode the message */
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                "Message length: %lu", (unsigned long)(*buffer).length);
    UA_NetworkMessage networkMessage;
    memset(&networkMessage, 0, sizeof(UA_NetworkMessage));
    size_t currentPosition = 0;
    UA_NetworkMessage_decodeBinary(buffer, &currentPosition, &networkMessage, NULL);

    /* Is this the correct message type? */
    if (networkMessage.networkMessageType != UA_NETWORKMESSAGE_DATASET)
        goto cleanup;

    /* At least one DataSetMessage in the NetworkMessage? */
    if (networkMessage.payloadHeaderEnabled &&
        networkMessage.payloadHeader.dataSetPayloadHeader.count < 1)
        goto cleanup;

    /* Is this a KeyFrame-DataSetMessage? */
    for (size_t j = 0; j < networkMessage.payloadHeader.dataSetPayloadHeader.count; j++)
    {
        UA_DataSetMessage *dsm = &networkMessage.payload.dataSetPayload.dataSetMessages[j];
        if (dsm->header.dataSetMessageType != UA_DATASETMESSAGE_DATAKEYFRAME)
            continue;
        if (dsm->header.fieldEncoding == UA_FIELDENCODING_RAWDATA)
        {
            // The RAW-Encoded payload contains no fieldCount information
            //  UA_DateTime dateTime;
            //  size_t offset = 0;
            //  UA_DateTime_decodeBinary(&dsm->data.keyFrameData.rawFields, &offset, &dateTime);
            //  //UA_DateTime value = *(UA_DateTime *)dsm->data.keyFrameData.rawFields->data;
            //  UA_DateTimeStruct receivedTime = UA_DateTime_toStruct(dateTime);
            //  UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
            //              "Message content: [DateTime] \t"
            //              "Received date: %02i-%02i-%02i Received time: %02i:%02i:%02i",
            //              receivedTime.year, receivedTime.month, receivedTime.day,
            //              receivedTime.hour, receivedTime.min, receivedTime.sec);
        }
        else
        {
            /* Loop over the fields and print well-known content types */
            for (int i = 0; i < dsm->data.keyFrameData.fieldCount; i++)
            {
                const UA_DataType *currentType = dsm->data.keyFrameData.dataSetFields[i].value.type;
                if (currentType == &UA_TYPES[UA_TYPES_BYTE])
                {
                    UA_Byte value = *(UA_Byte *)dsm->data.keyFrameData.dataSetFields[i].value.data;
                    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                                "Message content: [Byte] \tReceived data: %i", value);
                }
                else if (currentType == &UA_TYPES[UA_TYPES_UINT32])
                {
                    UA_UInt32 value = *(UA_UInt32 *)dsm->data.keyFrameData.dataSetFields[i].value.data;
                    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                                "Message content: [UInt32] \tReceived data: %u", value);
                }
                else if (currentType == &UA_TYPES[UA_TYPES_DATETIME])
                {
                    UA_DateTime value = *(UA_DateTime *)dsm->data.keyFrameData.dataSetFields[i].value.data;
                    UA_DateTimeStruct receivedTime = UA_DateTime_toStruct(value);
                    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                                "Message content: [DateTime] \t"
                                "Received date: %02i-%02i-%02i Received time: %02i:%02i:%02i",
                                receivedTime.year, receivedTime.month, receivedTime.day,
                                receivedTime.hour, receivedTime.min, receivedTime.sec);
                }
            }
        }
    }

cleanup:
    UA_NetworkMessage_clear(&networkMessage);
    return UA_STATUSCODE_GOOD;
}

static void
usage(char *progname)
{
    printf("usage: %s [device]\n", progname);
}

int main(int argc, char **argv)
{
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    UA_NetworkAddressUrlDataType networkAddressUrl =
        {UA_STRING_NULL, UA_STRING("opc.eth://01-00-5E-00-00-01")};

    if (argc != 2)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "-h") == 0)
    {
        usage(argv[0]);
        return EXIT_SUCCESS;
    }

    UA_PubSubTransportLayer ethernetLayer = UA_PubSubTransportLayerEthernet();

    networkAddressUrl.networkInterface = UA_STRING(argv[1]);
    UA_PubSubConnectionConfig connectionConfig;
    memset(&connectionConfig, 0, sizeof(connectionConfig));
    connectionConfig.name = UA_STRING("UADP Connection 1");
    connectionConfig.transportProfileUri =
        UA_STRING("http://opcfoundation.org/UA-Profile/Transport/pubsub-eth-uadp");
    connectionConfig.enabled = UA_TRUE;

    UA_Variant_setScalar(&connectionConfig.address, &networkAddressUrl,
                         &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);

    UA_TransportLayerContext ctx;
    ctx.connectionConfig = &connectionConfig;
    UA_PubSubChannel *psc =
        ethernetLayer.createPubSubChannel(&ethernetLayer, &ctx);
    psc->regist(psc, NULL, NULL);

    // UA_StatusCode retval = UA_STATUSCODE_GOOD;
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                "Listening for messages on %s", argv[1]);
    while (running)
    {
        // retval = subscriberListen(psc);
        subscriberListen(psc);
    }

    psc->close(psc);

    return 0;
}
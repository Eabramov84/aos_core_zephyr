/*
 * Copyright (C) 2023 Renesas Electronics Corporation.
 * Copyright (C) 2023 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <unistd.h>

#include <pb_decode.h>
#include <tinycrypt/constants.h>
#include <tinycrypt/sha256.h>
#include <vchanapi.h>

#include "cmclient.hpp"
#include "log.hpp"

using namespace aos::sm::launcher;

static constexpr auto cConnectionTimeoutSec = 5;
static constexpr auto cDomdID = CONFIG_AOS_DOMD_ID;
static constexpr auto cSmVchanPath = CONFIG_AOS_SM_VCHAN_PATH;
static constexpr auto cNodeID = CONFIG_AOS_NODE_ID;
static constexpr auto cNodeType = CONFIG_AOS_NODE_TYPE;
static constexpr auto cNumCPUs = CONFIG_AOS_NUM_CPU;
static constexpr auto cTotalRAM = CONFIG_AOS_TOTAL_RAM;
static constexpr auto cPartitionSize = CONFIG_AOS_PARTITION_SIZE;

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

CMClient::~CMClient()
{
    atomic_set_bit(&mFinishReadTrigger, 0);
    mThread.Join();
}

aos::Error CMClient::Init(LauncherItf& launcher)
{
    LOG_DBG() << "Initialize CM client";

    mLauncher = &launcher;

    auto err = mThread.Run([this](void*) {
        ConnectToCM();
        ProcessMessages();
    });
    if (!err.IsNone()) {
        return err;
    }

    return aos::ErrorEnum::eNone;
}

aos::Error CMClient::InstancesRunStatus(const aos::Array<aos::InstanceStatus>& instances)
{
    (void)instances;

    return aos::ErrorEnum::eNone;
}

aos::Error CMClient::InstancesUpdateStatus(const aos::Array<aos::InstanceStatus>& instances)
{
    (void)instances;

    return aos::ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void CMClient::ConnectToCM()
{
    bool connected = false;

    do {
        auto ret = vch_connect(cDomdID, cSmVchanPath, &mSMvchanHandler);
        if (ret != 0) {
            LOG_WRN() << "Can't connect to SM vchan error: " << aos::Error(ret);

            sleep(cConnectionTimeoutSec);

            continue;
        }

        connected = true;
    } while (!connected);

    SendNodeConfiguration();

    return;
}

void CMClient::ProcessMessages()
{
    VchanMessageHeader header;

    while (!atomic_test_bit(&mFinishReadTrigger, 0)) {
        auto read = vch_read(&mSMvchanHandler, &header, sizeof(VchanMessageHeader));

        LOG_DBG() << "READ form channel: " << read;

        if (read < static_cast<int>(sizeof(VchanMessageHeader))) {
            LOG_ERR() << "Read less than header size: " << read;
            continue;
        }

        read = vch_read(&mSMvchanHandler, mReceiveBuffer.Get(), header.dataSize);
        if (read < static_cast<int>(header.dataSize)) {
            LOG_ERR() << "Read less than expected message: " << read;
            continue;
        }

        SHA256Digest calculatedDigest;

        auto err = CalculateSha256(mReceiveBuffer, header.dataSize, calculatedDigest);
        if (!err.IsNone()) {
            LOG_ERR() << "Can't calculate SHA256: " << err;
            continue;
        }

        if (0 != memcmp(calculatedDigest, header.sha256, sizeof(SHA256Digest))) {
            LOG_ERR() << "SHA256 mismatch";
            continue;
        }

        mIncomingMessage = servicemanager_v3_SMIncomingMessages_init_zero;

        auto stream = pb_istream_from_buffer((const uint8_t*)mReceiveBuffer.Get(), header.dataSize);

        auto status = pb_decode(&stream, servicemanager_v3_SMIncomingMessages_fields, &mIncomingMessage);
        if (!status) {
            LOG_ERR() << "Can't decode incoming PB message: " << PB_GET_ERROR(&stream);
            continue;
        }

        switch (mIncomingMessage.which_SMIncomingMessage) {
        case servicemanager_v3_SMIncomingMessages_get_unit_config_status_tag:
            break;

        case servicemanager_v3_SMIncomingMessages_check_unit_config_tag:
            break;

        case servicemanager_v3_SMIncomingMessages_set_unit_config_tag:
            break;

        case servicemanager_v3_SMIncomingMessages_run_instances_tag:
            break;

        case servicemanager_v3_SMIncomingMessages_system_log_request_tag:
            break;

        case servicemanager_v3_SMIncomingMessages_instance_log_request_tag:
            break;

        case servicemanager_v3_SMIncomingMessages_instance_crash_log_request_tag:
            break;

        case servicemanager_v3_SMIncomingMessages_image_content_info_tag:
            break;

        case servicemanager_v3_SMIncomingMessages_image_content_tag:
            break;

        default:
            break;
        }
    }
}

void CMClient::SendNodeConfiguration()
{
    aos::LockGuard lock(mSendMutex);

    mOutgoingMessage.which_SMOutgoingMessage = servicemanager_v3_SMOutgoingMessages_node_configuration_tag;
    mOutgoingMessage.SMOutgoingMessage.node_configuration
        = servicemanager_v3_NodeConfiguration servicemanager_v3_NodeConfiguration_init_zero;
    auto&                                     config = mOutgoingMessage.SMOutgoingMessage.node_configuration;

    strncpy(config.node_id, cNodeID, sizeof(config.node_id));
    strncpy(config.node_type, cNodeType, sizeof(config.node_type));
    config.remote_node = true;
    config.runner_features_count = 1;
    strncpy(config.runner_features[0], "xrun", sizeof(config.runner_features[0]));
    config.num_cpus = cNumCPUs;
    config.total_ram = cTotalRAM;
    config.partitions_count = 1;
    strncpy(config.partitions[0].name, "aos", sizeof(config.partitions[0].name));
    config.partitions[0].total_size = cPartitionSize;
    config.partitions[0].types_count = 1;
    strncpy(config.partitions[0].types[0], "services", sizeof(config.partitions[0].types[0]));

    auto err = SendPbMessageToVchan();
    if (!err.IsNone()) {
        LOG_ERR() << "Can't send node configuration: " << err;
    }
}

aos::Error CMClient::SendPbMessageToVchan()
{
    auto outStream
        = pb_ostream_from_buffer(static_cast<pb_byte_t*>(mSendBuffer.Get()), servicemanager_v3_SMOutgoingMessages_size);

    auto status = pb_encode(&outStream, servicemanager_v3_SMOutgoingMessages_fields, &mOutgoingMessage);
    if (!status) {
        LOG_ERR() << "Encoding failed: " << PB_GET_ERROR(&outStream);

        return aos::Error(aos::ErrorEnum::eRuntime);
    }

    VchanMessageHeader header = {static_cast<uint32_t>(outStream.bytes_written)};

    auto err = CalculateSha256(mSendBuffer, header.dataSize, header.sha256);
    if (!err.IsNone()) {
        return err;
    }

    err = SendBufferToVchan(&mSMvchanHandler, (uint8_t*)&header, sizeof(VchanMessageHeader));
    if (!err.IsNone()) {
        return err;
    }

    return SendBufferToVchan(&mSMvchanHandler, static_cast<uint8_t*>(mSendBuffer.Get()), header.dataSize);
}

aos::Error CMClient::SendBufferToVchan(vch_handle* vChanHandler, const uint8_t* buffer, size_t msgSize)
{
    for (size_t offset = 0; offset < msgSize;) {
        auto ret = vch_write(vChanHandler, buffer + offset, msgSize - offset);
        if (ret < 0) {
        }

        offset += ret;

        LOG_DBG() << "Written: " << ret << " from: " << msgSize;
    }

    return aos::ErrorEnum::eNone;
}

aos::Error CMClient::CalculateSha256(const aos::Buffer& buffer, size_t msgSize, SHA256Digest& digest)
{
    tc_sha256_state_struct s;

    auto ret = tc_sha256_init(&s);
    if (TC_CRYPTO_SUCCESS != ret) {
        return ret;
    }

    ret = tc_sha256_update(&s, static_cast<uint8_t*>(buffer.Get()), msgSize);
    if (TC_CRYPTO_SUCCESS != ret) {
        return ret;
    }

    ret = tc_sha256_final(digest, &s);
    if (TC_CRYPTO_SUCCESS != ret) {
        return ret;
    }

    return aos::ErrorEnum::eNone;
}

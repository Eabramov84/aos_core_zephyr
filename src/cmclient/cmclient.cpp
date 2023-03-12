/*
 * Copyright (C) 2023 Renesas Electronics Corporation.
 * Copyright (C) 2023 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <unistd.h>

#include "cmclient.hpp"
#include "log.hpp"

using namespace aos::sm::launcher;

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
        auto ret = vch_connect(1, "SM_VCHAN_PATH", nullptr);
        this->ProcessMessages();
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

void CMClient::ProcessMessages()
{
    auto i = 0;

    while (!atomic_test_bit(&mFinishReadTrigger, 0)) {
        LOG_DBG() << "Process message: " << i++;

        LOG_DBG() << "Try  form channel: ";
        auto read = vch_read(nullptr, nullptr, 100);
        LOG_DBG() << "READ  form channel: " << read;

        if (read == -1) {
            continue;
        }

        LOG_DBG() << "Try  write: ";
        auto ret = vch_write(nullptr, nullptr, 100);
        LOG_DBG() << "Try  done: ";
    }
}

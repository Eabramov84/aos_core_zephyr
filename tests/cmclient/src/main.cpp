/*
 * Copyright (C) 2023 Renesas Electronics Corporation.
 * Copyright (C) 2023 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include "cmclient/cmclient.hpp"
#include <aos/common/log.hpp>
#include <aos/sm/launcher.hpp>
bool                     gShutdown = false;
aos::Mutex               gWaitMessageMutex;
aos::ConditionalVariable gWaitMessageCondVar(gWaitMessageMutex);
aos::Mutex               gReadMutex;
aos::ConditionalVariable gReadCondVar(gReadMutex);
CMClient                 client;

using namespace aos;

void TestLogCallback(LogModule module, LogLevel level, const char* message)
{
    TC_PRINT("%s \n", message);
}

class MockLauncher : public sm::launcher::LauncherItf {
public:
    Error RunInstances(const Array<InstanceInfo>& instances, bool forceRestart = false) { return ErrorEnum::eNone; }
};
int vch_connect(domid_t domain, const char* path, struct vch_handle* h)
{
    TC_PRINT("Connection Done\n");
    {
        UniqueLock lock(gWaitMessageMutex);
        gWaitMessageCondVar.NotifyOne();
    }

    {
        UniqueLock lock(gReadMutex);
        gReadCondVar.Wait();
    }

    TC_PRINT("Connection Exit\n");

    return 0;
}

void vch_close(struct vch_handle* h)
{
}

int vch_read(struct vch_handle* h, void* buf, size_t size)
{
    TC_PRINT("Start read  %d \n", size);
    if (gShutdown) {
        return -1;
    }

    UniqueLock lock(gReadMutex);
    gReadCondVar.Wait();
    if (gShutdown) {
        return -1;
    }

    TC_PRINT("END read  %d \n", size);
    return size;
}

int vch_write(struct vch_handle* h, const void* buf, size_t size)
{
    TC_PRINT("Write done %d \n", size);

    {
        UniqueLock lock(gWaitMessageMutex);
        gWaitMessageCondVar.NotifyOne();
    }
    TC_PRINT("Notify msg \n");

    return size;
}
void testUnitConfigMessages()
{
    {
        TC_PRINT("[test tread] UNLOCK READER \n");
        UniqueLock lock(gReadMutex);
        gReadCondVar.NotifyOne();
    }

    // Wait node config
    {
        UniqueLock lock(gWaitMessageMutex);
        gWaitMessageCondVar.Wait();
        TC_PRINT("[test tread] UNLOCKED \n");
    }
}

ZTEST_SUITE(cmclient, NULL, NULL, NULL, NULL, NULL);

ZTEST(cmclient, test_init)
{

    MockLauncher mockLauncher;

    aos::Log::SetCallback(TestLogCallback);
    auto err = client.Init(mockLauncher);
    zassert_true(err.IsNone(), "init error: %s", err.ToString());

    TC_PRINT("[test tread] WAIT connection\n");
    {
        UniqueLock lock(gWaitMessageMutex);
        gWaitMessageCondVar.Wait();
    }
    TC_PRINT("[test tread] UNlocked WAIT connection\n");

    {
        UniqueLock lock(gReadMutex);
        gReadCondVar.NotifyOne();
    }
    TC_PRINT("[test tread] Notify for unlock connection \n");

    k_sleep(K_SECONDS(1));
    testUnitConfigMessages();
    testUnitConfigMessages();
    //testUnitConfigMessages();
}

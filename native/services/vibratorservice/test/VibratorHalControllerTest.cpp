/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *            http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "VibratorHalControllerTest"

#include <android/hardware/vibrator/IVibrator.h>
#include <cutils/atomic.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <utils/Log.h>
#include <thread>

#include <vibratorservice/VibratorCallbackScheduler.h>
#include <vibratorservice/VibratorHalController.h>
#include <vibratorservice/VibratorHalWrapper.h>

#include "test_utils.h"

using android::hardware::vibrator::Effect;
using android::hardware::vibrator::EffectStrength;

using std::chrono::milliseconds;

using namespace android;
using namespace std::chrono_literals;
using namespace testing;

static const auto ON_FN = [](vibrator::HalWrapper* hal) { return hal->on(10ms, []() {}); };
static const auto OFF_FN = [](vibrator::HalWrapper* hal) { return hal->off(); };
static const auto PING_FN = [](vibrator::HalWrapper* hal) { return hal->ping(); };

// -------------------------------------------------------------------------------------------------

class MockHalWrapper : public vibrator::HalWrapper {
public:
    MockHalWrapper(std::shared_ptr<vibrator::CallbackScheduler> scheduler)
          : HalWrapper(scheduler) {}
    virtual ~MockHalWrapper() = default;

    MOCK_METHOD(vibrator::HalResult<void>, ping, (), (override));
    MOCK_METHOD(void, tryReconnect, (), (override));
    MOCK_METHOD(vibrator::HalResult<void>, on,
                (milliseconds timeout, const std::function<void()>& completionCallback),
                (override));
    MOCK_METHOD(vibrator::HalResult<void>, off, (), (override));
    MOCK_METHOD(vibrator::HalResult<void>, setAmplitude, (float amplitude), (override));
    MOCK_METHOD(vibrator::HalResult<void>, setExternalControl, (bool enabled), (override));
    MOCK_METHOD(vibrator::HalResult<void>, alwaysOnEnable,
                (int32_t id, Effect effect, EffectStrength strength), (override));
    MOCK_METHOD(vibrator::HalResult<void>, alwaysOnDisable, (int32_t id), (override));
    MOCK_METHOD(vibrator::HalResult<milliseconds>, performEffect,
                (Effect effect, EffectStrength strength,
                 const std::function<void()>& completionCallback),
                (override));
    MOCK_METHOD(vibrator::HalResult<vibrator::Capabilities>, getCapabilitiesInternal, (),
                (override));

    vibrator::CallbackScheduler* getCallbackScheduler() { return mCallbackScheduler.get(); }
};

// -------------------------------------------------------------------------------------------------

class VibratorHalControllerTest : public Test {
public:
    void SetUp() override {
        mConnectCounter = 0;
        auto callbackScheduler = std::make_shared<vibrator::CallbackScheduler>();
        mMockHal = std::make_shared<StrictMock<MockHalWrapper>>(callbackScheduler);
        mController = std::make_unique<
                vibrator::HalController>(std::move(callbackScheduler),
                                         [&](std::shared_ptr<vibrator::CallbackScheduler>) {
                                             android_atomic_inc(&(this->mConnectCounter));
                                             return this->mMockHal;
                                         });
        ASSERT_NE(mController, nullptr);
    }

protected:
    int32_t mConnectCounter;
    std::shared_ptr<MockHalWrapper> mMockHal;
    std::unique_ptr<vibrator::HalController> mController;
};

// -------------------------------------------------------------------------------------------------

TEST_F(VibratorHalControllerTest, TestInit) {
    ASSERT_TRUE(mController->init());
    ASSERT_EQ(1, mConnectCounter);

    // Noop when wrapper was already initialized.
    ASSERT_TRUE(mController->init());
    ASSERT_EQ(1, mConnectCounter);
}

TEST_F(VibratorHalControllerTest, TestGetInfoRetriesOnTransactionFailure) {
    EXPECT_CALL(*mMockHal.get(), tryReconnect()).Times(Exactly(1));
    EXPECT_CALL(*mMockHal.get(), getCapabilitiesInternal())
            .Times(Exactly(2))
            .WillOnce(Return(vibrator::HalResult<vibrator::Capabilities>::transactionFailed("msg")))
            .WillRepeatedly(Return(vibrator::HalResult<vibrator::Capabilities>::ok(
                    vibrator::Capabilities::ON_CALLBACK)));

    auto result = mController->getInfo();
    ASSERT_TRUE(result.capabilities.isOk());
    ASSERT_EQ(1, mConnectCounter);
}

TEST_F(VibratorHalControllerTest, TestGetInfoDoesNotRetryOnOperationFailure) {
    EXPECT_CALL(*mMockHal.get(), tryReconnect()).Times(Exactly(0));
    EXPECT_CALL(*mMockHal.get(), getCapabilitiesInternal())
            .Times(Exactly(1))
            .WillRepeatedly(Return(vibrator::HalResult<vibrator::Capabilities>::failed("msg")));

    auto result = mController->getInfo();
    ASSERT_TRUE(result.capabilities.isFailed());
    ASSERT_EQ(1, mConnectCounter);
}

TEST_F(VibratorHalControllerTest, TestGetInfoDoesNotRetryOnUnsupported) {
    EXPECT_CALL(*mMockHal.get(), tryReconnect()).Times(Exactly(0));
    EXPECT_CALL(*mMockHal.get(), getCapabilitiesInternal())
            .Times(Exactly(1))
            .WillRepeatedly(Return(vibrator::HalResult<vibrator::Capabilities>::unsupported()));

    auto result = mController->getInfo();
    ASSERT_TRUE(result.capabilities.isUnsupported());
    ASSERT_EQ(1, mConnectCounter);
}

TEST_F(VibratorHalControllerTest, TestApiCallsAreForwardedToHal) {
    EXPECT_CALL(*mMockHal.get(), on(_, _))
            .Times(Exactly(1))
            .WillRepeatedly(Return(vibrator::HalResult<void>::ok()));

    auto result = mController->doWithRetry<void>(ON_FN, "on");
    ASSERT_TRUE(result.isOk());
    ASSERT_EQ(1, mConnectCounter);
}

TEST_F(VibratorHalControllerTest, TestUnsupportedApiResultDoesNotResetHalConnection) {
    EXPECT_CALL(*mMockHal.get(), tryReconnect()).Times(Exactly(0));
    EXPECT_CALL(*mMockHal.get(), off())
            .Times(Exactly(1))
            .WillRepeatedly(Return(vibrator::HalResult<void>::unsupported()));

    auto result = mController->doWithRetry<void>(OFF_FN, "off");
    ASSERT_TRUE(result.isUnsupported());
    ASSERT_EQ(1, mConnectCounter);
}

TEST_F(VibratorHalControllerTest, TestOperationFailedApiResultDoesNotResetHalConnection) {
    EXPECT_CALL(*mMockHal.get(), tryReconnect()).Times(Exactly(0));
    EXPECT_CALL(*mMockHal.get(), on(_, _))
            .Times(Exactly(1))
            .WillRepeatedly(Return(vibrator::HalResult<void>::failed("message")));

    auto result = mController->doWithRetry<void>(ON_FN, "on");
    ASSERT_TRUE(result.isFailed());
    ASSERT_EQ(1, mConnectCounter);
}

TEST_F(VibratorHalControllerTest, TestTransactionFailedApiResultResetsHalConnection) {
    EXPECT_CALL(*mMockHal.get(), tryReconnect()).Times(Exactly(1));
    EXPECT_CALL(*mMockHal.get(), on(_, _))
            .Times(Exactly(2))
            .WillRepeatedly(Return(vibrator::HalResult<void>::transactionFailed("message")));

    auto result = mController->doWithRetry<void>(ON_FN, "on");
    ASSERT_TRUE(result.isFailed());
    ASSERT_EQ(1, mConnectCounter);
}

TEST_F(VibratorHalControllerTest, TestTransactionFailedApiResultReturnsSuccessAfterRetries) {
    {
        InSequence seq;
        EXPECT_CALL(*mMockHal.get(), ping())
                .Times(Exactly(1))
                .WillRepeatedly(Return(vibrator::HalResult<void>::transactionFailed("message")));
        EXPECT_CALL(*mMockHal.get(), tryReconnect()).Times(Exactly(1));
        EXPECT_CALL(*mMockHal.get(), ping())
                .Times(Exactly(1))
                .WillRepeatedly(Return(vibrator::HalResult<void>::ok()));
    }

    auto result = mController->doWithRetry<void>(PING_FN, "ping");
    ASSERT_TRUE(result.isOk());
    ASSERT_EQ(1, mConnectCounter);
}

TEST_F(VibratorHalControllerTest, TestMultiThreadConnectsOnlyOnce) {
    ASSERT_EQ(0, mConnectCounter);

    EXPECT_CALL(*mMockHal.get(), ping())
            .Times(Exactly(10))
            .WillRepeatedly(Return(vibrator::HalResult<void>::ok()));

    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
        threads.push_back(std::thread([&]() {
            auto result = mController->doWithRetry<void>(PING_FN, "ping");
            ASSERT_TRUE(result.isOk());
        }));
    }
    std::for_each(threads.begin(), threads.end(), [](std::thread& t) { t.join(); });

    // Connector was called only by the first thread to use the api.
    ASSERT_EQ(1, mConnectCounter);
}

TEST_F(VibratorHalControllerTest, TestNoVibratorReturnsUnsupportedAndAttemptsToReconnect) {
    mController = std::make_unique<
            vibrator::HalController>(nullptr, [&](std::shared_ptr<vibrator::CallbackScheduler>) {
        android_atomic_inc(&(this->mConnectCounter));
        return nullptr;
    });
    ASSERT_EQ(0, mConnectCounter);

    ASSERT_TRUE(mController->doWithRetry<void>(OFF_FN, "off").isUnsupported());
    ASSERT_TRUE(mController->doWithRetry<void>(PING_FN, "ping").isUnsupported());

    // One connection attempt per api call.
    ASSERT_EQ(2, mConnectCounter);
}

TEST_F(VibratorHalControllerTest, TestScheduledCallbackSurvivesReconnection) {
    {
        InSequence seq;
        EXPECT_CALL(*mMockHal.get(), on(_, _))
                .Times(Exactly(1))
                .WillRepeatedly([&](milliseconds timeout, std::function<void()> callback) {
                    mMockHal.get()->getCallbackScheduler()->schedule(callback, timeout);
                    return vibrator::HalResult<void>::ok();
                });
        EXPECT_CALL(*mMockHal.get(), ping())
                .Times(Exactly(1))
                .WillRepeatedly(Return(vibrator::HalResult<void>::transactionFailed("message")));
        EXPECT_CALL(*mMockHal.get(), tryReconnect()).Times(Exactly(1));
        EXPECT_CALL(*mMockHal.get(), ping())
                .Times(Exactly(1))
                .WillRepeatedly(Return(vibrator::HalResult<void>::transactionFailed("message")));
    }

    auto counter = vibrator::TestCounter(0);

    auto onFn = [&](vibrator::HalWrapper* hal) {
        return hal->on(10ms, [&counter] { counter.increment(); });
    };
    ASSERT_TRUE(mController->doWithRetry<void>(onFn, "on").isOk());
    ASSERT_TRUE(mController->doWithRetry<void>(PING_FN, "ping").isFailed());
    mMockHal.reset();
    ASSERT_EQ(0, counter.get());

    // Callback triggered even after HalWrapper was reconnected.
    counter.tryWaitUntilCountIsAtLeast(1, 500ms);
    ASSERT_EQ(1, counter.get());
}

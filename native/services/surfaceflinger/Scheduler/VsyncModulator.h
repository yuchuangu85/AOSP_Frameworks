/*
 * Copyright 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <unordered_set>

#include <android-base/thread_annotations.h>
#include <binder/IBinder.h>
#include <utils/Timers.h>

#include <scheduler/TransactionSchedule.h>
#include <scheduler/VsyncConfig.h>

#include "../WpHash.h"

namespace android::scheduler {

// Modulates VSYNC phase depending on transaction schedule and refresh rate changes.
class VsyncModulator : public IBinder::DeathRecipient {
public:
    // Number of frames to keep early offsets after an early transaction or GPU composition.
    // This acts as a low-pass filter in case subsequent transactions are delayed, or if the
    // composition strategy alternates on subsequent frames.
    static constexpr int MIN_EARLY_TRANSACTION_FRAMES = 2;
    static constexpr int MIN_EARLY_GPU_FRAMES = 2;

    // Duration to delay the MIN_EARLY_TRANSACTION_FRAMES countdown after an early transaction.
    // This may keep early offsets for an extra frame, but avoids a race with transaction commit.
    static const std::chrono::nanoseconds MIN_EARLY_TRANSACTION_TIME;

    using VsyncConfigOpt = std::optional<VsyncConfig>;

    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Now = TimePoint (*)();

    explicit VsyncModulator(const VsyncConfigSet&, Now = Clock::now);

    bool isVsyncConfigEarly() const EXCLUDES(mMutex);

    VsyncConfig getVsyncConfig() const EXCLUDES(mMutex);

    void cancelRefreshRateChange() { mRefreshRateChangePending = false; }

    [[nodiscard]] VsyncConfig setVsyncConfigSet(const VsyncConfigSet&) EXCLUDES(mMutex);

    // Changes offsets in response to transaction flags or commit.
    [[nodiscard]] VsyncConfigOpt setTransactionSchedule(TransactionSchedule,
                                                        const sp<IBinder>& = {}) EXCLUDES(mMutex);
    [[nodiscard]] VsyncConfigOpt onTransactionCommit();

    // Called when we send a refresh rate change to hardware composer, so that
    // we can move into early offsets.
    [[nodiscard]] VsyncConfigOpt onRefreshRateChangeInitiated();

    // Called when we detect from VSYNC signals that the refresh rate changed.
    // This way we can move out of early offsets if no longer necessary.
    [[nodiscard]] VsyncConfigOpt onRefreshRateChangeCompleted();

    [[nodiscard]] VsyncConfigOpt onDisplayRefresh(bool usedGpuComposition);

protected:
    // Called from unit tests as well
    void binderDied(const wp<IBinder>&) override EXCLUDES(mMutex);

private:
    enum class VsyncConfigType { Early, EarlyGpu, Late };

    VsyncConfigType getNextVsyncConfigType() const REQUIRES(mMutex);
    const VsyncConfig& getNextVsyncConfig() const REQUIRES(mMutex);
    [[nodiscard]] VsyncConfig updateVsyncConfig() EXCLUDES(mMutex);
    [[nodiscard]] VsyncConfig updateVsyncConfigLocked() REQUIRES(mMutex);

    mutable std::mutex mMutex;
    VsyncConfigSet mVsyncConfigSet GUARDED_BY(mMutex);

    VsyncConfig mVsyncConfig GUARDED_BY(mMutex){mVsyncConfigSet.late};

    using Schedule = TransactionSchedule;
    std::atomic<Schedule> mTransactionSchedule = Schedule::Late;

    std::unordered_set<wp<IBinder>, WpHash> mEarlyWakeupRequests GUARDED_BY(mMutex);
    std::atomic<bool> mRefreshRateChangePending = false;

    std::atomic<int> mEarlyTransactionFrames = 0;
    std::atomic<int> mEarlyGpuFrames = 0;
    std::atomic<TimePoint> mEarlyTransactionStartTime = TimePoint();
    std::atomic<TimePoint> mLastTransactionCommitTime = TimePoint();

    const Now mNow;
    const bool mTraceDetailedInfo;
};

} // namespace android::scheduler

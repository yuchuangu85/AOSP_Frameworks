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

// TODO(b/129481165): remove the #pragma below and fix conversion issues
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"

//#define LOG_NDEBUG 0
#undef LOG_TAG
#define LOG_TAG "TransactionCallbackInvoker"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "TransactionCallbackInvoker.h"
#include "BackgroundExecutor.h"
#include "Utils/FenceUtils.h"

#include <cinttypes>

#include <binder/IInterface.h>
#include <common/FlagManager.h>
#include <utils/RefBase.h>

namespace android {

// Returns 0 if they are equal
//         <0 if the first id that doesn't match is lower in c2 or all ids match but c2 is shorter
//         >0 if the first id that doesn't match is greater in c2 or all ids match but c2 is longer
//
// See CallbackIdsHash for a explanation of why this works
static int compareCallbackIds(const std::vector<CallbackId>& c1,
                              const std::vector<CallbackId>& c2) {
    if (c1.empty()) {
        return !c2.empty();
    }
    return c1.front().id - c2.front().id;
}

static bool containsOnCommitCallbacks(const std::vector<CallbackId>& callbacks) {
    return !callbacks.empty() && callbacks.front().type == CallbackId::Type::ON_COMMIT;
}

void TransactionCallbackInvoker::addEmptyTransaction(const ListenerCallbacks& listenerCallbacks) {
    auto& [listener, callbackIds] = listenerCallbacks;
    auto& transactionStatsDeque = mCompletedTransactions[listener];
    transactionStatsDeque.emplace_back(callbackIds);
}

status_t TransactionCallbackInvoker::addOnCommitCallbackHandles(
        const std::deque<sp<CallbackHandle>>& handles,
        std::deque<sp<CallbackHandle>>& outRemainingHandles) {
    if (handles.empty()) {
        return NO_ERROR;
    }
    const std::vector<JankData>& jankData = std::vector<JankData>();
    for (const auto& handle : handles) {
        if (!containsOnCommitCallbacks(handle->callbackIds)) {
            outRemainingHandles.push_back(handle);
            continue;
        }
        status_t err = addCallbackHandle(handle, jankData);
        if (err != NO_ERROR) {
            return err;
        }
    }

    return NO_ERROR;
}

status_t TransactionCallbackInvoker::addCallbackHandles(
        const std::deque<sp<CallbackHandle>>& handles, const std::vector<JankData>& jankData) {
    if (handles.empty()) {
        return NO_ERROR;
    }
    for (const auto& handle : handles) {
        status_t err = addCallbackHandle(handle, jankData);
        if (err != NO_ERROR) {
            return err;
        }
    }

    return NO_ERROR;
}

status_t TransactionCallbackInvoker::findOrCreateTransactionStats(
        const sp<IBinder>& listener, const std::vector<CallbackId>& callbackIds,
        TransactionStats** outTransactionStats) {
    auto& transactionStatsDeque = mCompletedTransactions[listener];

    // Search back to front because the most recent transactions are at the back of the deque
    auto itr = transactionStatsDeque.rbegin();
    for (; itr != transactionStatsDeque.rend(); itr++) {
        if (compareCallbackIds(itr->callbackIds, callbackIds) == 0) {
            *outTransactionStats = &(*itr);
            return NO_ERROR;
        }
    }
    *outTransactionStats = &transactionStatsDeque.emplace_back(callbackIds);
    return NO_ERROR;
}

status_t TransactionCallbackInvoker::addCallbackHandle(const sp<CallbackHandle>& handle,
        const std::vector<JankData>& jankData) {
    // If we can't find the transaction stats something has gone wrong. The client should call
    // startRegistration before trying to add a callback handle.
    TransactionStats* transactionStats;
    status_t err =
            findOrCreateTransactionStats(handle->listener, handle->callbackIds, &transactionStats);
    if (err != NO_ERROR) {
        return err;
    }

    transactionStats->latchTime = handle->latchTime;
    // If the layer has already been destroyed, don't add the SurfaceControl to the callback.
    // The client side keeps a sp<> to the SurfaceControl so if the SurfaceControl has been
    // destroyed the client side is dead and there won't be anyone to send the callback to.
    sp<IBinder> surfaceControl = handle->surfaceControl.promote();
    if (surfaceControl) {
        sp<Fence> prevFence = nullptr;

        if (FlagManager::getInstance().ce_fence_promise()) {
            for (auto& future : handle->previousReleaseFences) {
                mergeFence(handle->name.c_str(), future.get().value_or(Fence::NO_FENCE), prevFence);
            }
        } else {
            for (const auto& future : handle->previousSharedReleaseFences) {
                mergeFence(handle->name.c_str(), future.get().value_or(Fence::NO_FENCE), prevFence);
            }
        }

        handle->previousReleaseFence = prevFence;
        handle->previousReleaseFences.clear();

        FrameEventHistoryStats eventStats(handle->frameNumber, handle->previousFrameNumber,
                                          handle->gpuCompositionDoneFence->getSnapshot().fence,
                                          handle->compositorTiming, handle->refreshStartTime,
                                          handle->dequeueReadyTime);
        transactionStats->surfaceStats.emplace_back(surfaceControl, handle->acquireTimeOrFence,
                                                    handle->previousReleaseFence,
                                                    handle->transformHint,
                                                    handle->currentMaxAcquiredBufferCount,
                                                    eventStats, jankData,
                                                    handle->previousReleaseCallbackId);
    }
    return NO_ERROR;
}

void TransactionCallbackInvoker::addPresentFence(sp<Fence> presentFence) {
    mPresentFence = std::move(presentFence);
}

void TransactionCallbackInvoker::sendCallbacks(bool onCommitOnly) {
    // For each listener
    auto completedTransactionsItr = mCompletedTransactions.begin();
    BackgroundExecutor::Callbacks callbacks;
    while (completedTransactionsItr != mCompletedTransactions.end()) {
        auto& [listener, transactionStatsDeque] = *completedTransactionsItr;
        ListenerStats listenerStats;
        listenerStats.listener = listener;

        // For each transaction
        auto transactionStatsItr = transactionStatsDeque.begin();
        while (transactionStatsItr != transactionStatsDeque.end()) {
            auto& transactionStats = *transactionStatsItr;
            if (onCommitOnly && !containsOnCommitCallbacks(transactionStats.callbackIds)) {
                transactionStatsItr++;
                continue;
            }

            // If the transaction has been latched
            if (transactionStats.latchTime >= 0 &&
                !containsOnCommitCallbacks(transactionStats.callbackIds)) {
                transactionStats.presentFence = mPresentFence;
            }

            // Remove the transaction from completed to the callback
            listenerStats.transactionStats.push_back(std::move(transactionStats));
            transactionStatsItr = transactionStatsDeque.erase(transactionStatsItr);
        }
        // If the listener has completed transactions
        if (!listenerStats.transactionStats.empty()) {
            // If the listener is still alive
            if (listener->isBinderAlive()) {
                // Send callback.  The listener stored in listenerStats
                // comes from the cross-process setTransactionState call to
                // SF.  This MUST be an ITransactionCompletedListener.  We
                // keep it as an IBinder due to consistency reasons: if we
                // interface_cast at the IPC boundary when reading a Parcel,
                // we get pointers that compare unequal in the SF process.
                callbacks.emplace_back([stats = std::move(listenerStats)]() {
                    interface_cast<ITransactionCompletedListener>(stats.listener)
                            ->onTransactionCompleted(stats);
                });
            }
        }
        completedTransactionsItr++;
    }

    if (mPresentFence) {
        mPresentFence.clear();
    }

    BackgroundExecutor::getInstance().sendCallbacks(std::move(callbacks));
}

// -----------------------------------------------------------------------

CallbackHandle::CallbackHandle(const sp<IBinder>& transactionListener,
                               const std::vector<CallbackId>& ids, const sp<IBinder>& sc)
      : listener(transactionListener), callbackIds(ids), surfaceControl(sc) {}

} // namespace android

// TODO(b/129481165): remove the #pragma below and fix conversion issues
#pragma clang diagnostic pop // ignored "-Wconversion"

/*
 * Copyright 2021 The Android Open Source Project
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

#include <ftl/small_vector.h>
#include <semaphore.h>
#include <utils/Singleton.h>
#include <thread>

#include "LocklessQueue.h"

namespace android {

// Executes tasks off the main thread.
class BackgroundExecutor : public Singleton<BackgroundExecutor> {
public:
    BackgroundExecutor();
    ~BackgroundExecutor();
    using Callbacks = ftl::SmallVector<std::function<void()>, 10>;
    // Queues callbacks onto a work queue to be executed by a background thread.
    // This is safe to call from multiple threads.
    void sendCallbacks(Callbacks&& tasks);
    void flushQueue();

private:
    sem_t mSemaphore;
    std::atomic_bool mDone = false;

    LocklessQueue<Callbacks> mCallbacksQueue;
    std::thread mThread;
};

} // namespace android

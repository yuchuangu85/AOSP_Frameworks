/*
 * Copyright 2024 The Android Open Source Project
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

#include <future>
#include <mutex>

#include <android-base/thread_annotations.h>

namespace android::utils {

// Allows a thread to `wait` for a future produced by a different thread. The future is returned by
// the first call to a function `F` that multiple threads may `callOnce`. If no `callOnce` happens,
// then `wait` does nothing. Otherwise, it blocks on the future, then destroys it, which resets the
// `OnceFuture`.
class OnceFuture {
public:
    template <typename F>
    void callOnce(F f) {
        std::lock_guard lock(mMutex);
        if (!mFuture.valid()) {
            mFuture = f();
        }
    }

    void wait() {
        std::lock_guard lock(mMutex);
        if (mFuture.valid()) {
            mFuture.wait();
            mFuture = {};
        }
    }

private:
    std::mutex mMutex;
    std::future<void> mFuture GUARDED_BY(mMutex);
};

} // namespace android::utils

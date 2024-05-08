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

#include <android/gui/ISurfaceComposer.h>
#include <gui/AidlStatusUtil.h>
#include <gui/WindowInfosListenerReporter.h>
#include "gui/WindowInfosUpdate.h"

namespace android {

using gui::DisplayInfo;
using gui::WindowInfo;
using gui::WindowInfosListener;
using gui::aidl_utils::statusTFromBinderStatus;

sp<WindowInfosListenerReporter> WindowInfosListenerReporter::getInstance() {
    static sp<WindowInfosListenerReporter> sInstance = new WindowInfosListenerReporter;
    return sInstance;
}

status_t WindowInfosListenerReporter::addWindowInfosListener(
        const sp<WindowInfosListener>& windowInfosListener,
        const sp<gui::ISurfaceComposer>& surfaceComposer,
        std::pair<std::vector<gui::WindowInfo>, std::vector<gui::DisplayInfo>>* outInitialInfo) {
    status_t status = OK;
    {
        std::scoped_lock lock(mListenersMutex);
        if (mWindowInfosListeners.empty()) {
            gui::WindowInfosListenerInfo listenerInfo;
            binder::Status s = surfaceComposer->addWindowInfosListener(this, &listenerInfo);
            status = statusTFromBinderStatus(s);
            if (status == OK) {
                mWindowInfosPublisher = std::move(listenerInfo.windowInfosPublisher);
                mListenerId = listenerInfo.listenerId;
            }
        }

        if (status == OK) {
            mWindowInfosListeners.insert(windowInfosListener);
        }

        if (outInitialInfo != nullptr) {
            outInitialInfo->first = mLastWindowInfos;
            outInitialInfo->second = mLastDisplayInfos;
        }
    }

    return status;
}

status_t WindowInfosListenerReporter::removeWindowInfosListener(
        const sp<WindowInfosListener>& windowInfosListener,
        const sp<gui::ISurfaceComposer>& surfaceComposer) {
    status_t status = OK;
    {
        std::scoped_lock lock(mListenersMutex);
        if (mWindowInfosListeners.find(windowInfosListener) == mWindowInfosListeners.end()) {
            return status;
        }

        if (mWindowInfosListeners.size() == 1) {
            binder::Status s = surfaceComposer->removeWindowInfosListener(this);
            status = statusTFromBinderStatus(s);
            // Clear the last stored state since we're disabling updates and don't want to hold
            // stale values
            mLastWindowInfos.clear();
            mLastDisplayInfos.clear();
        }

        if (status == OK) {
            mWindowInfosListeners.erase(windowInfosListener);
        }
    }

    return status;
}

binder::Status WindowInfosListenerReporter::onWindowInfosChanged(
        const gui::WindowInfosUpdate& update) {
    std::unordered_set<sp<WindowInfosListener>, gui::SpHash<WindowInfosListener>>
            windowInfosListeners;

    {
        std::scoped_lock lock(mListenersMutex);
        for (auto listener : mWindowInfosListeners) {
            windowInfosListeners.insert(listener);
        }

        mLastWindowInfos = update.windowInfos;
        mLastDisplayInfos = update.displayInfos;
    }

    for (auto listener : windowInfosListeners) {
        listener->onWindowInfosChanged(update);
    }

    mWindowInfosPublisher->ackWindowInfosReceived(update.vsyncId, mListenerId);

    return binder::Status::ok();
}

void WindowInfosListenerReporter::reconnect(const sp<gui::ISurfaceComposer>& composerService) {
    std::scoped_lock lock(mListenersMutex);
    if (!mWindowInfosListeners.empty()) {
        gui::WindowInfosListenerInfo listenerInfo;
        composerService->addWindowInfosListener(this, &listenerInfo);
        mWindowInfosPublisher = std::move(listenerInfo.windowInfosPublisher);
        mListenerId = listenerInfo.listenerId;
    }
}

} // namespace android

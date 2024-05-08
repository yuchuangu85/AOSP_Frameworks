/*
 * Copyright 2022 The Android Open Source Project
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

#include <list>
#include <memory>
#include <vector>

#include <PointerControllerInterface.h>
#include <utils/Timers.h>

#include "CapturedTouchpadEventConverter.h"
#include "EventHub.h"
#include "InputDevice.h"
#include "InputMapper.h"
#include "InputReaderBase.h"
#include "NotifyArgs.h"
#include "accumulator/MultiTouchMotionAccumulator.h"
#include "gestures/GestureConverter.h"
#include "gestures/HardwareStateConverter.h"
#include "gestures/PropertyProvider.h"

#include "include/gestures.h"

namespace android {

class TouchpadInputMapper : public InputMapper {
public:
    template <class T, class... Args>
    friend std::unique_ptr<T> createInputMapper(InputDeviceContext& deviceContext,
                                                const InputReaderConfiguration& readerConfig,
                                                Args... args);
    ~TouchpadInputMapper();

    uint32_t getSources() const override;
    void populateDeviceInfo(InputDeviceInfo& deviceInfo) override;
    void dump(std::string& dump) override;

    [[nodiscard]] std::list<NotifyArgs> reconfigure(nsecs_t when,
                                                    const InputReaderConfiguration& config,
                                                    ConfigurationChanges changes) override;
    [[nodiscard]] std::list<NotifyArgs> reset(nsecs_t when) override;
    [[nodiscard]] std::list<NotifyArgs> process(const RawEvent* rawEvent) override;

    void consumeGesture(const Gesture* gesture);

private:
    void resetGestureInterpreter(nsecs_t when);
    explicit TouchpadInputMapper(InputDeviceContext& deviceContext,
                                 const InputReaderConfiguration& readerConfig);
    [[nodiscard]] std::list<NotifyArgs> sendHardwareState(nsecs_t when, nsecs_t readTime,
                                                          SelfContainedHardwareState schs);
    [[nodiscard]] std::list<NotifyArgs> processGestures(nsecs_t when, nsecs_t readTime);

    std::unique_ptr<gestures::GestureInterpreter, void (*)(gestures::GestureInterpreter*)>
            mGestureInterpreter;
    std::shared_ptr<PointerControllerInterface> mPointerController;

    PropertyProvider mPropertyProvider;

    // The MultiTouchMotionAccumulator is shared between the HardwareStateConverter and
    // CapturedTouchpadEventConverter, so that if the touchpad is captured or released while touches
    // are down, the relevant converter can still benefit from the current axis values stored in the
    // accumulator.
    MultiTouchMotionAccumulator mMotionAccumulator;

    HardwareStateConverter mStateConverter;
    GestureConverter mGestureConverter;
    CapturedTouchpadEventConverter mCapturedEventConverter;

    bool mPointerCaptured = false;
    bool mProcessing = false;
    bool mResettingInterpreter = false;
    std::vector<Gesture> mGesturesToProcess;
};

} // namespace android

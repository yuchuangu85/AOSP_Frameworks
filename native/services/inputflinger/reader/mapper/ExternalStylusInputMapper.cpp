/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "../Macros.h"

#include "ExternalStylusInputMapper.h"

#include "SingleTouchMotionAccumulator.h"
#include "TouchButtonAccumulator.h"

namespace android {

ExternalStylusInputMapper::ExternalStylusInputMapper(InputDeviceContext& deviceContext,
                                                     const InputReaderConfiguration& readerConfig)
      : InputMapper(deviceContext, readerConfig), mTouchButtonAccumulator(deviceContext) {}

uint32_t ExternalStylusInputMapper::getSources() const {
    return AINPUT_SOURCE_STYLUS;
}

void ExternalStylusInputMapper::populateDeviceInfo(InputDeviceInfo& info) {
    InputMapper::populateDeviceInfo(info);
    if (mRawPressureAxis.valid) {
        info.addMotionRange(AMOTION_EVENT_AXIS_PRESSURE, AINPUT_SOURCE_STYLUS, 0.0f, 1.0f, 0.0f,
                            0.0f, 0.0f);
    }
}

void ExternalStylusInputMapper::dump(std::string& dump) {
    dump += INDENT2 "External Stylus Input Mapper:\n";
    dump += INDENT3 "Raw Stylus Axes:\n";
    dumpRawAbsoluteAxisInfo(dump, mRawPressureAxis, "Pressure");
    dump += INDENT3 "Stylus State:\n";
    dumpStylusState(dump, mStylusState);
}

std::list<NotifyArgs> ExternalStylusInputMapper::reconfigure(nsecs_t when,
                                                             const InputReaderConfiguration& config,
                                                             ConfigurationChanges changes) {
    getAbsoluteAxisInfo(ABS_PRESSURE, &mRawPressureAxis);
    mTouchButtonAccumulator.configure();
    return {};
}

std::list<NotifyArgs> ExternalStylusInputMapper::reset(nsecs_t when) {
    mSingleTouchMotionAccumulator.reset(getDeviceContext());
    mTouchButtonAccumulator.reset();
    return InputMapper::reset(when);
}

std::list<NotifyArgs> ExternalStylusInputMapper::process(const RawEvent* rawEvent) {
    std::list<NotifyArgs> out;
    mSingleTouchMotionAccumulator.process(rawEvent);
    mTouchButtonAccumulator.process(rawEvent);

    if (rawEvent->type == EV_SYN && rawEvent->code == SYN_REPORT) {
        out += sync(rawEvent->when);
    }
    return out;
}

std::list<NotifyArgs> ExternalStylusInputMapper::sync(nsecs_t when) {
    mStylusState.clear();

    mStylusState.when = when;

    mStylusState.toolType = mTouchButtonAccumulator.getToolType();
    if (mStylusState.toolType == ToolType::UNKNOWN) {
        mStylusState.toolType = ToolType::STYLUS;
    }

    if (mRawPressureAxis.valid) {
        auto rawPressure = static_cast<float>(mSingleTouchMotionAccumulator.getAbsolutePressure());
        mStylusState.pressure = (rawPressure - mRawPressureAxis.minValue) /
                static_cast<float>(mRawPressureAxis.maxValue - mRawPressureAxis.minValue);
    } else if (mTouchButtonAccumulator.hasButtonTouch()) {
        mStylusState.pressure = mTouchButtonAccumulator.isHovering() ? 0.0f : 1.0f;
    }

    mStylusState.buttons = mTouchButtonAccumulator.getButtonState();

    return getContext()->dispatchExternalStylusState(mStylusState);
}

} // namespace android

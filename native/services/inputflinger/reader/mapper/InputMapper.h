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

#pragma once

#include "EventHub.h"
#include "InputDevice.h"
#include "InputListener.h"
#include "InputReaderContext.h"
#include "NotifyArgs.h"
#include "StylusState.h"
#include "VibrationElement.h"

namespace android {
/**
 * This is the factory method that must be used to create any InputMapper
 */
template <class T, class... Args>
std::unique_ptr<T> createInputMapper(InputDeviceContext& deviceContext,
                                     const InputReaderConfiguration& readerConfig, Args... args) {
    // Using `new` to access non-public constructors.
    std::unique_ptr<T> mapper(new T(deviceContext, readerConfig, args...));
    // We need to reset and configure the mapper to ensure it is ready to process event
    nsecs_t now = systemTime(SYSTEM_TIME_MONOTONIC);
    std::list<NotifyArgs> unused = mapper->reset(now);
    unused += mapper->reconfigure(now, readerConfig, /*changes=*/{});
    return mapper;
}

/* An input mapper transforms raw input events into cooked event data.
 * A single input device can have multiple associated input mappers in order to interpret
 * different classes of events.
 *
 * InputMapper lifecycle:
 * - create and configure with 0 changes
 * - reset
 * - process, process, process (may occasionally reconfigure or reset)
 * - reset
 * - destroy
 */
class InputMapper {
public:
    /**
     * Subclasses must either provide a public constructor
     * or must be-friend the factory method.
     */
    template <class T, class... Args>
    friend std::unique_ptr<T> createInputMapper(InputDeviceContext& deviceContext,
                                                const InputReaderConfiguration& readerConfig,
                                                Args... args);

    virtual ~InputMapper();

    inline int32_t getDeviceId() { return mDeviceContext.getId(); }
    inline InputDeviceContext& getDeviceContext() { return mDeviceContext; }
    inline InputDeviceContext& getDeviceContext() const { return mDeviceContext; };
    inline const std::string getDeviceName() const { return mDeviceContext.getName(); }
    inline InputReaderContext* getContext() { return mDeviceContext.getContext(); }
    inline InputReaderPolicyInterface* getPolicy() { return getContext()->getPolicy(); }

    virtual uint32_t getSources() const = 0;
    virtual void populateDeviceInfo(InputDeviceInfo& deviceInfo);
    virtual void dump(std::string& dump);
    [[nodiscard]] virtual std::list<NotifyArgs> reconfigure(nsecs_t when,
                                                            const InputReaderConfiguration& config,
                                                            ConfigurationChanges changes);
    [[nodiscard]] virtual std::list<NotifyArgs> reset(nsecs_t when);
    [[nodiscard]] virtual std::list<NotifyArgs> process(const RawEvent* rawEvent) = 0;
    [[nodiscard]] virtual std::list<NotifyArgs> timeoutExpired(nsecs_t when);

    virtual int32_t getKeyCodeState(uint32_t sourceMask, int32_t keyCode);
    virtual int32_t getScanCodeState(uint32_t sourceMask, int32_t scanCode);
    virtual int32_t getSwitchState(uint32_t sourceMask, int32_t switchCode);
    virtual int32_t getKeyCodeForKeyLocation(int32_t locationKeyCode) const;

    virtual bool markSupportedKeyCodes(uint32_t sourceMask, const std::vector<int32_t>& keyCodes,
                                       uint8_t* outFlags);
    [[nodiscard]] virtual std::list<NotifyArgs> vibrate(const VibrationSequence& sequence,
                                                        ssize_t repeat, int32_t token);
    [[nodiscard]] virtual std::list<NotifyArgs> cancelVibrate(int32_t token);
    virtual bool isVibrating();
    virtual std::vector<int32_t> getVibratorIds();
    [[nodiscard]] virtual std::list<NotifyArgs> cancelTouch(nsecs_t when, nsecs_t readTime);
    virtual bool enableSensor(InputDeviceSensorType sensorType,
                              std::chrono::microseconds samplingPeriod,
                              std::chrono::microseconds maxBatchReportLatency);
    virtual void disableSensor(InputDeviceSensorType sensorType);
    virtual void flushSensor(InputDeviceSensorType sensorType);

    virtual std::optional<int32_t> getBatteryCapacity() { return std::nullopt; }
    virtual std::optional<int32_t> getBatteryStatus() { return std::nullopt; }

    virtual bool setLightColor(int32_t lightId, int32_t color) { return true; }
    virtual bool setLightPlayerId(int32_t lightId, int32_t playerId) { return true; }
    virtual std::optional<int32_t> getLightColor(int32_t lightId) { return std::nullopt; }
    virtual std::optional<int32_t> getLightPlayerId(int32_t lightId) { return std::nullopt; }

    virtual int32_t getMetaState();
    /**
     * Process the meta key and update the global meta state when changed.
     * Return true if the meta key could be handled by the InputMapper.
     */
    virtual bool updateMetaState(int32_t keyCode);

    [[nodiscard]] virtual std::list<NotifyArgs> updateExternalStylusState(const StylusState& state);

    virtual std::optional<int32_t> getAssociatedDisplayId() { return std::nullopt; }
    virtual void updateLedState(bool reset) {}

protected:
    InputDeviceContext& mDeviceContext;

    explicit InputMapper(InputDeviceContext& deviceContext,
                         const InputReaderConfiguration& readerConfig);

    status_t getAbsoluteAxisInfo(int32_t axis, RawAbsoluteAxisInfo* axisInfo);
    void bumpGeneration();

    static void dumpRawAbsoluteAxisInfo(std::string& dump, const RawAbsoluteAxisInfo& axis,
                                        const char* name);
    static void dumpStylusState(std::string& dump, const StylusState& state);
};

} // namespace android

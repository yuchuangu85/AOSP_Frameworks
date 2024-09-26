/*
 * Copyright (C) 2010 The Android Open Source Project
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

#include "../dispatcher/InputDispatcher.h"
#include "FakeApplicationHandle.h"
#include "FakeInputDispatcherPolicy.h"
#include "FakeInputTracingBackend.h"
#include "FakeWindows.h"
#include "TestEventMatchers.h"

#include <NotifyArgsBuilders.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/silent_death_test.h>
#include <android-base/stringprintf.h>
#include <android-base/thread_annotations.h>
#include <binder/Binder.h>
#include <com_android_input_flags.h>
#include <fcntl.h>
#include <flag_macros.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <input/BlockingQueue.h>
#include <input/Input.h>
#include <input/InputConsumer.h>
#include <input/PrintTools.h>
#include <linux/input.h>
#include <sys/epoll.h>

#include <cinttypes>
#include <compare>
#include <thread>
#include <unordered_set>
#include <vector>

using android::base::StringPrintf;
using android::gui::FocusRequest;
using android::gui::TouchOcclusionMode;
using android::gui::WindowInfo;
using android::gui::WindowInfoHandle;
using android::os::InputEventInjectionResult;
using android::os::InputEventInjectionSync;

namespace android::inputdispatcher {

using namespace ftl::flag_operators;
using testing::AllOf;
using testing::Not;
using testing::Pointee;
using testing::UnorderedElementsAre;

namespace {

// An arbitrary time value.
static constexpr nsecs_t ARBITRARY_TIME = 1234;

// An arbitrary device id.
static constexpr int32_t DEVICE_ID = DEFAULT_DEVICE_ID;
static constexpr int32_t SECOND_DEVICE_ID = 2;

// An arbitrary display id.
constexpr ui::LogicalDisplayId DISPLAY_ID = ui::LogicalDisplayId::DEFAULT;
constexpr ui::LogicalDisplayId SECOND_DISPLAY_ID = ui::LogicalDisplayId{1};

// Ensure common actions are interchangeable between keys and motions for convenience.
static_assert(AMOTION_EVENT_ACTION_DOWN == AKEY_EVENT_ACTION_DOWN);
static_assert(AMOTION_EVENT_ACTION_UP == AKEY_EVENT_ACTION_UP);
static constexpr int32_t ACTION_DOWN = AMOTION_EVENT_ACTION_DOWN;
static constexpr int32_t ACTION_MOVE = AMOTION_EVENT_ACTION_MOVE;
static constexpr int32_t ACTION_UP = AMOTION_EVENT_ACTION_UP;
static constexpr int32_t ACTION_HOVER_ENTER = AMOTION_EVENT_ACTION_HOVER_ENTER;
static constexpr int32_t ACTION_HOVER_MOVE = AMOTION_EVENT_ACTION_HOVER_MOVE;
static constexpr int32_t ACTION_HOVER_EXIT = AMOTION_EVENT_ACTION_HOVER_EXIT;
static constexpr int32_t ACTION_SCROLL = AMOTION_EVENT_ACTION_SCROLL;
static constexpr int32_t ACTION_OUTSIDE = AMOTION_EVENT_ACTION_OUTSIDE;
static constexpr int32_t ACTION_CANCEL = AMOTION_EVENT_ACTION_CANCEL;
/**
 * The POINTER_DOWN(0) is an unusual, but valid, action. It just means that the new pointer in the
 * MotionEvent is at the index 0 rather than 1 (or later). That is, the pointer id=0 (which is at
 * index 0) is the new pointer going down. The same pointer could have been placed at a different
 * index, and the action would become POINTER_1_DOWN, 2, etc..; these would all be valid. In
 * general, we try to place pointer id = 0 at the index 0. Of course, this is not possible if
 * pointer id=0 leaves but the pointer id=1 remains.
 */
static constexpr int32_t POINTER_0_DOWN =
        AMOTION_EVENT_ACTION_POINTER_DOWN | (0 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
static constexpr int32_t POINTER_1_DOWN =
        AMOTION_EVENT_ACTION_POINTER_DOWN | (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
static constexpr int32_t POINTER_2_DOWN =
        AMOTION_EVENT_ACTION_POINTER_DOWN | (2 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
static constexpr int32_t POINTER_3_DOWN =
        AMOTION_EVENT_ACTION_POINTER_DOWN | (3 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
static constexpr int32_t POINTER_0_UP =
        AMOTION_EVENT_ACTION_POINTER_UP | (0 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
static constexpr int32_t POINTER_1_UP =
        AMOTION_EVENT_ACTION_POINTER_UP | (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
static constexpr int32_t POINTER_2_UP =
        AMOTION_EVENT_ACTION_POINTER_UP | (2 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);

// The default pid and uid for the windows created on the secondary display by the test.
static constexpr gui::Pid SECONDARY_WINDOW_PID{1010};
static constexpr gui::Uid SECONDARY_WINDOW_UID{1012};

// An arbitrary pid of the gesture monitor window
static constexpr gui::Pid MONITOR_PID{2001};

static constexpr int EXPECTED_WALLPAPER_FLAGS =
        AMOTION_EVENT_FLAG_WINDOW_IS_OBSCURED | AMOTION_EVENT_FLAG_WINDOW_IS_PARTIALLY_OBSCURED;

using ReservedInputDeviceId::VIRTUAL_KEYBOARD_ID;

/**
 * Return a DOWN key event with KEYCODE_A.
 */
static KeyEvent getTestKeyEvent() {
    KeyEvent event;

    event.initialize(InputEvent::nextId(), DEVICE_ID, AINPUT_SOURCE_KEYBOARD,
                     ui::LogicalDisplayId::INVALID, INVALID_HMAC, AKEY_EVENT_ACTION_DOWN, 0,
                     AKEYCODE_A, KEY_A, AMETA_NONE, 0, ARBITRARY_TIME, ARBITRARY_TIME);
    return event;
}

/**
 * Provide a local override for a flag value. The value is restored when the object of this class
 * goes out of scope.
 * This class is not intended to be used directly, because its usage is cumbersome.
 * Instead, a wrapper macro SCOPED_FLAG_OVERRIDE is provided.
 */
class ScopedFlagOverride {
public:
    ScopedFlagOverride(std::function<bool()> read, std::function<void(bool)> write, bool value)
          : mInitialValue(read()), mWriteValue(write) {
        mWriteValue(value);
    }
    ~ScopedFlagOverride() { mWriteValue(mInitialValue); }

private:
    const bool mInitialValue;
    std::function<void(bool)> mWriteValue;
};

typedef bool (*readFlagValueFunction)();
typedef void (*writeFlagValueFunction)(bool);

/**
 * Use this macro to locally override a flag value.
 * Example usage:
 *    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
 * Note: this works by creating a local variable in your current scope. Don't call this twice for
 * the same flag, because the variable names will clash!
 */
#define SCOPED_FLAG_OVERRIDE(NAME, VALUE)                                  \
    readFlagValueFunction read##NAME = com::android::input::flags::NAME;   \
    writeFlagValueFunction write##NAME = com::android::input::flags::NAME; \
    ScopedFlagOverride override##NAME(read##NAME, write##NAME, (VALUE))

} // namespace

// --- InputDispatcherTest ---

class InputDispatcherTest : public testing::Test {
protected:
    std::unique_ptr<FakeInputDispatcherPolicy> mFakePolicy;
    std::unique_ptr<InputDispatcher> mDispatcher;
    std::shared_ptr<VerifyingTrace> mVerifyingTrace;

    void SetUp() override {
        mVerifyingTrace = std::make_shared<VerifyingTrace>();
        FakeWindowHandle::sOnEventReceivedCallback = [this](const auto& _1, const auto& _2) {
            handleEventReceivedByWindow(_1, _2);
        };

        mFakePolicy = std::make_unique<FakeInputDispatcherPolicy>();
        mDispatcher = std::make_unique<InputDispatcher>(*mFakePolicy,
                                                        std::make_unique<FakeInputTracingBackend>(
                                                                mVerifyingTrace));

        mDispatcher->setInputDispatchMode(/*enabled=*/true, /*frozen=*/false);
        // Start InputDispatcher thread
        ASSERT_EQ(OK, mDispatcher->start());
    }

    void TearDown() override {
        ASSERT_NO_FATAL_FAILURE(mVerifyingTrace->verifyExpectedEventsTraced());
        FakeWindowHandle::sOnEventReceivedCallback = nullptr;

        ASSERT_EQ(OK, mDispatcher->stop());
        mFakePolicy.reset();
        mDispatcher.reset();
    }

    void handleEventReceivedByWindow(const std::unique_ptr<InputEvent>& event,
                                     const gui::WindowInfo& info) {
        if (!event) {
            return;
        }

        switch (event->getType()) {
            case InputEventType::KEY: {
                mVerifyingTrace->expectKeyDispatchTraced(static_cast<KeyEvent&>(*event), info.id);
                break;
            }
            case InputEventType::MOTION: {
                mVerifyingTrace->expectMotionDispatchTraced(static_cast<MotionEvent&>(*event),
                                                            info.id);
                break;
            }
            default:
                break;
        }
    }

    /**
     * Used for debugging when writing the test
     */
    void dumpDispatcherState() {
        std::string dump;
        mDispatcher->dump(dump);
        std::stringstream ss(dump);
        std::string to;

        while (std::getline(ss, to, '\n')) {
            ALOGE("%s", to.c_str());
        }
    }

    void setFocusedWindow(const sp<WindowInfoHandle>& window) {
        FocusRequest request;
        request.token = window->getToken();
        request.windowName = window->getName();
        request.timestamp = systemTime(SYSTEM_TIME_MONOTONIC);
        request.displayId = window->getInfo()->displayId.val();
        mDispatcher->setFocusedWindow(request);
    }
};

TEST_F(InputDispatcherTest, InjectInputEvent_ValidatesKeyEvents) {
    KeyEvent event;

    // Rejects undefined key actions.
    event.initialize(InputEvent::nextId(), DEVICE_ID, AINPUT_SOURCE_KEYBOARD,
                     ui::LogicalDisplayId::INVALID, INVALID_HMAC,
                     /*action=*/-1, 0, AKEYCODE_A, KEY_A, AMETA_NONE, 0, ARBITRARY_TIME,
                     ARBITRARY_TIME);
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              mDispatcher->injectInputEvent(&event, /*targetUid=*/{}, InputEventInjectionSync::NONE,
                                            0ms, 0))
            << "Should reject key events with undefined action.";

    // Rejects ACTION_MULTIPLE since it is not supported despite being defined in the API.
    event.initialize(InputEvent::nextId(), DEVICE_ID, AINPUT_SOURCE_KEYBOARD,
                     ui::LogicalDisplayId::INVALID, INVALID_HMAC, AKEY_EVENT_ACTION_MULTIPLE, 0,
                     AKEYCODE_A, KEY_A, AMETA_NONE, 0, ARBITRARY_TIME, ARBITRARY_TIME);
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              mDispatcher->injectInputEvent(&event, /*targetUid=*/{}, InputEventInjectionSync::NONE,
                                            0ms, 0))
            << "Should reject key events with ACTION_MULTIPLE.";
}

TEST_F(InputDispatcherTest, InjectInputEvent_ValidatesMotionEvents) {
    MotionEvent event;
    PointerProperties pointerProperties[MAX_POINTERS + 1];
    PointerCoords pointerCoords[MAX_POINTERS + 1];
    for (size_t i = 0; i <= MAX_POINTERS; i++) {
        pointerProperties[i].clear();
        pointerProperties[i].id = i;
        pointerCoords[i].clear();
    }

    // Some constants commonly used below
    constexpr int32_t source = AINPUT_SOURCE_TOUCHSCREEN;
    constexpr int32_t edgeFlags = AMOTION_EVENT_EDGE_FLAG_NONE;
    constexpr int32_t metaState = AMETA_NONE;
    constexpr MotionClassification classification = MotionClassification::NONE;

    ui::Transform identityTransform;
    // Rejects undefined motion actions.
    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     /*action=*/-1, 0, 0, edgeFlags, metaState, 0, classification,
                     identityTransform, 0, 0, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, identityTransform, ARBITRARY_TIME,
                     ARBITRARY_TIME,
                     /*pointerCount=*/1, pointerProperties, pointerCoords);
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              mDispatcher->injectInputEvent(&event, /*targetUid=*/{}, InputEventInjectionSync::NONE,
                                            0ms, 0))
            << "Should reject motion events with undefined action.";

    // Rejects pointer down with invalid index.
    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     POINTER_1_DOWN, 0, 0, edgeFlags, metaState, 0, classification,
                     identityTransform, 0, 0, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, identityTransform, ARBITRARY_TIME,
                     ARBITRARY_TIME,
                     /*pointerCount=*/1, pointerProperties, pointerCoords);
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              mDispatcher->injectInputEvent(&event, /*targetUid=*/{}, InputEventInjectionSync::NONE,
                                            0ms, 0))
            << "Should reject motion events with pointer down index too large.";

    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     AMOTION_EVENT_ACTION_POINTER_DOWN |
                             (~0U << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT),
                     0, 0, edgeFlags, metaState, 0, classification, identityTransform, 0, 0,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     identityTransform, ARBITRARY_TIME, ARBITRARY_TIME,
                     /*pointerCount=*/1, pointerProperties, pointerCoords);
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              mDispatcher->injectInputEvent(&event, /*targetUid=*/{}, InputEventInjectionSync::NONE,
                                            0ms, 0))
            << "Should reject motion events with pointer down index too small.";

    // Rejects pointer up with invalid index.
    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     POINTER_1_UP, 0, 0, edgeFlags, metaState, 0, classification, identityTransform,
                     0, 0, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, identityTransform, ARBITRARY_TIME,
                     ARBITRARY_TIME,
                     /*pointerCount=*/1, pointerProperties, pointerCoords);
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              mDispatcher->injectInputEvent(&event, /*targetUid=*/{}, InputEventInjectionSync::NONE,
                                            0ms, 0))
            << "Should reject motion events with pointer up index too large.";

    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     AMOTION_EVENT_ACTION_POINTER_UP |
                             (~0U << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT),
                     0, 0, edgeFlags, metaState, 0, classification, identityTransform, 0, 0,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     identityTransform, ARBITRARY_TIME, ARBITRARY_TIME,
                     /*pointerCount=*/1, pointerProperties, pointerCoords);
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              mDispatcher->injectInputEvent(&event, /*targetUid=*/{}, InputEventInjectionSync::NONE,
                                            0ms, 0))
            << "Should reject motion events with pointer up index too small.";

    // Rejects motion events with invalid number of pointers.
    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     AMOTION_EVENT_ACTION_DOWN, 0, 0, edgeFlags, metaState, 0, classification,
                     identityTransform, 0, 0, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, identityTransform, ARBITRARY_TIME,
                     ARBITRARY_TIME,
                     /*pointerCount=*/0, pointerProperties, pointerCoords);
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              mDispatcher->injectInputEvent(&event, /*targetUid=*/{}, InputEventInjectionSync::NONE,
                                            0ms, 0))
            << "Should reject motion events with 0 pointers.";

    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     AMOTION_EVENT_ACTION_DOWN, 0, 0, edgeFlags, metaState, 0, classification,
                     identityTransform, 0, 0, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, identityTransform, ARBITRARY_TIME,
                     ARBITRARY_TIME,
                     /*pointerCount=*/MAX_POINTERS + 1, pointerProperties, pointerCoords);
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              mDispatcher->injectInputEvent(&event, /*targetUid=*/{}, InputEventInjectionSync::NONE,
                                            0ms, 0))
            << "Should reject motion events with more than MAX_POINTERS pointers.";

    // Rejects motion events with invalid pointer ids.
    pointerProperties[0].id = -1;
    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     AMOTION_EVENT_ACTION_DOWN, 0, 0, edgeFlags, metaState, 0, classification,
                     identityTransform, 0, 0, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, identityTransform, ARBITRARY_TIME,
                     ARBITRARY_TIME,
                     /*pointerCount=*/1, pointerProperties, pointerCoords);
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              mDispatcher->injectInputEvent(&event, /*targetUid=*/{}, InputEventInjectionSync::NONE,
                                            0ms, 0))
            << "Should reject motion events with pointer ids less than 0.";

    pointerProperties[0].id = MAX_POINTER_ID + 1;
    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     AMOTION_EVENT_ACTION_DOWN, 0, 0, edgeFlags, metaState, 0, classification,
                     identityTransform, 0, 0, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, identityTransform, ARBITRARY_TIME,
                     ARBITRARY_TIME,
                     /*pointerCount=*/1, pointerProperties, pointerCoords);
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              mDispatcher->injectInputEvent(&event, /*targetUid=*/{}, InputEventInjectionSync::NONE,
                                            0ms, 0))
            << "Should reject motion events with pointer ids greater than MAX_POINTER_ID.";

    // Rejects motion events with duplicate pointer ids.
    pointerProperties[0].id = 1;
    pointerProperties[1].id = 1;
    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     AMOTION_EVENT_ACTION_DOWN, 0, 0, edgeFlags, metaState, 0, classification,
                     identityTransform, 0, 0, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, identityTransform, ARBITRARY_TIME,
                     ARBITRARY_TIME,
                     /*pointerCount=*/2, pointerProperties, pointerCoords);
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              mDispatcher->injectInputEvent(&event, /*targetUid=*/{}, InputEventInjectionSync::NONE,
                                            0ms, 0))
            << "Should reject motion events with duplicate pointer ids.";
}

/* Test InputDispatcher for notifyConfigurationChanged and notifySwitch events */

TEST_F(InputDispatcherTest, NotifyConfigurationChanged_CallsPolicy) {
    constexpr nsecs_t eventTime = 20;
    mDispatcher->notifyConfigurationChanged({/*id=*/10, eventTime});
    ASSERT_TRUE(mDispatcher->waitForIdle());

    mFakePolicy->assertNotifyConfigurationChangedWasCalled(eventTime);
}

TEST_F(InputDispatcherTest, NotifySwitch_CallsPolicy) {
    NotifySwitchArgs args(InputEvent::nextId(), /*eventTime=*/20, /*policyFlags=*/0,
                          /*switchValues=*/1,
                          /*switchMask=*/2);
    mDispatcher->notifySwitch(args);

    // InputDispatcher adds POLICY_FLAG_TRUSTED because the event went through InputListener
    args.policyFlags |= POLICY_FLAG_TRUSTED;
    mFakePolicy->assertNotifySwitchWasCalled(args);
}

namespace {

static constexpr std::chrono::duration INJECT_EVENT_TIMEOUT = 500ms;

class FakeMonitorReceiver {
public:
    FakeMonitorReceiver(InputDispatcher& dispatcher, const std::string name,
                        ui::LogicalDisplayId displayId)
          : mInputReceiver(*dispatcher.createInputMonitor(displayId, name, MONITOR_PID), name) {}

    sp<IBinder> getToken() { return mInputReceiver.getToken(); }

    void consumeKeyDown(ui::LogicalDisplayId expectedDisplayId, int32_t expectedFlags = 0) {
        mInputReceiver.consumeEvent(InputEventType::KEY, AKEY_EVENT_ACTION_DOWN, expectedDisplayId,
                                    expectedFlags);
    }

    std::optional<int32_t> receiveEvent() {
        const auto [sequenceNum, _] = mInputReceiver.receiveEvent(CONSUME_TIMEOUT_EVENT_EXPECTED);
        return sequenceNum;
    }

    void finishEvent(uint32_t consumeSeq) { return mInputReceiver.finishEvent(consumeSeq); }

    void consumeMotionDown(ui::LogicalDisplayId expectedDisplayId, int32_t expectedFlags = 0) {
        mInputReceiver.consumeEvent(InputEventType::MOTION, AMOTION_EVENT_ACTION_DOWN,
                                    expectedDisplayId, expectedFlags);
    }

    void consumeMotionMove(ui::LogicalDisplayId expectedDisplayId, int32_t expectedFlags = 0) {
        mInputReceiver.consumeEvent(InputEventType::MOTION, AMOTION_EVENT_ACTION_MOVE,
                                    expectedDisplayId, expectedFlags);
    }

    void consumeMotionUp(ui::LogicalDisplayId expectedDisplayId, int32_t expectedFlags = 0) {
        mInputReceiver.consumeEvent(InputEventType::MOTION, AMOTION_EVENT_ACTION_UP,
                                    expectedDisplayId, expectedFlags);
    }

    void consumeMotionCancel(ui::LogicalDisplayId expectedDisplayId, int32_t expectedFlags = 0) {
        mInputReceiver.consumeMotionEvent(
                AllOf(WithMotionAction(AMOTION_EVENT_ACTION_CANCEL),
                      WithDisplayId(expectedDisplayId),
                      WithFlags(expectedFlags | AMOTION_EVENT_FLAG_CANCELED)));
    }

    void consumeMotionPointerDown(int32_t pointerIdx) {
        int32_t action = AMOTION_EVENT_ACTION_POINTER_DOWN |
                (pointerIdx << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        mInputReceiver.consumeEvent(InputEventType::MOTION, action, ui::LogicalDisplayId::DEFAULT,
                                    /*expectedFlags=*/0);
    }

    void consumeMotionEvent(const ::testing::Matcher<MotionEvent>& matcher) {
        mInputReceiver.consumeMotionEvent(matcher);
    }

    std::unique_ptr<MotionEvent> consumeMotion() { return mInputReceiver.consumeMotion(); }

    void assertNoEvents() { mInputReceiver.assertNoEvents(CONSUME_TIMEOUT_NO_EVENT_EXPECTED); }

private:
    FakeInputReceiver mInputReceiver;
};

static InputEventInjectionResult injectKey(
        InputDispatcher& dispatcher, int32_t action, int32_t repeatCount,
        ui::LogicalDisplayId displayId = ui::LogicalDisplayId::INVALID,
        InputEventInjectionSync syncMode = InputEventInjectionSync::WAIT_FOR_RESULT,
        std::chrono::milliseconds injectionTimeout = INJECT_EVENT_TIMEOUT,
        bool allowKeyRepeat = true, std::optional<gui::Uid> targetUid = {},
        uint32_t policyFlags = DEFAULT_POLICY_FLAGS) {
    KeyEvent event;
    nsecs_t currentTime = systemTime(SYSTEM_TIME_MONOTONIC);

    // Define a valid key down event.
    event.initialize(InputEvent::nextId(), DEVICE_ID, AINPUT_SOURCE_KEYBOARD, displayId,
                     INVALID_HMAC, action, /*flags=*/0, AKEYCODE_A, KEY_A, AMETA_NONE, repeatCount,
                     currentTime, currentTime);

    if (!allowKeyRepeat) {
        policyFlags |= POLICY_FLAG_DISABLE_KEY_REPEAT;
    }
    // Inject event until dispatch out.
    return dispatcher.injectInputEvent(&event, targetUid, syncMode, injectionTimeout, policyFlags);
}

static void assertInjectedKeyTimesOut(InputDispatcher& dispatcher) {
    InputEventInjectionResult result =
            injectKey(dispatcher, AKEY_EVENT_ACTION_DOWN, /*repeatCount=*/0,
                      ui::LogicalDisplayId::INVALID, InputEventInjectionSync::WAIT_FOR_RESULT,
                      CONSUME_TIMEOUT_NO_EVENT_EXPECTED);
    if (result != InputEventInjectionResult::TIMED_OUT) {
        FAIL() << "Injection should have timed out, but got " << ftl::enum_string(result);
    }
}

static InputEventInjectionResult injectKeyDown(
        InputDispatcher& dispatcher,
        ui::LogicalDisplayId displayId = ui::LogicalDisplayId::INVALID) {
    return injectKey(dispatcher, AKEY_EVENT_ACTION_DOWN, /*repeatCount=*/0, displayId);
}

// Inject a down event that has key repeat disabled. This allows InputDispatcher to idle without
// sending a subsequent key up. When key repeat is enabled, the dispatcher cannot idle because it
// has to be woken up to process the repeating key.
static InputEventInjectionResult injectKeyDownNoRepeat(
        InputDispatcher& dispatcher,
        ui::LogicalDisplayId displayId = ui::LogicalDisplayId::INVALID) {
    return injectKey(dispatcher, AKEY_EVENT_ACTION_DOWN, /*repeatCount=*/0, displayId,
                     InputEventInjectionSync::WAIT_FOR_RESULT, INJECT_EVENT_TIMEOUT,
                     /*allowKeyRepeat=*/false);
}

static InputEventInjectionResult injectKeyUp(
        InputDispatcher& dispatcher,
        ui::LogicalDisplayId displayId = ui::LogicalDisplayId::INVALID) {
    return injectKey(dispatcher, AKEY_EVENT_ACTION_UP, /*repeatCount=*/0, displayId);
}

static InputEventInjectionResult injectMotionEvent(
        InputDispatcher& dispatcher, const MotionEvent& event,
        std::chrono::milliseconds injectionTimeout = INJECT_EVENT_TIMEOUT,
        InputEventInjectionSync injectionMode = InputEventInjectionSync::WAIT_FOR_RESULT,
        std::optional<gui::Uid> targetUid = {}, uint32_t policyFlags = DEFAULT_POLICY_FLAGS) {
    return dispatcher.injectInputEvent(&event, targetUid, injectionMode, injectionTimeout,
                                       policyFlags);
}

static InputEventInjectionResult injectMotionEvent(
        InputDispatcher& dispatcher, int32_t action, int32_t source, ui::LogicalDisplayId displayId,
        const PointF& position = {100, 200},
        const PointF& cursorPosition = {AMOTION_EVENT_INVALID_CURSOR_POSITION,
                                        AMOTION_EVENT_INVALID_CURSOR_POSITION},
        std::chrono::milliseconds injectionTimeout = INJECT_EVENT_TIMEOUT,
        InputEventInjectionSync injectionMode = InputEventInjectionSync::WAIT_FOR_RESULT,
        nsecs_t eventTime = systemTime(SYSTEM_TIME_MONOTONIC),
        std::optional<gui::Uid> targetUid = {}, uint32_t policyFlags = DEFAULT_POLICY_FLAGS) {
    MotionEventBuilder motionBuilder =
            MotionEventBuilder(action, source)
                    .displayId(displayId)
                    .eventTime(eventTime)
                    .rawXCursorPosition(cursorPosition.x)
                    .rawYCursorPosition(cursorPosition.y)
                    .pointer(
                            PointerBuilder(/*id=*/0, ToolType::FINGER).x(position.x).y(position.y));
    if (MotionEvent::getActionMasked(action) == ACTION_DOWN) {
        motionBuilder.downTime(eventTime);
    }

    // Inject event until dispatch out.
    return injectMotionEvent(dispatcher, motionBuilder.build(), injectionTimeout, injectionMode,
                             targetUid, policyFlags);
}

static InputEventInjectionResult injectMotionDown(InputDispatcher& dispatcher, int32_t source,
                                                  ui::LogicalDisplayId displayId,
                                                  const PointF& location = {100, 200}) {
    return injectMotionEvent(dispatcher, AMOTION_EVENT_ACTION_DOWN, source, displayId, location);
}

static InputEventInjectionResult injectMotionUp(InputDispatcher& dispatcher, int32_t source,
                                                ui::LogicalDisplayId displayId,
                                                const PointF& location = {100, 200}) {
    return injectMotionEvent(dispatcher, AMOTION_EVENT_ACTION_UP, source, displayId, location);
}

static NotifyKeyArgs generateKeyArgs(
        int32_t action, ui::LogicalDisplayId displayId = ui::LogicalDisplayId::INVALID) {
    nsecs_t currentTime = systemTime(SYSTEM_TIME_MONOTONIC);
    // Define a valid key event.
    NotifyKeyArgs args(InputEvent::nextId(), currentTime, /*readTime=*/0, DEVICE_ID,
                       AINPUT_SOURCE_KEYBOARD, displayId, POLICY_FLAG_PASS_TO_USER, action,
                       /*flags=*/0, AKEYCODE_A, KEY_A, AMETA_NONE, currentTime);

    return args;
}

[[nodiscard]] static NotifyMotionArgs generateMotionArgs(int32_t action, int32_t source,
                                                         ui::LogicalDisplayId displayId,
                                                         const std::vector<PointF>& points) {
    size_t pointerCount = points.size();
    if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_UP) {
        EXPECT_EQ(1U, pointerCount) << "Actions DOWN and UP can only contain a single pointer";
    }

    PointerProperties pointerProperties[pointerCount];
    PointerCoords pointerCoords[pointerCount];

    for (size_t i = 0; i < pointerCount; i++) {
        pointerProperties[i].clear();
        pointerProperties[i].id = i;
        pointerProperties[i].toolType = ToolType::FINGER;

        pointerCoords[i].clear();
        pointerCoords[i].setAxisValue(AMOTION_EVENT_AXIS_X, points[i].x);
        pointerCoords[i].setAxisValue(AMOTION_EVENT_AXIS_Y, points[i].y);
    }

    nsecs_t currentTime = systemTime(SYSTEM_TIME_MONOTONIC);
    // Define a valid motion event.
    NotifyMotionArgs args(InputEvent::nextId(), currentTime, /*readTime=*/0, DEVICE_ID, source,
                          displayId, POLICY_FLAG_PASS_TO_USER, action, /*actionButton=*/0,
                          /*flags=*/0, AMETA_NONE, /*buttonState=*/0, MotionClassification::NONE,
                          AMOTION_EVENT_EDGE_FLAG_NONE, pointerCount, pointerProperties,
                          pointerCoords, /*xPrecision=*/0, /*yPrecision=*/0,
                          AMOTION_EVENT_INVALID_CURSOR_POSITION,
                          AMOTION_EVENT_INVALID_CURSOR_POSITION, currentTime, /*videoFrames=*/{});

    return args;
}

static NotifyMotionArgs generateTouchArgs(int32_t action, const std::vector<PointF>& points) {
    return generateMotionArgs(action, AINPUT_SOURCE_TOUCHSCREEN, DISPLAY_ID, points);
}

static NotifyMotionArgs generateMotionArgs(int32_t action, int32_t source,
                                           ui::LogicalDisplayId displayId) {
    return generateMotionArgs(action, source, displayId, {PointF{100, 200}});
}

static NotifyPointerCaptureChangedArgs generatePointerCaptureChangedArgs(
        const PointerCaptureRequest& request) {
    return NotifyPointerCaptureChangedArgs(InputEvent::nextId(), systemTime(SYSTEM_TIME_MONOTONIC),
                                           request);
}

} // namespace

/**
 * When a window unexpectedly disposes of its input channel, policy should be notified about the
 * broken channel.
 */
TEST_F(InputDispatcherTest, WhenInputChannelBreaks_PolicyIsNotified) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher,
                                                             "Window that breaks its input channel",
                                                             ui::LogicalDisplayId::DEFAULT);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    // Window closes its channel, but the window remains.
    window->destroyReceiver();
    mFakePolicy->assertNotifyInputChannelBrokenWasCalled(window->getInfo()->token);
}

TEST_F(InputDispatcherTest, SetInputWindow_SingleWindowTouch) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    // Window should receive motion event.
    window->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
}

using InputDispatcherDeathTest = InputDispatcherTest;

/**
 * When 'onWindowInfosChanged' arguments contain a duplicate entry for the same window, dispatcher
 * should crash.
 */
TEST_F(InputDispatcherDeathTest, DuplicateWindowInfosAbortDispatcher) {
    testing::GTEST_FLAG(death_test_style) = "threadsafe";
    ScopedSilentDeath _silentDeath;

    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);
    ASSERT_DEATH(mDispatcher->onWindowInfosChanged(
                         {{*window->getInfo(), *window->getInfo()}, {}, 0, 0}),
                 "Incorrect WindowInfosUpdate provided");
}

TEST_F(InputDispatcherTest, WhenDisplayNotSpecified_InjectMotionToDefaultDisplay) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    // Inject a MotionEvent to an unknown display.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::INVALID))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    // Window should receive motion event.
    window->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
}

/**
 * Calling onWindowInfosChanged once should not cause any issues.
 * This test serves as a sanity check for the next test, where onWindowInfosChanged is
 * called twice.
 */
TEST_F(InputDispatcherTest, SetInputWindowOnceWithSingleTouchWindow) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 100, 100));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {50, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    // Window should receive motion event.
    window->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
}

/**
 * Calling onWindowInfosChanged twice, with the same info, should not cause any issues.
 */
TEST_F(InputDispatcherTest, SetInputWindowTwice_SingleWindowTouch) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 100, 100));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {50, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    // Window should receive motion event.
    window->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
}

// The foreground window should receive the first touch down event.
TEST_F(InputDispatcherTest, SetInputWindow_MultiWindowsTouch) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> windowTop = sp<FakeWindowHandle>::make(application, mDispatcher, "Top",
                                                                ui::LogicalDisplayId::DEFAULT);
    sp<FakeWindowHandle> windowSecond =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Second",
                                       ui::LogicalDisplayId::DEFAULT);

    mDispatcher->onWindowInfosChanged(
            {{*windowTop->getInfo(), *windowSecond->getInfo()}, {}, 0, 0});
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    // Top window should receive the touch down event. Second window should not receive anything.
    windowTop->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    windowSecond->assertNoEvents();
}

/**
 * Two windows: A top window, and a wallpaper behind the window.
 * Touch goes to the top window, and then top window disappears. Ensure that wallpaper window
 * gets ACTION_CANCEL.
 * 1. foregroundWindow <-- dup touch to wallpaper
 * 2. wallpaperWindow <-- is wallpaper
 */
TEST_F(InputDispatcherTest, WhenForegroundWindowDisappears_WallpaperTouchIsCanceled) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> foregroundWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Foreground",
                                       ui::LogicalDisplayId::DEFAULT);
    foregroundWindow->setDupTouchToWallpaper(true);
    sp<FakeWindowHandle> wallpaperWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Wallpaper",
                                       ui::LogicalDisplayId::DEFAULT);
    wallpaperWindow->setIsWallpaper(true);

    mDispatcher->onWindowInfosChanged(
            {{*foregroundWindow->getInfo(), *wallpaperWindow->getInfo()}, {}, 0, 0});
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                        .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(200))
                                        .build()))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    // Both foreground window and its wallpaper should receive the touch down
    foregroundWindow->consumeMotionDown();
    wallpaperWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT, EXPECTED_WALLPAPER_FLAGS);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                        .pointer(PointerBuilder(0, ToolType::FINGER).x(110).y(200))
                                        .build()))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    foregroundWindow->consumeMotionEvent(WithMotionAction(ACTION_MOVE));
    wallpaperWindow->consumeMotionMove(ui::LogicalDisplayId::DEFAULT, EXPECTED_WALLPAPER_FLAGS);

    // Now the foreground window goes away, but the wallpaper stays
    mDispatcher->onWindowInfosChanged({{*wallpaperWindow->getInfo()}, {}, 0, 0});
    foregroundWindow->consumeMotionCancel();
    // Since the "parent" window of the wallpaper is gone, wallpaper should receive cancel, too.
    wallpaperWindow->consumeMotionCancel(ui::LogicalDisplayId::DEFAULT, EXPECTED_WALLPAPER_FLAGS);
}

/**
 * Two fingers down on the window, and lift off the first finger.
 * Next, cancel the gesture to the window by removing the window. Make sure that the CANCEL event
 * contains a single pointer.
 */
TEST_F(InputDispatcherTest, CancelAfterPointer0Up) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    // First touch pointer down on right window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                      .build());
    // Second touch pointer down
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(110).y(100))
                                      .build());
    // First touch pointer lifts. The second one remains down
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_0_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(110).y(100))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_DOWN));
    window->consumeMotionEvent(WithMotionAction(POINTER_1_DOWN));
    window->consumeMotionEvent(WithMotionAction(POINTER_0_UP));

    // Remove the window. The gesture should be canceled
    mDispatcher->onWindowInfosChanged({{}, {}, 0, 0});
    const std::map<int32_t, PointF> expectedPointers{{1, PointF{110, 100}}};
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithPointers(expectedPointers)));
}

/**
 * Same test as WhenForegroundWindowDisappears_WallpaperTouchIsCanceled above,
 * with the following differences:
 * After ACTION_DOWN, Wallpaper window hangs up its channel, which forces the dispatcher to
 * clean up the connection.
 * This later may crash dispatcher during ACTION_CANCEL synthesis, if the dispatcher is not careful.
 * Ensure that there's no crash in the dispatcher.
 */
TEST_F(InputDispatcherTest, WhenWallpaperDisappears_NoCrash) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> foregroundWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Foreground",
                                       ui::LogicalDisplayId::DEFAULT);
    foregroundWindow->setDupTouchToWallpaper(true);
    sp<FakeWindowHandle> wallpaperWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Wallpaper",
                                       ui::LogicalDisplayId::DEFAULT);
    wallpaperWindow->setIsWallpaper(true);

    mDispatcher->onWindowInfosChanged(
            {{*foregroundWindow->getInfo(), *wallpaperWindow->getInfo()}, {}, 0, 0});
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {100, 200}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    // Both foreground window and its wallpaper should receive the touch down
    foregroundWindow->consumeMotionDown();
    wallpaperWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT, EXPECTED_WALLPAPER_FLAGS);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {110, 200}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    foregroundWindow->consumeMotionMove();
    wallpaperWindow->consumeMotionMove(ui::LogicalDisplayId::DEFAULT, EXPECTED_WALLPAPER_FLAGS);

    // Wallpaper closes its channel, but the window remains.
    wallpaperWindow->destroyReceiver();
    mFakePolicy->assertNotifyInputChannelBrokenWasCalled(wallpaperWindow->getInfo()->token);

    // Now the foreground window goes away, but the wallpaper stays, even though its channel
    // is no longer valid.
    mDispatcher->onWindowInfosChanged({{*wallpaperWindow->getInfo()}, {}, 0, 0});
    foregroundWindow->consumeMotionCancel();
}

/**
 * Two windows: left and right, and a separate wallpaper window underneath each. Device A sends a
 * down event to the left window. Device B sends a down event to the right window. Next, the right
 * window disappears. Both the right window and its wallpaper window should receive cancel event.
 * The left window and its wallpaper window should not receive any events.
 */
TEST_F(InputDispatcherTest, MultiDeviceDisappearingWindowWithWallpaperWindows) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> leftForegroundWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Left foreground window",
                                       ui::LogicalDisplayId::DEFAULT);
    leftForegroundWindow->setFrame(Rect(0, 0, 100, 100));
    leftForegroundWindow->setDupTouchToWallpaper(true);
    sp<FakeWindowHandle> leftWallpaperWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Left wallpaper window",
                                       ui::LogicalDisplayId::DEFAULT);
    leftWallpaperWindow->setFrame(Rect(0, 0, 100, 100));
    leftWallpaperWindow->setIsWallpaper(true);

    sp<FakeWindowHandle> rightForegroundWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Right foreground window",
                                       ui::LogicalDisplayId::DEFAULT);
    rightForegroundWindow->setFrame(Rect(100, 0, 200, 100));
    rightForegroundWindow->setDupTouchToWallpaper(true);
    sp<FakeWindowHandle> rightWallpaperWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Right wallpaper window",
                                       ui::LogicalDisplayId::DEFAULT);
    rightWallpaperWindow->setFrame(Rect(100, 0, 200, 100));
    rightWallpaperWindow->setIsWallpaper(true);

    mDispatcher->onWindowInfosChanged(
            {{*leftForegroundWindow->getInfo(), *leftWallpaperWindow->getInfo(),
              *rightForegroundWindow->getInfo(), *rightWallpaperWindow->getInfo()},
             {},
             0,
             0});

    const DeviceId deviceA = 9;
    const DeviceId deviceB = 3;
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .deviceId(deviceA)
                                      .build());
    leftForegroundWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(deviceA)));
    leftWallpaperWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN),
                                                  WithDeviceId(deviceA),
                                                  WithFlags(EXPECTED_WALLPAPER_FLAGS)));

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(150).y(50))
                                      .deviceId(deviceB)
                                      .build());
    rightForegroundWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(deviceB)));
    rightWallpaperWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN),
                                                   WithDeviceId(deviceB),
                                                   WithFlags(EXPECTED_WALLPAPER_FLAGS)));

    // Now right foreground window disappears, but right wallpaper window remains.
    mDispatcher->onWindowInfosChanged(
            {{*leftForegroundWindow->getInfo(), *leftWallpaperWindow->getInfo(),
              *rightWallpaperWindow->getInfo()},
             {},
             0,
             0});

    // Left foreground window and left wallpaper window still exist, and should not receive any
    // events.
    leftForegroundWindow->assertNoEvents();
    leftWallpaperWindow->assertNoEvents();
    // Since right foreground window disappeared, right wallpaper window and right foreground window
    // should receive cancel events.
    rightForegroundWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(deviceB)));
    rightWallpaperWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(deviceB),
                  WithFlags(EXPECTED_WALLPAPER_FLAGS | AMOTION_EVENT_FLAG_CANCELED)));
}

/**
 * Three windows arranged horizontally and without any overlap. Every window has a
 * wallpaper window underneath. The middle window also has SLIPPERY flag.
 * Device A sends a down event to the left window. Device B sends a down event to the middle window.
 * Next, device B sends move event to the right window. Touch for device B should slip from the
 * middle window to the right window. Also, the right wallpaper window should receive a down event.
 * The middle window and its wallpaper window should receive a cancel event. The left window should
 * not receive any events. If device B continues to report events, the right window and its
 * wallpaper window should receive remaining events.
 */
TEST_F(InputDispatcherTest, MultiDeviceSlipperyTouchWithWallpaperWindow) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> leftForegroundWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Left foreground window",
                                       ui::LogicalDisplayId::DEFAULT);
    leftForegroundWindow->setFrame(Rect(0, 0, 100, 100));
    leftForegroundWindow->setDupTouchToWallpaper(true);
    sp<FakeWindowHandle> leftWallpaperWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Left wallpaper window",
                                       ui::LogicalDisplayId::DEFAULT);
    leftWallpaperWindow->setFrame(Rect(0, 0, 100, 100));
    leftWallpaperWindow->setIsWallpaper(true);

    sp<FakeWindowHandle> middleForegroundWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Middle foreground window",
                                       ui::LogicalDisplayId::DEFAULT);
    middleForegroundWindow->setFrame(Rect(100, 0, 200, 100));
    middleForegroundWindow->setDupTouchToWallpaper(true);
    middleForegroundWindow->setSlippery(true);
    sp<FakeWindowHandle> middleWallpaperWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Middle wallpaper window",
                                       ui::LogicalDisplayId::DEFAULT);
    middleWallpaperWindow->setFrame(Rect(100, 0, 200, 100));
    middleWallpaperWindow->setIsWallpaper(true);

    sp<FakeWindowHandle> rightForegroundWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Right foreground window",
                                       ui::LogicalDisplayId::DEFAULT);
    rightForegroundWindow->setFrame(Rect(200, 0, 300, 100));
    rightForegroundWindow->setDupTouchToWallpaper(true);
    sp<FakeWindowHandle> rightWallpaperWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Right wallpaper window",
                                       ui::LogicalDisplayId::DEFAULT);
    rightWallpaperWindow->setFrame(Rect(200, 0, 300, 100));
    rightWallpaperWindow->setIsWallpaper(true);

    mDispatcher->onWindowInfosChanged(
            {{*leftForegroundWindow->getInfo(), *leftWallpaperWindow->getInfo(),
              *middleForegroundWindow->getInfo(), *middleWallpaperWindow->getInfo(),
              *rightForegroundWindow->getInfo(), *rightWallpaperWindow->getInfo()},
             {},
             0,
             0});

    const DeviceId deviceA = 9;
    const DeviceId deviceB = 3;
    // Device A sends a DOWN event to the left window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .deviceId(deviceA)
                                      .build());
    leftForegroundWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(deviceA)));
    leftWallpaperWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN),
                                                  WithDeviceId(deviceA),
                                                  WithFlags(EXPECTED_WALLPAPER_FLAGS)));
    // Device B sends a DOWN event to the middle window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(150).y(50))
                                      .deviceId(deviceB)
                                      .build());
    middleForegroundWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(deviceB)));
    middleWallpaperWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN),
                                                    WithDeviceId(deviceB),
                                                    WithFlags(EXPECTED_WALLPAPER_FLAGS)));
    // Move the events of device B to the top of the right window.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(250).y(50))
                                      .deviceId(deviceB)
                                      .build());
    middleForegroundWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(deviceB)));
    middleWallpaperWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(deviceB),
                  WithFlags(EXPECTED_WALLPAPER_FLAGS | AMOTION_EVENT_FLAG_CANCELED)));
    rightForegroundWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(deviceB)));
    rightWallpaperWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN),
                                                   WithDeviceId(deviceB),
                                                   WithFlags(EXPECTED_WALLPAPER_FLAGS)));
    // Make sure the window on the right can receive the remaining events.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(251).y(51))
                                      .deviceId(deviceB)
                                      .build());
    leftForegroundWindow->assertNoEvents();
    leftWallpaperWindow->assertNoEvents();
    middleForegroundWindow->assertNoEvents();
    middleWallpaperWindow->assertNoEvents();
    rightForegroundWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(deviceB)));
    rightWallpaperWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE),
                                                   WithDeviceId(deviceB),
                                                   WithFlags(EXPECTED_WALLPAPER_FLAGS)));
}

/**
 * Similar to the test above, we have three windows, they are arranged horizontally and without any
 * overlap, and every window has a wallpaper window. The middle window is a simple window, without
 * any special flags. Device A reports a down event that lands in left window. Device B sends a down
 * event to the middle window and then touch is transferred from the middle window to the right
 * window. The right window and its wallpaper window should receive a down event. The middle window
 * and its wallpaper window should receive a cancel event. The left window should not receive any
 * events. Subsequent events reported by device B should go to the right window and its wallpaper.
 */
TEST_F(InputDispatcherTest, MultiDeviceTouchTransferWithWallpaperWindows) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> leftForegroundWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Left foreground window",
                                       ui::LogicalDisplayId::DEFAULT);
    leftForegroundWindow->setFrame(Rect(0, 0, 100, 100));
    leftForegroundWindow->setDupTouchToWallpaper(true);
    sp<FakeWindowHandle> leftWallpaperWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Left wallpaper window",
                                       ui::LogicalDisplayId::DEFAULT);
    leftWallpaperWindow->setFrame(Rect(0, 0, 100, 100));
    leftWallpaperWindow->setIsWallpaper(true);

    sp<FakeWindowHandle> middleForegroundWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Middle foreground window",
                                       ui::LogicalDisplayId::DEFAULT);
    middleForegroundWindow->setFrame(Rect(100, 0, 200, 100));
    middleForegroundWindow->setDupTouchToWallpaper(true);
    sp<FakeWindowHandle> middleWallpaperWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Middle wallpaper window",
                                       ui::LogicalDisplayId::DEFAULT);
    middleWallpaperWindow->setFrame(Rect(100, 0, 200, 100));
    middleWallpaperWindow->setIsWallpaper(true);

    sp<FakeWindowHandle> rightForegroundWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Right foreground window",
                                       ui::LogicalDisplayId::DEFAULT);
    rightForegroundWindow->setFrame(Rect(200, 0, 300, 100));
    rightForegroundWindow->setDupTouchToWallpaper(true);
    sp<FakeWindowHandle> rightWallpaperWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Right wallpaper window",
                                       ui::LogicalDisplayId::DEFAULT);
    rightWallpaperWindow->setFrame(Rect(200, 0, 300, 100));
    rightWallpaperWindow->setIsWallpaper(true);

    mDispatcher->onWindowInfosChanged(
            {{*leftForegroundWindow->getInfo(), *leftWallpaperWindow->getInfo(),
              *middleForegroundWindow->getInfo(), *middleWallpaperWindow->getInfo(),
              *rightForegroundWindow->getInfo(), *rightWallpaperWindow->getInfo()},
             {},
             0,
             0});

    const DeviceId deviceA = 9;
    const DeviceId deviceB = 3;
    // Device A touch down on the left window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .deviceId(deviceA)
                                      .build());
    leftForegroundWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(deviceA)));
    leftWallpaperWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN),
                                                  WithDeviceId(deviceA),
                                                  WithFlags(EXPECTED_WALLPAPER_FLAGS)));
    // Device B touch down on the middle window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(150).y(50))
                                      .deviceId(deviceB)
                                      .build());
    middleForegroundWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(deviceB)));
    middleWallpaperWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN),
                                                    WithDeviceId(deviceB),
                                                    WithFlags(EXPECTED_WALLPAPER_FLAGS)));

    // Transfer touch from the middle window to the right window.
    ASSERT_TRUE(mDispatcher->transferTouchGesture(middleForegroundWindow->getToken(),
                                                  rightForegroundWindow->getToken()));

    middleForegroundWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(deviceB)));
    middleWallpaperWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(deviceB),
                  WithFlags(EXPECTED_WALLPAPER_FLAGS | AMOTION_EVENT_FLAG_CANCELED)));
    rightForegroundWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN),
                                                    WithDeviceId(deviceB),
                                                    WithFlags(AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE)));
    rightWallpaperWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(deviceB),
                  WithFlags(EXPECTED_WALLPAPER_FLAGS | AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE)));

    // Make sure the right window can receive the remaining events.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(251).y(51))
                                      .deviceId(deviceB)
                                      .build());
    leftForegroundWindow->assertNoEvents();
    leftWallpaperWindow->assertNoEvents();
    middleForegroundWindow->assertNoEvents();
    middleWallpaperWindow->assertNoEvents();
    rightForegroundWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE),
                                                    WithDeviceId(deviceB),
                                                    WithFlags(AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE)));
    rightWallpaperWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(deviceB),
                  WithFlags(EXPECTED_WALLPAPER_FLAGS | AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE)));
}

class ShouldSplitTouchFixture : public InputDispatcherTest,
                                public ::testing::WithParamInterface<bool> {};
INSTANTIATE_TEST_SUITE_P(InputDispatcherTest, ShouldSplitTouchFixture,
                         ::testing::Values(true, false));
/**
 * A single window that receives touch (on top), and a wallpaper window underneath it.
 * The top window gets a multitouch gesture.
 * Ensure that wallpaper gets the same gesture.
 */
TEST_P(ShouldSplitTouchFixture, WallpaperWindowReceivesMultiTouch) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> foregroundWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Foreground",
                                       ui::LogicalDisplayId::DEFAULT);
    foregroundWindow->setDupTouchToWallpaper(true);
    foregroundWindow->setPreventSplitting(GetParam());

    sp<FakeWindowHandle> wallpaperWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Wallpaper",
                                       ui::LogicalDisplayId::DEFAULT);
    wallpaperWindow->setIsWallpaper(true);

    mDispatcher->onWindowInfosChanged(
            {{*foregroundWindow->getInfo(), *wallpaperWindow->getInfo()}, {}, 0, 0});

    // Touch down on top window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {100, 100}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    // Both top window and its wallpaper should receive the touch down
    foregroundWindow->consumeMotionDown();
    wallpaperWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT, EXPECTED_WALLPAPER_FLAGS);

    // Second finger down on the top window
    const MotionEvent secondFingerDownEvent =
            MotionEventBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(100).y(100))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(150).y(150))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerDownEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    foregroundWindow->consumeMotionPointerDown(/*pointerIndex=*/1);
    wallpaperWindow->consumeMotionPointerDown(/*pointerIndex=*/1, ui::LogicalDisplayId::DEFAULT,
                                              EXPECTED_WALLPAPER_FLAGS);

    const MotionEvent secondFingerUpEvent =
            MotionEventBuilder(POINTER_0_UP, AINPUT_SOURCE_TOUCHSCREEN)
                    .displayId(ui::LogicalDisplayId::DEFAULT)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(100).y(100))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(150).y(150))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerUpEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    foregroundWindow->consumeMotionPointerUp(/*pointerIdx=*/0,
                                             WithDisplayId(ui::LogicalDisplayId::DEFAULT));
    wallpaperWindow->consumeMotionPointerUp(/*pointerIdx=*/0,
                                            AllOf(WithDisplayId(ui::LogicalDisplayId::DEFAULT),
                                                  WithFlags(EXPECTED_WALLPAPER_FLAGS)));

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_UP,
                                                   AINPUT_SOURCE_TOUCHSCREEN)
                                        .displayId(ui::LogicalDisplayId::DEFAULT)
                                        .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                                        .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER)
                                                         .x(100)
                                                         .y(100))
                                        .build(),
                                INJECT_EVENT_TIMEOUT, InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    foregroundWindow->consumeMotionUp(ui::LogicalDisplayId::DEFAULT);
    wallpaperWindow->consumeMotionUp(ui::LogicalDisplayId::DEFAULT, EXPECTED_WALLPAPER_FLAGS);
}

/**
 * Two windows: a window on the left and window on the right.
 * A third window, wallpaper, is behind both windows, and spans both top windows.
 * The first touch down goes to the left window. A second pointer touches down on the right window.
 * The touch is split, so both left and right windows should receive ACTION_DOWN.
 * The wallpaper will get the full event, so it should receive ACTION_DOWN followed by
 * ACTION_POINTER_DOWN(1).
 */
TEST_F(InputDispatcherTest, TwoWindows_SplitWallpaperTouch) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> leftWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Left",
                                                                 ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect(0, 0, 200, 200));
    leftWindow->setDupTouchToWallpaper(true);

    sp<FakeWindowHandle> rightWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Right",
                                                                  ui::LogicalDisplayId::DEFAULT);
    rightWindow->setFrame(Rect(200, 0, 400, 200));
    rightWindow->setDupTouchToWallpaper(true);

    sp<FakeWindowHandle> wallpaperWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Wallpaper",
                                       ui::LogicalDisplayId::DEFAULT);
    wallpaperWindow->setFrame(Rect(0, 0, 400, 200));
    wallpaperWindow->setIsWallpaper(true);

    mDispatcher->onWindowInfosChanged(
            {{*leftWindow->getInfo(), *rightWindow->getInfo(), *wallpaperWindow->getInfo()},
             {},
             0,
             0});

    // Touch down on left window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {100, 100}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    // Both foreground window and its wallpaper should receive the touch down
    leftWindow->consumeMotionDown();
    wallpaperWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT, EXPECTED_WALLPAPER_FLAGS);

    // Second finger down on the right window
    const MotionEvent secondFingerDownEvent =
            MotionEventBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(100).y(100))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(300).y(100))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerDownEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    leftWindow->consumeMotionMove();
    // Since the touch is split, right window gets ACTION_DOWN
    rightWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    wallpaperWindow->consumeMotionPointerDown(/*pointerIndex=*/1, ui::LogicalDisplayId::DEFAULT,
                                              EXPECTED_WALLPAPER_FLAGS);

    // Now, leftWindow, which received the first finger, disappears.
    mDispatcher->onWindowInfosChanged(
            {{*rightWindow->getInfo(), *wallpaperWindow->getInfo()}, {}, 0, 0});
    leftWindow->consumeMotionCancel();
    // Since a "parent" window of the wallpaper is gone, wallpaper should receive cancel, too.
    wallpaperWindow->consumeMotionCancel(ui::LogicalDisplayId::DEFAULT, EXPECTED_WALLPAPER_FLAGS);

    // The pointer that's still down on the right window moves, and goes to the right window only.
    // As far as the dispatcher's concerned though, both pointers are still present.
    const MotionEvent secondFingerMoveEvent =
            MotionEventBuilder(AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(100).y(100))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(310).y(110))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerMoveEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT));
    rightWindow->consumeMotionMove();

    leftWindow->assertNoEvents();
    rightWindow->assertNoEvents();
    wallpaperWindow->assertNoEvents();
}

/**
 * Two windows: a window on the left with dup touch to wallpaper and window on the right without it.
 * The touch slips to the right window. so left window and wallpaper should receive ACTION_CANCEL
 * The right window should receive ACTION_DOWN.
 */
TEST_F(InputDispatcherTest, WallpaperWindowWhenSlippery) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> leftWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Left",
                                                                 ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect(0, 0, 200, 200));
    leftWindow->setDupTouchToWallpaper(true);
    leftWindow->setSlippery(true);

    sp<FakeWindowHandle> rightWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Right",
                                                                  ui::LogicalDisplayId::DEFAULT);
    rightWindow->setFrame(Rect(200, 0, 400, 200));

    sp<FakeWindowHandle> wallpaperWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Wallpaper",
                                       ui::LogicalDisplayId::DEFAULT);
    wallpaperWindow->setIsWallpaper(true);

    mDispatcher->onWindowInfosChanged(
            {{*leftWindow->getInfo(), *rightWindow->getInfo(), *wallpaperWindow->getInfo()},
             {},
             0,
             0});

    // Touch down on left window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {100, 100}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    // Both foreground window and its wallpaper should receive the touch down
    leftWindow->consumeMotionDown();
    wallpaperWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT, EXPECTED_WALLPAPER_FLAGS);

    // Move to right window, the left window should receive cancel.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {201, 100}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    leftWindow->consumeMotionCancel();
    rightWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    wallpaperWindow->consumeMotionCancel(ui::LogicalDisplayId::DEFAULT, EXPECTED_WALLPAPER_FLAGS);
}

/**
 * The policy typically sets POLICY_FLAG_PASS_TO_USER to the events. But when the display is not
 * interactive, it might stop sending this flag.
 * In this test, we check that if the policy stops sending this flag mid-gesture, we still ensure
 * to have a consistent input stream.
 *
 * Test procedure:
 * DOWN -> POINTER_DOWN -> (stop sending POLICY_FLAG_PASS_TO_USER) -> CANCEL.
 * DOWN (new gesture).
 *
 * In the bad implementation, we could potentially drop the CANCEL event, and get an inconsistent
 * state in the dispatcher. This would cause the final DOWN event to not be delivered to the app.
 *
 * We technically just need a single window here, but we are using two windows (spy on top and a
 * regular window below) to emulate the actual situation where it happens on the device.
 */
TEST_F(InputDispatcherTest, TwoPointerCancelInconsistentPolicy) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> spyWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Spy",
                                                                ui::LogicalDisplayId::DEFAULT);
    spyWindow->setFrame(Rect(0, 0, 200, 200));
    spyWindow->setTrustedOverlay(true);
    spyWindow->setSpy(true);

    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*spyWindow->getInfo(), *window->getInfo()}, {}, 0, 0});
    const int32_t touchDeviceId = 4;

    // Two pointers down
    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .deviceId(touchDeviceId)
                    .policyFlags(DEFAULT_POLICY_FLAGS)
                    .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                    .build());

    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .policyFlags(DEFAULT_POLICY_FLAGS)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(120).y(120))
                                      .build());
    spyWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_DOWN));
    spyWindow->consumeMotionEvent(WithMotionAction(POINTER_1_DOWN));
    window->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_DOWN));
    window->consumeMotionEvent(WithMotionAction(POINTER_1_DOWN));

    // Cancel the current gesture. Send the cancel without the default policy flags.
    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_CANCEL, AINPUT_SOURCE_TOUCHSCREEN)
                    .deviceId(touchDeviceId)
                    .policyFlags(0)
                    .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                    .pointer(PointerBuilder(1, ToolType::FINGER).x(120).y(120))
                    .build());
    spyWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_CANCEL));
    window->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_CANCEL));

    // We don't need to reset the device to reproduce the issue, but the reset event typically
    // follows, so we keep it here to model the actual listener behaviour more closely.
    mDispatcher->notifyDeviceReset({/*id=*/1, systemTime(SYSTEM_TIME_MONOTONIC), touchDeviceId});

    // Start new gesture
    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .deviceId(touchDeviceId)
                    .policyFlags(DEFAULT_POLICY_FLAGS)
                    .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                    .build());
    spyWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_DOWN));
    window->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_DOWN));

    // No more events
    spyWindow->assertNoEvents();
    window->assertNoEvents();
}

/**
 * Same as the above 'TwoPointerCancelInconsistentPolicy' test, but for hovers.
 * The policy typically sets POLICY_FLAG_PASS_TO_USER to the events. But when the display is not
 * interactive, it might stop sending this flag.
 * We've already ensured the consistency of the touch event in this case, and we should also ensure
 * the consistency of the hover event in this case.
 *
 * Test procedure:
 * HOVER_ENTER -> HOVER_MOVE -> (stop sending POLICY_FLAG_PASS_TO_USER) -> HOVER_EXIT
 * HOVER_ENTER -> HOVER_MOVE -> HOVER_EXIT
 *
 * We expect to receive two full streams of hover events.
 */
TEST_F(InputDispatcherTest, HoverEventInconsistentPolicy) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 300, 300));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .policyFlags(DEFAULT_POLICY_FLAGS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(101))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS)
                                      .policyFlags(DEFAULT_POLICY_FLAGS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(101).y(102))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_MOVE));

    // Send hover exit without the default policy flags.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_EXIT, AINPUT_SOURCE_STYLUS)
                                      .policyFlags(0)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(101).y(102))
                                      .build());

    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_EXIT));

    // Send a simple hover event stream, ensure dispatcher not crashed and window can receive
    // right event.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .policyFlags(DEFAULT_POLICY_FLAGS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(200).y(201))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS)
                                      .policyFlags(DEFAULT_POLICY_FLAGS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(201).y(202))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_MOVE));

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_EXIT, AINPUT_SOURCE_STYLUS)
                                      .policyFlags(DEFAULT_POLICY_FLAGS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(201).y(202))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_EXIT));
}

/**
 * Two windows: a window on the left and a window on the right.
 * Mouse is hovered from the right window into the left window.
 * Next, we tap on the left window, where the cursor was last seen.
 * The second tap is done onto the right window.
 * The mouse and tap are from two different devices.
 * We technically don't need to set the downtime / eventtime for these events, but setting these
 * explicitly helps during debugging.
 * This test reproduces a crash where there is a mismatch between the downTime and eventTime.
 * In the buggy implementation, a tap on the right window would cause a crash.
 */
TEST_F(InputDispatcherTest, HoverFromLeftToRightAndTap_legacy) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);

    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> leftWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Left",
                                                                 ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect(0, 0, 200, 200));

    sp<FakeWindowHandle> rightWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Right",
                                                                  ui::LogicalDisplayId::DEFAULT);
    rightWindow->setFrame(Rect(200, 0, 400, 200));

    mDispatcher->onWindowInfosChanged(
            {{*leftWindow->getInfo(), *rightWindow->getInfo()}, {}, 0, 0});
    // All times need to start at the current time, otherwise the dispatcher will drop the events as
    // stale.
    const nsecs_t baseTime = systemTime(SYSTEM_TIME_MONOTONIC);
    const int32_t mouseDeviceId = 6;
    const int32_t touchDeviceId = 4;
    // Move the cursor from right
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE,
                                                   AINPUT_SOURCE_MOUSE)
                                        .deviceId(mouseDeviceId)
                                        .downTime(baseTime + 10)
                                        .eventTime(baseTime + 20)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(100))
                                        .build()));
    rightWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER));

    // .. to the left window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE,
                                                   AINPUT_SOURCE_MOUSE)
                                        .deviceId(mouseDeviceId)
                                        .downTime(baseTime + 10)
                                        .eventTime(baseTime + 30)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(110).y(100))
                                        .build()));
    rightWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_EXIT));
    leftWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER));
    // Now tap the left window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_DOWN,
                                                   AINPUT_SOURCE_TOUCHSCREEN)
                                        .deviceId(touchDeviceId)
                                        .downTime(baseTime + 40)
                                        .eventTime(baseTime + 40)
                                        .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                        .build()));
    leftWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_EXIT));
    leftWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_DOWN));

    // release tap
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_UP,
                                                   AINPUT_SOURCE_TOUCHSCREEN)
                                        .deviceId(touchDeviceId)
                                        .downTime(baseTime + 40)
                                        .eventTime(baseTime + 50)
                                        .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                        .build()));
    leftWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_UP));

    // Tap the window on the right
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_DOWN,
                                                   AINPUT_SOURCE_TOUCHSCREEN)
                                        .deviceId(touchDeviceId)
                                        .downTime(baseTime + 60)
                                        .eventTime(baseTime + 60)
                                        .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                        .build()));
    rightWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_DOWN));

    // release tap
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_UP,
                                                   AINPUT_SOURCE_TOUCHSCREEN)
                                        .deviceId(touchDeviceId)
                                        .downTime(baseTime + 60)
                                        .eventTime(baseTime + 70)
                                        .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                        .build()));
    rightWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_UP));

    // No more events
    leftWindow->assertNoEvents();
    rightWindow->assertNoEvents();
}

/**
 * Two windows: a window on the left and a window on the right.
 * Mouse is hovered from the right window into the left window.
 * Next, we tap on the left window, where the cursor was last seen.
 * The second tap is done onto the right window.
 * The mouse and tap are from two different devices.
 * We technically don't need to set the downtime / eventtime for these events, but setting these
 * explicitly helps during debugging.
 * This test reproduces a crash where there is a mismatch between the downTime and eventTime.
 * In the buggy implementation, a tap on the right window would cause a crash.
 */
TEST_F(InputDispatcherTest, HoverFromLeftToRightAndTap) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);

    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> leftWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Left",
                                                                 ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect(0, 0, 200, 200));

    sp<FakeWindowHandle> rightWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Right",
                                                                  ui::LogicalDisplayId::DEFAULT);
    rightWindow->setFrame(Rect(200, 0, 400, 200));

    mDispatcher->onWindowInfosChanged(
            {{*leftWindow->getInfo(), *rightWindow->getInfo()}, {}, 0, 0});
    // All times need to start at the current time, otherwise the dispatcher will drop the events as
    // stale.
    const nsecs_t baseTime = systemTime(SYSTEM_TIME_MONOTONIC);
    const int32_t mouseDeviceId = 6;
    const int32_t touchDeviceId = 4;
    // Move the cursor from right
    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                    .deviceId(mouseDeviceId)
                    .downTime(baseTime + 10)
                    .eventTime(baseTime + 20)
                    .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(100))
                    .build());
    rightWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER));

    // .. to the left window
    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                    .deviceId(mouseDeviceId)
                    .downTime(baseTime + 10)
                    .eventTime(baseTime + 30)
                    .pointer(PointerBuilder(0, ToolType::MOUSE).x(110).y(100))
                    .build());
    rightWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_EXIT));
    leftWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER));
    // Now tap the left window
    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .deviceId(touchDeviceId)
                    .downTime(baseTime + 40)
                    .eventTime(baseTime + 40)
                    .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                    .build());
    leftWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_DOWN));

    // release tap
    mDispatcher->notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .downTime(baseTime + 40)
                                      .eventTime(baseTime + 50)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                      .build());
    leftWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_UP));

    // Tap the window on the right
    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .deviceId(touchDeviceId)
                    .downTime(baseTime + 60)
                    .eventTime(baseTime + 60)
                    .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                    .build());
    rightWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_DOWN));

    // release tap
    mDispatcher->notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .downTime(baseTime + 60)
                                      .eventTime(baseTime + 70)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .build());
    rightWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_UP));

    // No more events
    leftWindow->assertNoEvents();
    rightWindow->assertNoEvents();
}

/**
 * Start hovering in a window. While this hover is still active, make another window appear on top.
 * The top, obstructing window has no input channel, so it's not supposed to receive input.
 * While the top window is present, the hovering is stopped.
 * Later, hovering gets resumed again.
 * Ensure that new hover gesture is handled correctly.
 * This test reproduces a crash where the HOVER_EXIT event wasn't getting dispatched correctly
 * to the window that's currently being hovered over.
 */
TEST_F(InputDispatcherTest, HoverWhileWindowAppears) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));

    // Only a single window is present at first
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    // Start hovering in the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(100))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));

    // Now, an obscuring window appears!
    sp<FakeWindowHandle> obscuringWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Obscuring window",
                                       ui::LogicalDisplayId::DEFAULT,
                                       /*createInputChannel=*/false);
    obscuringWindow->setFrame(Rect(0, 0, 200, 200));
    obscuringWindow->setTouchOcclusionMode(TouchOcclusionMode::BLOCK_UNTRUSTED);
    obscuringWindow->setOwnerInfo(SECONDARY_WINDOW_PID, SECONDARY_WINDOW_UID);
    obscuringWindow->setNoInputChannel(true);
    obscuringWindow->setFocusable(false);
    obscuringWindow->setAlpha(1.0);
    mDispatcher->onWindowInfosChanged(
            {{*obscuringWindow->getInfo(), *window->getInfo()}, {}, 0, 0});

    // While this new obscuring window is present, the hovering is stopped
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_EXIT, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(100))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_EXIT));

    // Now the obscuring window goes away.
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    // And a new hover gesture starts.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(100))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));
}

/**
 * Same test as 'HoverWhileWindowAppears' above, but here, we also send some HOVER_MOVE events to
 * the obscuring window.
 */
TEST_F(InputDispatcherTest, HoverMoveWhileWindowAppears) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));

    // Only a single window is present at first
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    // Start hovering in the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(100))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));

    // Now, an obscuring window appears!
    sp<FakeWindowHandle> obscuringWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Obscuring window",
                                       ui::LogicalDisplayId::DEFAULT,
                                       /*createInputChannel=*/false);
    obscuringWindow->setFrame(Rect(0, 0, 200, 200));
    obscuringWindow->setTouchOcclusionMode(TouchOcclusionMode::BLOCK_UNTRUSTED);
    obscuringWindow->setOwnerInfo(SECONDARY_WINDOW_PID, SECONDARY_WINDOW_UID);
    obscuringWindow->setNoInputChannel(true);
    obscuringWindow->setFocusable(false);
    obscuringWindow->setAlpha(1.0);
    mDispatcher->onWindowInfosChanged(
            {{*obscuringWindow->getInfo(), *window->getInfo()}, {}, 0, 0});

    // While this new obscuring window is present, the hovering continues. The event can't go to the
    // bottom window due to obstructed touches, so it should generate HOVER_EXIT for that window.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(100))
                                      .build());
    obscuringWindow->assertNoEvents();
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_EXIT));

    // Now the obscuring window goes away.
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    // Hovering continues in the same position. The hovering pointer re-enters the bottom window,
    // so it should generate a HOVER_ENTER
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(100))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));

    // Now the MOVE should be getting dispatched normally
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(110).y(110))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_MOVE));
}

/**
 * Hover mouse over a window, and then send ACTION_SCROLL. Ensure both the hover and the scroll
 * events are delivered to the window.
 */
TEST_F(InputDispatcherTest, HoverMoveAndScroll) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    // Start hovering in the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(110))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(110).y(120))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_MOVE));

    // Scroll with the mouse
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_SCROLL, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(110).y(120))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_SCROLL));
}

using InputDispatcherMultiDeviceTest = InputDispatcherTest;

/**
 * One window. Stylus down on the window. Next, touch from another device goes down. Ensure that
 * touch is dropped, because stylus should be preferred over touch.
 */
TEST_F(InputDispatcherMultiDeviceTest, StylusDownBlocksTouchDown) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    constexpr int32_t touchDeviceId = 4;
    constexpr int32_t stylusDeviceId = 2;

    // Stylus down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(110))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId)));

    // Touch down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(140).y(145))
                                      .build());

    // Touch move
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(141).y(146))
                                      .build());
    // Touch is ignored because stylus is already down

    // Subsequent stylus movements are delivered correctly
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(101).y(111))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId),
                                     WithCoords(101, 111)));

    window->assertNoEvents();
}

/**
 * One window. Stylus down on the window. Next, touch from another device goes down. Ensure that
 * touch is not dropped, because multiple devices are allowed to be active in the same window.
 */
TEST_F(InputDispatcherMultiDeviceTest, StylusDownDoesNotBlockTouchDown) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    constexpr int32_t touchDeviceId = 4;
    constexpr int32_t stylusDeviceId = 2;

    // Stylus down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(110))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId)));

    // Touch down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(140).y(145))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));

    // Touch move
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(141).y(146))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));

    // Stylus move
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(101).y(111))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId),
                                     WithCoords(101, 111)));

    window->assertNoEvents();
}

/**
 * One window and one spy window. Stylus down on the window. Next, touch from another device goes
 * down. Ensure that touch is dropped, because stylus should be preferred over touch.
 * Similar test as above, but with added SPY window.
 */
TEST_F(InputDispatcherMultiDeviceTest, StylusDownWithSpyBlocksTouchDown) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    sp<FakeWindowHandle> spyWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Spy",
                                                                ui::LogicalDisplayId::DEFAULT);
    spyWindow->setFrame(Rect(0, 0, 200, 200));
    spyWindow->setTrustedOverlay(true);
    spyWindow->setSpy(true);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*spyWindow->getInfo(), *window->getInfo()}, {}, 0, 0});

    constexpr int32_t touchDeviceId = 4;
    constexpr int32_t stylusDeviceId = 2;

    // Stylus down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(110))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId)));

    // Touch down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(140).y(145))
                                      .build());

    // Touch move
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(141).y(146))
                                      .build());

    // Touch is ignored because stylus is already down

    // Subsequent stylus movements are delivered correctly
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(101).y(111))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId),
                                     WithCoords(101, 111)));
    spyWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId),
                                        WithCoords(101, 111)));

    window->assertNoEvents();
    spyWindow->assertNoEvents();
}

/**
 * One window and one spy window. Stylus down on the window. Next, touch from another device goes
 * down. Ensure that touch is not dropped, because multiple devices can be active at the same time.
 * Similar test as above, but with added SPY window.
 */
TEST_F(InputDispatcherMultiDeviceTest, StylusDownWithSpyDoesNotBlockTouchDown) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    sp<FakeWindowHandle> spyWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Spy",
                                                                ui::LogicalDisplayId::DEFAULT);
    spyWindow->setFrame(Rect(0, 0, 200, 200));
    spyWindow->setTrustedOverlay(true);
    spyWindow->setSpy(true);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*spyWindow->getInfo(), *window->getInfo()}, {}, 0, 0});

    constexpr int32_t touchDeviceId = 4;
    constexpr int32_t stylusDeviceId = 2;

    // Stylus down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(110))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId)));

    // Touch down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(140).y(145))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));

    // Touch move
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(141).y(146))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));

    // Subsequent stylus movements are delivered correctly
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(101).y(111))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId),
                                     WithCoords(101, 111)));
    spyWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId),
                                        WithCoords(101, 111)));

    window->assertNoEvents();
    spyWindow->assertNoEvents();
}

/**
 * One window. Stylus hover on the window. Next, touch from another device goes down. Ensure that
 * touch is dropped, because stylus hover takes precedence.
 */
TEST_F(InputDispatcherMultiDeviceTest, StylusHoverBlocksTouchDown) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    constexpr int32_t touchDeviceId = 4;
    constexpr int32_t stylusDeviceId = 2;

    // Stylus down on the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(110))
                                      .build());
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithDeviceId(stylusDeviceId)));

    // Touch down on window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(140).y(145))
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(141).y(146))
                                      .build());

    // Touch is ignored because stylus is hovering

    // Subsequent stylus movements are delivered correctly
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(101).y(111))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_MOVE),
                                     WithDeviceId(stylusDeviceId), WithCoords(101, 111)));

    // and subsequent touches continue to be ignored
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(142).y(147))
                                      .build());
    window->assertNoEvents();
}

/**
 * One window. Stylus hover on the window. Next, touch from another device goes down. Ensure that
 * touch is not dropped, because stylus hover and touch can be both active at the same time.
 */
TEST_F(InputDispatcherMultiDeviceTest, StylusHoverDoesNotBlockTouchDown) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    constexpr int32_t touchDeviceId = 4;
    constexpr int32_t stylusDeviceId = 2;

    // Stylus down on the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(110))
                                      .build());
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithDeviceId(stylusDeviceId)));

    // Touch down on window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(140).y(145))
                                      .build());
    // Touch move on window
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(141).y(146))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));

    // Subsequent stylus movements are delivered correctly
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(101).y(111))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_MOVE),
                                     WithDeviceId(stylusDeviceId), WithCoords(101, 111)));

    // and subsequent touches continue to work
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(142).y(147))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));
    window->assertNoEvents();
}

/**
 * One window. Touch down on the window. Then, stylus hover on the window from another device.
 * Ensure that touch is canceled, because stylus hover should take precedence.
 */
TEST_F(InputDispatcherMultiDeviceTest, TouchIsCanceledByStylusHover) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    constexpr int32_t touchDeviceId = 4;
    constexpr int32_t stylusDeviceId = 2;

    // Touch down on window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(140).y(145))
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(141).y(146))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));

    // Stylus hover on the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(110))
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(101).y(111))
                                      .build());
    // Stylus hover movement causes touch to be canceled
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(touchDeviceId),
                                     WithCoords(141, 146)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_ENTER),
                                     WithDeviceId(stylusDeviceId), WithCoords(100, 110)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_MOVE),
                                     WithDeviceId(stylusDeviceId), WithCoords(101, 111)));

    // Subsequent touch movements are ignored
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(142).y(147))
                                      .build());

    window->assertNoEvents();
}

/**
 * One window. Touch down on the window. Then, stylus hover on the window from another device.
 * Ensure that touch is not canceled, because stylus hover can be active at the same time as touch.
 */
TEST_F(InputDispatcherMultiDeviceTest, TouchIsNotCanceledByStylusHover) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    constexpr int32_t touchDeviceId = 4;
    constexpr int32_t stylusDeviceId = 2;

    // Touch down on window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(140).y(145))
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(141).y(146))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));

    // Stylus hover on the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(110))
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(101).y(111))
                                      .build());
    // Stylus hover movement is received normally
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_ENTER),
                                     WithDeviceId(stylusDeviceId), WithCoords(100, 110)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_MOVE),
                                     WithDeviceId(stylusDeviceId), WithCoords(101, 111)));

    // Subsequent touch movements also work
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(142).y(147))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId),
                                     WithCoords(142, 147)));

    window->assertNoEvents();
}

/**
 * One window. Stylus down on the window. Then, stylus from another device goes down. Ensure that
 * the latest stylus takes over. That is, old stylus should be canceled and the new stylus should
 * become active.
 */
TEST_F(InputDispatcherMultiDeviceTest, LatestStylusWins) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    constexpr int32_t stylusDeviceId1 = 3;
    constexpr int32_t stylusDeviceId2 = 5;

    // Touch down on window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId1)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(99).y(100))
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId1)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(101))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId1)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId1)));

    // Second stylus down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId2)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(9).y(10))
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId2)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(10).y(11))
                                      .build());

    // First stylus is canceled, second one takes over.
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(stylusDeviceId1)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId2)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId2)));

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId1)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(101).y(102))
                                      .build());
    // Subsequent stylus movements are delivered correctly
    window->assertNoEvents();
}

/**
 * One window. Stylus down on the window. Then, stylus from another device goes down. Ensure that
 * both stylus devices can function simultaneously.
 */
TEST_F(InputDispatcherMultiDeviceTest, TwoStylusDevicesActiveAtTheSameTime) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    constexpr int32_t stylusDeviceId1 = 3;
    constexpr int32_t stylusDeviceId2 = 5;

    // Touch down on window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId1)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(99).y(100))
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId1)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(101))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId1)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId1)));

    // Second stylus down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId2)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(9).y(10))
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId2)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(10).y(11))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId2)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId2)));

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId1)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(101).y(102))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId1)));
    window->assertNoEvents();
}

/**
 * One window. Touch down on the window. Then, stylus down on the window from another device.
 * Ensure that is canceled, because stylus down should be preferred over touch.
 */
TEST_F(InputDispatcherMultiDeviceTest, TouchIsCanceledByStylusDown) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    constexpr int32_t touchDeviceId = 4;
    constexpr int32_t stylusDeviceId = 2;

    // Touch down on window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(140).y(145))
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(141).y(146))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));

    // Stylus down on the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(110))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(touchDeviceId)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId)));

    // Subsequent stylus movements are delivered correctly
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(101).y(111))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId),
                                     WithCoords(101, 111)));
}

/**
 * One window. Touch down on the window. Then, stylus down on the window from another device.
 * Ensure that both touch and stylus are functioning independently.
 */
TEST_F(InputDispatcherMultiDeviceTest, TouchIsNotCanceledByStylusDown) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    constexpr int32_t touchDeviceId = 4;
    constexpr int32_t stylusDeviceId = 2;

    // Touch down on window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(140).y(145))
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(141).y(146))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));

    // Stylus down on the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(110))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId)));

    // Subsequent stylus movements are delivered correctly
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(101).y(111))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId),
                                     WithCoords(101, 111)));

    // Touch continues to work too
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(148).y(149))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));
}

/**
 * Two windows: a window on the left and a window on the right.
 * Mouse is clicked on the left window and remains down. Touch is touched on the right and remains
 * down. Then, on the left window, also place second touch pointer down.
 * This test tries to reproduce a crash.
 * In the buggy implementation, second pointer down on the left window would cause a crash.
 */
TEST_F(InputDispatcherMultiDeviceTest, MultiDeviceSplitTouch_legacy) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> leftWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Left",
                                                                 ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect(0, 0, 200, 200));

    sp<FakeWindowHandle> rightWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Right",
                                                                  ui::LogicalDisplayId::DEFAULT);
    rightWindow->setFrame(Rect(200, 0, 400, 200));

    mDispatcher->onWindowInfosChanged(
            {{*leftWindow->getInfo(), *rightWindow->getInfo()}, {}, 0, 0});

    const int32_t touchDeviceId = 4;
    const int32_t mouseDeviceId = 6;

    // Start hovering over the left window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(100))
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithDeviceId(mouseDeviceId)));

    // Mouse down on left window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(100))
                                      .build());

    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_EXIT), WithDeviceId(mouseDeviceId)));
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(mouseDeviceId)));

    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_BUTTON_PRESS, AINPUT_SOURCE_MOUSE)
                    .deviceId(mouseDeviceId)
                    .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                    .actionButton(AMOTION_EVENT_BUTTON_PRIMARY)
                    .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(100))
                    .build());
    leftWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_PRESS));

    // First touch pointer down on right window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .build());
    leftWindow->assertNoEvents();

    rightWindow->consumeMotionEvent(WithMotionAction(ACTION_DOWN));

    // Second touch pointer down on left window
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(100).y(100))
                                      .build());
    // Since this is now a new splittable pointer going down on the left window, and it's coming
    // from a different device, the current gesture in the left window (pointer down) should first
    // be canceled.
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(mouseDeviceId)));
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));
    // This MOVE event is not necessary (doesn't carry any new information), but it's there in the
    // current implementation.
    const std::map<int32_t, PointF> expectedPointers{{0, PointF{100, 100}}};
    rightWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithPointers(expectedPointers)));

    leftWindow->assertNoEvents();
    rightWindow->assertNoEvents();
}

/**
 * Two windows: a window on the left and a window on the right.
 * Mouse is clicked on the left window and remains down. Touch is touched on the right and remains
 * down. Then, on the left window, also place second touch pointer down.
 * This test tries to reproduce a crash.
 * In the buggy implementation, second pointer down on the left window would cause a crash.
 */
TEST_F(InputDispatcherMultiDeviceTest, MultiDeviceSplitTouch) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> leftWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Left",
                                                                 ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect(0, 0, 200, 200));

    sp<FakeWindowHandle> rightWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Right",
                                                                  ui::LogicalDisplayId::DEFAULT);
    rightWindow->setFrame(Rect(200, 0, 400, 200));

    mDispatcher->onWindowInfosChanged(
            {{*leftWindow->getInfo(), *rightWindow->getInfo()}, {}, 0, 0});

    const int32_t touchDeviceId = 4;
    const int32_t mouseDeviceId = 6;

    // Start hovering over the left window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(100))
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithDeviceId(mouseDeviceId)));

    // Mouse down on left window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(100))
                                      .build());

    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_EXIT), WithDeviceId(mouseDeviceId)));
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(mouseDeviceId)));

    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_BUTTON_PRESS, AINPUT_SOURCE_MOUSE)
                    .deviceId(mouseDeviceId)
                    .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                    .actionButton(AMOTION_EVENT_BUTTON_PRIMARY)
                    .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(100))
                    .build());
    leftWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_PRESS));

    // First touch pointer down on right window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .build());
    leftWindow->assertNoEvents();

    rightWindow->consumeMotionEvent(WithMotionAction(ACTION_DOWN));

    // Second touch pointer down on left window
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(100).y(100))
                                      .build());
    // Since this is now a new splittable pointer going down on the left window, and it's coming
    // from a different device, it will be split and delivered to left window separately.
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));
    // This MOVE event is not necessary (doesn't carry any new information), but it's there in the
    // current implementation.
    const std::map<int32_t, PointF> expectedPointers{{0, PointF{100, 100}}};
    rightWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithPointers(expectedPointers)));

    leftWindow->assertNoEvents();
    rightWindow->assertNoEvents();
}

/**
 * Two windows: a window on the left and a window on the right.
 * Mouse is hovered on the left window and stylus is hovered on the right window.
 */
TEST_F(InputDispatcherMultiDeviceTest, MultiDeviceHover) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> leftWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Left",
                                                                 ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect(0, 0, 200, 200));

    sp<FakeWindowHandle> rightWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Right",
                                                                  ui::LogicalDisplayId::DEFAULT);
    rightWindow->setFrame(Rect(200, 0, 400, 200));

    mDispatcher->onWindowInfosChanged(
            {{*leftWindow->getInfo(), *rightWindow->getInfo()}, {}, 0, 0});

    const int32_t stylusDeviceId = 3;
    const int32_t mouseDeviceId = 6;

    // Start hovering over the left window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(110))
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithDeviceId(mouseDeviceId)));

    // Stylus hovered on right window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(300).y(100))
                                      .build());
    rightWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithDeviceId(stylusDeviceId)));

    // Subsequent HOVER_MOVE events are dispatched correctly.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(110).y(120))
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_MOVE), WithDeviceId(mouseDeviceId)));

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(310).y(110))
                                      .build());
    rightWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_MOVE), WithDeviceId(stylusDeviceId)));

    leftWindow->assertNoEvents();
    rightWindow->assertNoEvents();
}

/**
 * Three windows: a window on the left and a window on the right.
 * And a spy window that's positioned above all of them.
 * Stylus down on the left window and remains down. Touch goes down on the right and remains down.
 * Check the stream that's received by the spy.
 */
TEST_F(InputDispatcherMultiDeviceTest, MultiDeviceWithSpy_legacy) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> spyWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Spy",
                                                                ui::LogicalDisplayId::DEFAULT);
    spyWindow->setFrame(Rect(0, 0, 400, 400));
    spyWindow->setTrustedOverlay(true);
    spyWindow->setSpy(true);

    sp<FakeWindowHandle> leftWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Left",
                                                                 ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect(0, 0, 200, 200));

    sp<FakeWindowHandle> rightWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Right",
                                                                  ui::LogicalDisplayId::DEFAULT);

    rightWindow->setFrame(Rect(200, 0, 400, 200));

    mDispatcher->onWindowInfosChanged(
            {{*spyWindow->getInfo(), *leftWindow->getInfo(), *rightWindow->getInfo()}, {}, 0, 0});

    const int32_t stylusDeviceId = 1;
    const int32_t touchDeviceId = 2;

    // Stylus down on the left window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(100))
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId)));

    // Touch down on the right window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .build());
    leftWindow->assertNoEvents();
    rightWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));

    // Spy window does not receive touch events, because stylus events take precedence, and it
    // already has an active stylus gesture.

    // Stylus movements continue. They should be delivered to the left window and to the spy window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(110).y(110))
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId)));

    // Further MOVE events keep going to the right window only
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(310).y(110))
                                      .build());
    rightWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));

    spyWindow->assertNoEvents();
    leftWindow->assertNoEvents();
    rightWindow->assertNoEvents();
}

/**
 * Three windows: a window on the left and a window on the right.
 * And a spy window that's positioned above all of them.
 * Stylus down on the left window and remains down. Touch goes down on the right and remains down.
 * Check the stream that's received by the spy.
 */
TEST_F(InputDispatcherMultiDeviceTest, MultiDeviceWithSpy) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> spyWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Spy",
                                                                ui::LogicalDisplayId::DEFAULT);
    spyWindow->setFrame(Rect(0, 0, 400, 400));
    spyWindow->setTrustedOverlay(true);
    spyWindow->setSpy(true);

    sp<FakeWindowHandle> leftWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Left",
                                                                 ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect(0, 0, 200, 200));

    sp<FakeWindowHandle> rightWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Right",
                                                                  ui::LogicalDisplayId::DEFAULT);

    rightWindow->setFrame(Rect(200, 0, 400, 200));

    mDispatcher->onWindowInfosChanged(
            {{*spyWindow->getInfo(), *leftWindow->getInfo(), *rightWindow->getInfo()}, {}, 0, 0});

    const int32_t stylusDeviceId = 1;
    const int32_t touchDeviceId = 2;

    // Stylus down on the left window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(100))
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId)));

    // Touch down on the right window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .build());
    leftWindow->assertNoEvents();
    rightWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));

    // Stylus movements continue. They should be delivered to the left window and to the spy window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(110).y(110))
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId)));

    // Further touch MOVE events keep going to the right window and to the spy
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(310).y(110))
                                      .build());
    rightWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));

    spyWindow->assertNoEvents();
    leftWindow->assertNoEvents();
    rightWindow->assertNoEvents();
}

/**
 * Three windows: a window on the left, a window on the right, and a spy window positioned above
 * both.
 * Check hover in left window and touch down in the right window.
 * At first, spy should receive hover. Spy shouldn't receive touch while stylus is hovering.
 * At the same time, left and right should be getting independent streams of hovering and touch,
 * respectively.
 */
TEST_F(InputDispatcherMultiDeviceTest, MultiDeviceHoverBlocksTouchWithSpy) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> spyWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Spy",
                                                                ui::LogicalDisplayId::DEFAULT);
    spyWindow->setFrame(Rect(0, 0, 400, 400));
    spyWindow->setTrustedOverlay(true);
    spyWindow->setSpy(true);

    sp<FakeWindowHandle> leftWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Left",
                                                                 ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect(0, 0, 200, 200));

    sp<FakeWindowHandle> rightWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Right",
                                                                  ui::LogicalDisplayId::DEFAULT);
    rightWindow->setFrame(Rect(200, 0, 400, 200));

    mDispatcher->onWindowInfosChanged(
            {{*spyWindow->getInfo(), *leftWindow->getInfo(), *rightWindow->getInfo()}, {}, 0, 0});

    const int32_t stylusDeviceId = 1;
    const int32_t touchDeviceId = 2;

    // Stylus hover on the left window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(100))
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithDeviceId(stylusDeviceId)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithDeviceId(stylusDeviceId)));

    // Touch down on the right window. Spy doesn't receive this touch because it already has
    // stylus hovering there.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .build());
    leftWindow->assertNoEvents();
    spyWindow->assertNoEvents();
    rightWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));

    // Stylus movements continue. They should be delivered to the left window and the spy.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(110).y(110))
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_MOVE), WithDeviceId(stylusDeviceId)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_MOVE), WithDeviceId(stylusDeviceId)));

    // Touch movements continue. They should be delivered to the right window only
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(301).y(101))
                                      .build());
    rightWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));

    spyWindow->assertNoEvents();
    leftWindow->assertNoEvents();
    rightWindow->assertNoEvents();
}

/**
 * Three windows: a window on the left, a window on the right, and a spy window positioned above
 * both.
 * Check hover in left window and touch down in the right window.
 * At first, spy should receive hover. Next, spy should receive touch.
 * At the same time, left and right should be getting independent streams of hovering and touch,
 * respectively.
 */
TEST_F(InputDispatcherMultiDeviceTest, MultiDeviceHoverDoesNotBlockTouchWithSpy) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> spyWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Spy",
                                                                ui::LogicalDisplayId::DEFAULT);
    spyWindow->setFrame(Rect(0, 0, 400, 400));
    spyWindow->setTrustedOverlay(true);
    spyWindow->setSpy(true);

    sp<FakeWindowHandle> leftWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Left",
                                                                 ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect(0, 0, 200, 200));

    sp<FakeWindowHandle> rightWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Right",
                                                                  ui::LogicalDisplayId::DEFAULT);
    rightWindow->setFrame(Rect(200, 0, 400, 200));

    mDispatcher->onWindowInfosChanged(
            {{*spyWindow->getInfo(), *leftWindow->getInfo(), *rightWindow->getInfo()}, {}, 0, 0});

    const int32_t stylusDeviceId = 1;
    const int32_t touchDeviceId = 2;

    // Stylus hover on the left window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(100).y(100))
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithDeviceId(stylusDeviceId)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithDeviceId(stylusDeviceId)));

    // Touch down on the right window.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .build());
    leftWindow->assertNoEvents();
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));
    rightWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));

    // Stylus movements continue. They should be delivered to the left window and the spy.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(110).y(110))
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_MOVE), WithDeviceId(stylusDeviceId)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_MOVE), WithDeviceId(stylusDeviceId)));

    // Touch movements continue. They should be delivered to the right window and the spy
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(301).y(101))
                                      .build());
    rightWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));

    spyWindow->assertNoEvents();
    leftWindow->assertNoEvents();
    rightWindow->assertNoEvents();
}

/**
 * On a single window, use two different devices: mouse and touch.
 * Touch happens first, with two pointers going down, and then the first pointer leaving.
 * Mouse is clicked next, which causes the touch stream to be aborted with ACTION_CANCEL.
 * Finally, a second touch pointer goes down again. Ensure the second touch pointer is ignored,
 * because the mouse is currently down, and a POINTER_DOWN event from the touchscreen does not
 * represent a new gesture.
 */
TEST_F(InputDispatcherMultiDeviceTest, MixedTouchAndMouseWithPointerDown_legacy) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 400, 400));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    const int32_t touchDeviceId = 4;
    const int32_t mouseDeviceId = 6;

    // First touch pointer down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .build());
    // Second touch pointer down
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(350).y(100))
                                      .build());
    // First touch pointer lifts. The second one remains down
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_0_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(350).y(100))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_DOWN));
    window->consumeMotionEvent(WithMotionAction(POINTER_1_DOWN));
    window->consumeMotionEvent(WithMotionAction(POINTER_0_UP));

    // Mouse down. The touch should be canceled
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(320).y(100))
                                      .build());

    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(touchDeviceId),
                                     WithPointerCount(1u)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(mouseDeviceId)));

    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_BUTTON_PRESS, AINPUT_SOURCE_MOUSE)
                    .deviceId(mouseDeviceId)
                    .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                    .actionButton(AMOTION_EVENT_BUTTON_PRIMARY)
                    .pointer(PointerBuilder(0, ToolType::MOUSE).x(320).y(100))
                    .build());
    window->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_PRESS));

    // Second touch pointer down.
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_0_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(350).y(100))
                                      .build());
    // Since we already canceled this touch gesture, it will be ignored until a completely new
    // gesture is started. This is easier to implement than trying to keep track of the new pointer
    // and generating an ACTION_DOWN instead of ACTION_POINTER_DOWN.
    // However, mouse movements should continue to work.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(330).y(110))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(mouseDeviceId)));

    window->assertNoEvents();
}

/**
 * On a single window, use two different devices: mouse and touch.
 * Touch happens first, with two pointers going down, and then the first pointer leaving.
 * Mouse is clicked next, which should not interfere with the touch stream.
 * Finally, a second touch pointer goes down again. Ensure the second touch pointer is also
 * delivered correctly.
 */
TEST_F(InputDispatcherMultiDeviceTest, MixedTouchAndMouseWithPointerDown) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 400, 400));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    const int32_t touchDeviceId = 4;
    const int32_t mouseDeviceId = 6;

    // First touch pointer down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .build());
    // Second touch pointer down
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(350).y(100))
                                      .build());
    // First touch pointer lifts. The second one remains down
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_0_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(350).y(100))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_DOWN));
    window->consumeMotionEvent(WithMotionAction(POINTER_1_DOWN));
    window->consumeMotionEvent(WithMotionAction(POINTER_0_UP));

    // Mouse down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(320).y(100))
                                      .build());

    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(mouseDeviceId)));

    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_BUTTON_PRESS, AINPUT_SOURCE_MOUSE)
                    .deviceId(mouseDeviceId)
                    .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                    .actionButton(AMOTION_EVENT_BUTTON_PRIMARY)
                    .pointer(PointerBuilder(0, ToolType::MOUSE).x(320).y(100))
                    .build());
    window->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_PRESS));

    // Second touch pointer down.
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_0_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(350).y(100))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(POINTER_0_DOWN), WithDeviceId(touchDeviceId),
                                     WithPointerCount(2u)));

    // Mouse movements should continue to work
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(330).y(110))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(mouseDeviceId)));

    window->assertNoEvents();
}

/**
 * Inject a touch down and then send a new event via 'notifyMotion'. Ensure the new event cancels
 * the injected event.
 */
TEST_F(InputDispatcherMultiDeviceTest, UnfinishedInjectedEvent_legacy) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 400, 400));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    const int32_t touchDeviceId = 4;
    // Pretend a test injects an ACTION_DOWN mouse event, but forgets to lift up the touch after
    // completion.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(ACTION_DOWN, AINPUT_SOURCE_MOUSE)
                                        .deviceId(ReservedInputDeviceId::VIRTUAL_KEYBOARD_ID)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(50).y(50))
                                        .build()));
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(VIRTUAL_KEYBOARD_ID)));

    // Now a real touch comes. Rather than crashing or dropping the real event, the injected pointer
    // should be canceled and the new gesture should take over.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .build());

    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(VIRTUAL_KEYBOARD_ID)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));
}

/**
 * Inject a touch down and then send a new event via 'notifyMotion'. Ensure the new event runs
 * parallel to the injected event.
 */
TEST_F(InputDispatcherMultiDeviceTest, UnfinishedInjectedEvent) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 400, 400));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    const int32_t touchDeviceId = 4;
    // Pretend a test injects an ACTION_DOWN mouse event, but forgets to lift up the touch after
    // completion.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(ACTION_DOWN, AINPUT_SOURCE_MOUSE)
                                        .deviceId(ReservedInputDeviceId::VIRTUAL_KEYBOARD_ID)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(50).y(50))
                                        .build()));
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(VIRTUAL_KEYBOARD_ID)));

    // Now a real touch comes. The injected pointer will remain, and the new gesture will also be
    // allowed through.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));
}

/**
 * This test is similar to the test above, but the sequence of injected events is different.
 *
 * Two windows: a window on the left and a window on the right.
 * Mouse is hovered over the left window.
 * Next, we tap on the left window, where the cursor was last seen.
 *
 * After that, we inject one finger down onto the right window, and then a second finger down onto
 * the left window.
 * The touch is split, so this last gesture should cause 2 ACTION_DOWN events, one in the right
 * window (first), and then another on the left window (second).
 * This test reproduces a crash where there is a mismatch between the downTime and eventTime.
 * In the buggy implementation, second finger down on the left window would cause a crash.
 */
TEST_F(InputDispatcherMultiDeviceTest, HoverTapAndSplitTouch_legacy) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> leftWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Left",
                                                                 ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect(0, 0, 200, 200));

    sp<FakeWindowHandle> rightWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Right",
                                                                  ui::LogicalDisplayId::DEFAULT);
    rightWindow->setFrame(Rect(200, 0, 400, 200));

    mDispatcher->onWindowInfosChanged(
            {{*leftWindow->getInfo(), *rightWindow->getInfo()}, {}, 0, 0});

    const int32_t mouseDeviceId = 6;
    const int32_t touchDeviceId = 4;
    // Hover over the left window. Keep the cursor there.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER,
                                                   AINPUT_SOURCE_MOUSE)
                                        .deviceId(mouseDeviceId)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(50).y(50))
                                        .build()));
    leftWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER));

    // Tap on left window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_DOWN,
                                                   AINPUT_SOURCE_TOUCHSCREEN)
                                        .deviceId(touchDeviceId)
                                        .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                        .build()));

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_UP,
                                                   AINPUT_SOURCE_TOUCHSCREEN)
                                        .deviceId(touchDeviceId)
                                        .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                        .build()));
    leftWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_EXIT));
    leftWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_DOWN));
    leftWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_UP));

    // First finger down on right window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_DOWN,
                                                   AINPUT_SOURCE_TOUCHSCREEN)
                                        .deviceId(touchDeviceId)
                                        .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                        .build()));
    rightWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_DOWN));

    // Second finger down on the left window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                        .deviceId(touchDeviceId)
                                        .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                        .pointer(PointerBuilder(1, ToolType::FINGER).x(100).y(100))
                                        .build()));
    leftWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_DOWN));
    rightWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_MOVE));

    // No more events
    leftWindow->assertNoEvents();
    rightWindow->assertNoEvents();
}

/**
 * This test is similar to the test above, but the sequence of injected events is different.
 *
 * Two windows: a window on the left and a window on the right.
 * Mouse is hovered over the left window.
 * Next, we tap on the left window, where the cursor was last seen.
 *
 * After that, we send one finger down onto the right window, and then a second finger down onto
 * the left window.
 * The touch is split, so this last gesture should cause 2 ACTION_DOWN events, one in the right
 * window (first), and then another on the left window (second).
 * This test reproduces a crash where there is a mismatch between the downTime and eventTime.
 * In the buggy implementation, second finger down on the left window would cause a crash.
 */
TEST_F(InputDispatcherMultiDeviceTest, HoverTapAndSplitTouch) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> leftWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Left",
                                                                 ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect(0, 0, 200, 200));

    sp<FakeWindowHandle> rightWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Right",
                                                                  ui::LogicalDisplayId::DEFAULT);
    rightWindow->setFrame(Rect(200, 0, 400, 200));

    mDispatcher->onWindowInfosChanged(
            {{*leftWindow->getInfo(), *rightWindow->getInfo()}, {}, 0, 0});

    const int32_t mouseDeviceId = 6;
    const int32_t touchDeviceId = 4;
    // Hover over the left window. Keep the cursor there.
    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                    .deviceId(mouseDeviceId)
                    .pointer(PointerBuilder(0, ToolType::MOUSE).x(50).y(50))
                    .build());
    leftWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER));

    // Tap on left window
    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .deviceId(touchDeviceId)
                    .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                    .build());

    mDispatcher->notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_DOWN), WithDeviceId(touchDeviceId)));
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_UP), WithDeviceId(touchDeviceId)));

    // First finger down on right window
    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .deviceId(touchDeviceId)
                    .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                    .build());
    rightWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_DOWN));

    // Second finger down on the left window
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(300).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(100).y(100))
                                      .build());
    leftWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_DOWN));
    rightWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_MOVE));

    // No more events
    leftWindow->assertNoEvents();
    rightWindow->assertNoEvents();
}

/**
 * Start hovering with a stylus device, and then tap with a touch device. Ensure no crash occurs.
 * While the touch is down, new hover events from the stylus device should be ignored. After the
 * touch is gone, stylus hovering should start working again.
 */
TEST_F(InputDispatcherMultiDeviceTest, StylusHoverIgnoresTouchTap) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    const int32_t stylusDeviceId = 5;
    const int32_t touchDeviceId = 4;
    // Start hovering with stylus
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                        .deviceId(stylusDeviceId)
                                        .pointer(PointerBuilder(0, ToolType::STYLUS).x(50).y(50))
                                        .build()));
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));

    // Finger down on the window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                        .deviceId(touchDeviceId)
                                        .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                        .build()));
    // The touch device should be ignored!

    // Continue hovering with stylus.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE,
                                                   AINPUT_SOURCE_STYLUS)
                                        .deviceId(stylusDeviceId)
                                        .pointer(PointerBuilder(0, ToolType::STYLUS).x(60).y(60))
                                        .build()));
    // Hovers continue to work
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_MOVE), WithDeviceId(stylusDeviceId)));

    // Lift up the finger
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_UP,
                                                   AINPUT_SOURCE_TOUCHSCREEN)
                                        .deviceId(touchDeviceId)
                                        .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                        .build()));

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE,
                                                   AINPUT_SOURCE_STYLUS)
                                        .deviceId(stylusDeviceId)
                                        .pointer(PointerBuilder(0, ToolType::STYLUS).x(70).y(70))
                                        .build()));
    window->consumeMotionEvent(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_MOVE), WithDeviceId(stylusDeviceId)));
    window->assertNoEvents();
}

/**
 * Start hovering with a stylus device, and then tap with a touch device. Ensure no crash occurs.
 * While the touch is down, hovering from the stylus is not affected. After the touch is gone,
 * check that the stylus hovering continues to work.
 */
TEST_F(InputDispatcherMultiDeviceTest, StylusHoverWithTouchTap) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    const int32_t stylusDeviceId = 5;
    const int32_t touchDeviceId = 4;
    // Start hovering with stylus
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(50).y(50))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));

    // Finger down on the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));

    // Continue hovering with stylus.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(60).y(60))
                                      .build());
    // Hovers continue to work
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_MOVE), WithDeviceId(stylusDeviceId)));

    // Lift up the finger
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_UP), WithDeviceId(touchDeviceId)));

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(70).y(70))
                                      .build());
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_MOVE), WithDeviceId(stylusDeviceId)));
    window->assertNoEvents();
}

/**
 * If stylus is down anywhere on the screen, then touches should not be delivered to windows that
 * have InputConfig::GLOBAL_STYLUS_BLOCKS_TOUCH.
 *
 * Two windows: one on the left and one on the right.
 * The window on the right has GLOBAL_STYLUS_BLOCKS_TOUCH config.
 * Stylus down on the left window, and then touch down on the right window.
 * Check that the right window doesn't get touches while the stylus is down on the left window.
 */
TEST_F(InputDispatcherMultiDeviceTest, GlobalStylusDownBlocksTouch) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> leftWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Left window",
                                       ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect(0, 0, 100, 100));

    sp<FakeWindowHandle> sbtRightWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher,
                                       "Stylus blocks touch (right) window",
                                       ui::LogicalDisplayId::DEFAULT);
    sbtRightWindow->setFrame(Rect(100, 100, 200, 200));
    sbtRightWindow->setGlobalStylusBlocksTouch(true);

    mDispatcher->onWindowInfosChanged(
            {{*leftWindow->getInfo(), *sbtRightWindow->getInfo()}, {}, 0, 0});

    const int32_t stylusDeviceId = 5;
    const int32_t touchDeviceId = 4;

    // Stylus down in the left window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(50).y(52))
                                      .deviceId(stylusDeviceId)
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId)));

    // Finger tap on the right window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(150).y(151))
                                      .deviceId(touchDeviceId)
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(150).y(151))
                                      .deviceId(touchDeviceId)
                                      .build());

    // The touch should be blocked, because stylus is down somewhere else on screen!
    sbtRightWindow->assertNoEvents();

    // Continue stylus motion, and ensure it's not impacted.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(51).y(53))
                                      .deviceId(stylusDeviceId)
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_UP, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(51).y(53))
                                      .deviceId(stylusDeviceId)
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId)));
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_UP), WithDeviceId(stylusDeviceId)));

    // Now that the stylus gesture is done, touches should be getting delivered correctly.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(151).y(153))
                                      .deviceId(touchDeviceId)
                                      .build());
    sbtRightWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));
}

/**
 * If stylus is hovering anywhere on the screen, then touches should not be delivered to windows
 * that have InputConfig::GLOBAL_STYLUS_BLOCKS_TOUCH.
 *
 * Two windows: one on the left and one on the right.
 * The window on the right has GLOBAL_STYLUS_BLOCKS_TOUCH config.
 * Stylus hover on the left window, and then touch down on the right window.
 * Check that the right window doesn't get touches while the stylus is hovering on the left window.
 */
TEST_F(InputDispatcherMultiDeviceTest, GlobalStylusHoverBlocksTouch) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> leftWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Left window",
                                       ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect(0, 0, 100, 100));

    sp<FakeWindowHandle> sbtRightWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher,
                                       "Stylus blocks touch (right) window",
                                       ui::LogicalDisplayId::DEFAULT);
    sbtRightWindow->setFrame(Rect(100, 100, 200, 200));
    sbtRightWindow->setGlobalStylusBlocksTouch(true);

    mDispatcher->onWindowInfosChanged(
            {{*leftWindow->getInfo(), *sbtRightWindow->getInfo()}, {}, 0, 0});

    const int32_t stylusDeviceId = 5;
    const int32_t touchDeviceId = 4;

    // Stylus hover in the left window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(50).y(52))
                                      .deviceId(stylusDeviceId)
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithDeviceId(stylusDeviceId)));

    // Finger tap on the right window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(150).y(151))
                                      .deviceId(touchDeviceId)
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(150).y(151))
                                      .deviceId(touchDeviceId)
                                      .build());

    // The touch should be blocked, because stylus is hovering somewhere else on screen!
    sbtRightWindow->assertNoEvents();

    // Continue stylus motion, and ensure it's not impacted.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(51).y(53))
                                      .deviceId(stylusDeviceId)
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_EXIT, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(51).y(53))
                                      .deviceId(stylusDeviceId)
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_MOVE), WithDeviceId(stylusDeviceId)));
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_EXIT), WithDeviceId(stylusDeviceId)));

    // Now that the stylus gesture is done, touches should be getting delivered correctly.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(151).y(153))
                                      .deviceId(touchDeviceId)
                                      .build());
    sbtRightWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));
}

/**
 * A spy window above a window with no input channel.
 * Start hovering with a stylus device, and then tap with it.
 * Ensure spy window receives the entire sequence.
 */
TEST_F(InputDispatcherTest, StylusHoverAndDownNoInputChannel) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> spyWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Spy",
                                                                ui::LogicalDisplayId::DEFAULT);
    spyWindow->setFrame(Rect(0, 0, 200, 200));
    spyWindow->setTrustedOverlay(true);
    spyWindow->setSpy(true);
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setNoInputChannel(true);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*spyWindow->getInfo(), *window->getInfo()}, {}, 0, 0});

    // Start hovering with stylus
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(50).y(50))
                                      .build());
    spyWindow->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));
    // Stop hovering
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_EXIT, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(50).y(50))
                                      .build());
    spyWindow->consumeMotionEvent(WithMotionAction(ACTION_HOVER_EXIT));

    // Stylus touches down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(50).y(50))
                                      .build());
    spyWindow->consumeMotionEvent(WithMotionAction(ACTION_DOWN));

    // Stylus goes up
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_UP, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(50).y(50))
                                      .build());
    spyWindow->consumeMotionEvent(WithMotionAction(ACTION_UP));

    // Again hover
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(50).y(50))
                                      .build());
    spyWindow->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));
    // Stop hovering
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_EXIT, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(50).y(50))
                                      .build());
    spyWindow->consumeMotionEvent(WithMotionAction(ACTION_HOVER_EXIT));

    // No more events
    spyWindow->assertNoEvents();
    window->assertNoEvents();
}

/**
 * A stale stylus HOVER_EXIT event is injected. Since it's a stale event, it should generally be
 * rejected. But since we already have an ongoing gesture, this event should be processed.
 * This prevents inconsistent events being handled inside the dispatcher.
 */
TEST_F(InputDispatcherTest, StaleStylusHoverGestureIsComplete) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    // Start hovering with stylus
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(50).y(50))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));

    NotifyMotionArgs hoverExit = MotionArgsBuilder(ACTION_HOVER_EXIT, AINPUT_SOURCE_STYLUS)
                                         .pointer(PointerBuilder(0, ToolType::STYLUS).x(50).y(50))
                                         .build();
    // Make this 'hoverExit' event stale
    mFakePolicy->setStaleEventTimeout(100ms);
    std::this_thread::sleep_for(100ms);

    // It shouldn't be dropped by the dispatcher, even though it's stale.
    mDispatcher->notifyMotion(hoverExit);
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_EXIT));

    // Stylus starts hovering again! There should be no crash.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(51).y(51))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));
}

/**
 * Start hovering with a mouse, and then tap with a touch device. Pilfer the touch stream.
 * Next, click with the mouse device. Both windows (spy and regular) should receive the new mouse
 * ACTION_DOWN event because that's a new gesture, and pilfering should no longer be active.
 * While the mouse is down, new move events from the touch device should be ignored.
 */
TEST_F(InputDispatcherTest, TouchPilferAndMouseMove_legacy) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> spyWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Spy",
                                                                ui::LogicalDisplayId::DEFAULT);
    spyWindow->setFrame(Rect(0, 0, 200, 200));
    spyWindow->setTrustedOverlay(true);
    spyWindow->setSpy(true);
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*spyWindow->getInfo(), *window->getInfo()}, {}, 0, 0});

    const int32_t mouseDeviceId = 7;
    const int32_t touchDeviceId = 4;

    // Hover a bit with mouse first
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(100))
                                      .build());
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithDeviceId(mouseDeviceId)));
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithDeviceId(mouseDeviceId)));

    // Start touching
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .build());
    spyWindow->consumeMotionEvent(WithMotionAction(ACTION_HOVER_EXIT));
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_EXIT));
    spyWindow->consumeMotionEvent(WithMotionAction(ACTION_DOWN));
    window->consumeMotionEvent(WithMotionAction(ACTION_DOWN));

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(55).y(55))
                                      .build());
    spyWindow->consumeMotionEvent(WithMotionAction(ACTION_MOVE));
    window->consumeMotionEvent(WithMotionAction(ACTION_MOVE));

    // Pilfer the stream
    EXPECT_EQ(OK, mDispatcher->pilferPointers(spyWindow->getToken()));
    window->consumeMotionEvent(WithMotionAction(ACTION_CANCEL));

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(60).y(60))
                                      .build());
    spyWindow->consumeMotionEvent(WithMotionAction(ACTION_MOVE));

    // Mouse down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(100))
                                      .build());

    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(touchDeviceId)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(mouseDeviceId)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(mouseDeviceId)));

    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_BUTTON_PRESS, AINPUT_SOURCE_MOUSE)
                    .deviceId(mouseDeviceId)
                    .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                    .actionButton(AMOTION_EVENT_BUTTON_PRIMARY)
                    .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(100))
                    .build());
    spyWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_PRESS));
    window->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_PRESS));

    // Mouse move!
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(110).y(110))
                                      .build());
    spyWindow->consumeMotionEvent(WithMotionAction(ACTION_MOVE));
    window->consumeMotionEvent(WithMotionAction(ACTION_MOVE));

    // Touch move!
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(65).y(65))
                                      .build());

    // No more events
    spyWindow->assertNoEvents();
    window->assertNoEvents();
}

/**
 * Start hovering with a mouse, and then tap with a touch device. Pilfer the touch stream.
 * Next, click with the mouse device. Both windows (spy and regular) should receive the new mouse
 * ACTION_DOWN event because that's a new gesture, and pilfering should no longer be active.
 * While the mouse is down, new move events from the touch device should continue to work.
 */
TEST_F(InputDispatcherTest, TouchPilferAndMouseMove) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> spyWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Spy",
                                                                ui::LogicalDisplayId::DEFAULT);
    spyWindow->setFrame(Rect(0, 0, 200, 200));
    spyWindow->setTrustedOverlay(true);
    spyWindow->setSpy(true);
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*spyWindow->getInfo(), *window->getInfo()}, {}, 0, 0});

    const int32_t mouseDeviceId = 7;
    const int32_t touchDeviceId = 4;

    // Hover a bit with mouse first
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(100))
                                      .build());
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithDeviceId(mouseDeviceId)));
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithDeviceId(mouseDeviceId)));

    // Start touching
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .build());

    spyWindow->consumeMotionEvent(WithMotionAction(ACTION_DOWN));
    window->consumeMotionEvent(WithMotionAction(ACTION_DOWN));

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(55).y(55))
                                      .build());
    spyWindow->consumeMotionEvent(WithMotionAction(ACTION_MOVE));
    window->consumeMotionEvent(WithMotionAction(ACTION_MOVE));

    // Pilfer the stream
    EXPECT_EQ(OK, mDispatcher->pilferPointers(spyWindow->getToken()));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(touchDeviceId)));
    // Hover is not pilfered! Only touch.

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(60).y(60))
                                      .build());
    spyWindow->consumeMotionEvent(WithMotionAction(ACTION_MOVE));

    // Mouse down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(100))
                                      .build());

    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_EXIT), WithDeviceId(mouseDeviceId)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(mouseDeviceId)));
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_EXIT), WithDeviceId(mouseDeviceId)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(mouseDeviceId)));

    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_BUTTON_PRESS, AINPUT_SOURCE_MOUSE)
                    .deviceId(mouseDeviceId)
                    .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                    .actionButton(AMOTION_EVENT_BUTTON_PRIMARY)
                    .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(100))
                    .build());
    spyWindow->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_PRESS));
    window->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_PRESS));

    // Mouse move!
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(110).y(110))
                                      .build());
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(mouseDeviceId)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(mouseDeviceId)));

    // Touch move!
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(65).y(65))
                                      .build());
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));

    // No more events
    spyWindow->assertNoEvents();
    window->assertNoEvents();
}

/**
 * On the display, have a single window, and also an area where there's no window.
 * First pointer touches the "no window" area of the screen. Second pointer touches the window.
 * Make sure that the window receives the second pointer, and first pointer is simply ignored.
 */
TEST_F(InputDispatcherTest, SplitWorksWhenEmptyAreaIsTouched) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Window", DISPLAY_ID);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    // Touch down on the empty space
    mDispatcher->notifyMotion(generateTouchArgs(AMOTION_EVENT_ACTION_DOWN, {{-1, -1}}));

    mDispatcher->waitForIdle();
    window->assertNoEvents();

    // Now touch down on the window with another pointer
    mDispatcher->notifyMotion(generateTouchArgs(POINTER_1_DOWN, {{-1, -1}, {10, 10}}));
    mDispatcher->waitForIdle();
    window->consumeMotionDown();
}

/**
 * Same test as above, but instead of touching the empty space, the first touch goes to
 * non-touchable window.
 */
TEST_F(InputDispatcherTest, SplitWorksWhenNonTouchableWindowIsTouched) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window1 =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Window1", DISPLAY_ID);
    window1->setTouchableRegion(Region{{0, 0, 100, 100}});
    window1->setTouchable(false);
    sp<FakeWindowHandle> window2 =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Window2", DISPLAY_ID);
    window2->setTouchableRegion(Region{{100, 0, 200, 100}});

    mDispatcher->onWindowInfosChanged({{*window1->getInfo(), *window2->getInfo()}, {}, 0, 0});

    // Touch down on the non-touchable window
    mDispatcher->notifyMotion(generateTouchArgs(AMOTION_EVENT_ACTION_DOWN, {{50, 50}}));

    mDispatcher->waitForIdle();
    window1->assertNoEvents();
    window2->assertNoEvents();

    // Now touch down on the window with another pointer
    mDispatcher->notifyMotion(generateTouchArgs(POINTER_1_DOWN, {{50, 50}, {150, 50}}));
    mDispatcher->waitForIdle();
    window2->consumeMotionDown();
}

/**
 * When splitting touch events the downTime should be adjusted such that the downTime corresponds
 * to the event time of the first ACTION_DOWN sent to the particular window.
 */
TEST_F(InputDispatcherTest, SplitTouchesSendCorrectActionDownTime) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window1 =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Window1", DISPLAY_ID);
    window1->setTouchableRegion(Region{{0, 0, 100, 100}});
    sp<FakeWindowHandle> window2 =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Window2", DISPLAY_ID);
    window2->setTouchableRegion(Region{{100, 0, 200, 100}});

    mDispatcher->onWindowInfosChanged({{*window1->getInfo(), *window2->getInfo()}, {}, 0, 0});

    // Touch down on the first window
    mDispatcher->notifyMotion(generateTouchArgs(AMOTION_EVENT_ACTION_DOWN, {{50, 50}}));
    mDispatcher->waitForIdle();

    const std::unique_ptr<MotionEvent> firstDown =
            window1->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN)));
    ASSERT_EQ(firstDown->getDownTime(), firstDown->getEventTime());
    window2->assertNoEvents();

    // Now touch down on the window with another pointer
    mDispatcher->notifyMotion(generateTouchArgs(POINTER_1_DOWN, {{50, 50}, {150, 50}}));
    mDispatcher->waitForIdle();

    const std::unique_ptr<MotionEvent> secondDown =
            window2->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN)));
    ASSERT_EQ(secondDown->getDownTime(), secondDown->getEventTime());
    ASSERT_NE(firstDown->getDownTime(), secondDown->getDownTime());
    // We currently send MOVE events to all windows receiving a split touch when there is any change
    // in the touch state, even when none of the pointers in the split window actually moved.
    // Document this behavior in the test.
    window1->consumeMotionMove();

    // Now move the pointer on the second window
    mDispatcher->notifyMotion(generateTouchArgs(AMOTION_EVENT_ACTION_MOVE, {{50, 50}, {151, 51}}));
    mDispatcher->waitForIdle();

    window2->consumeMotionEvent(WithDownTime(secondDown->getDownTime()));
    window1->consumeMotionEvent(WithDownTime(firstDown->getDownTime()));

    // Now add new touch down on the second window
    mDispatcher->notifyMotion(generateTouchArgs(POINTER_2_DOWN, {{50, 50}, {151, 51}, {150, 50}}));
    mDispatcher->waitForIdle();

    window2->consumeMotionEvent(
            AllOf(WithMotionAction(POINTER_1_DOWN), WithDownTime(secondDown->getDownTime())));
    window1->consumeMotionEvent(WithDownTime(firstDown->getDownTime()));

    // Now move the pointer on the first window
    mDispatcher->notifyMotion(
            generateTouchArgs(AMOTION_EVENT_ACTION_MOVE, {{51, 51}, {151, 51}, {150, 50}}));
    mDispatcher->waitForIdle();

    window1->consumeMotionEvent(WithDownTime(firstDown->getDownTime()));
    window2->consumeMotionEvent(WithDownTime(secondDown->getDownTime()));

    // Now add new touch down on the first window
    mDispatcher->notifyMotion(
            generateTouchArgs(POINTER_3_DOWN, {{51, 51}, {151, 51}, {150, 50}, {50, 50}}));
    mDispatcher->waitForIdle();

    window1->consumeMotionEvent(
            AllOf(WithMotionAction(POINTER_1_DOWN), WithDownTime(firstDown->getDownTime())));
    window2->consumeMotionEvent(WithDownTime(secondDown->getDownTime()));
}

TEST_F(InputDispatcherTest, HoverMoveEnterMouseClickAndHoverMoveExit) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> windowLeft = sp<FakeWindowHandle>::make(application, mDispatcher, "Left",
                                                                 ui::LogicalDisplayId::DEFAULT);
    windowLeft->setFrame(Rect(0, 0, 600, 800));
    sp<FakeWindowHandle> windowRight = sp<FakeWindowHandle>::make(application, mDispatcher, "Right",
                                                                  ui::LogicalDisplayId::DEFAULT);
    windowRight->setFrame(Rect(600, 0, 1200, 800));

    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);

    mDispatcher->onWindowInfosChanged(
            {{*windowLeft->getInfo(), *windowRight->getInfo()}, {}, 0, 0});

    // Start cursor position in right window so that we can move the cursor to left window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE,
                                                   AINPUT_SOURCE_MOUSE)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(900).y(400))
                                        .build()));
    windowRight->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER));

    // Move cursor into left window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE,
                                                   AINPUT_SOURCE_MOUSE)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(400))
                                        .build()));
    windowRight->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_EXIT));
    windowLeft->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER));

    // Inject a series of mouse events for a mouse click
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_MOUSE)
                                        .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(400))
                                        .build()));
    windowLeft->consumeMotionEvent(WithMotionAction(ACTION_HOVER_EXIT));
    windowLeft->consumeMotionEvent(WithMotionAction(ACTION_DOWN));

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_BUTTON_PRESS,
                                                   AINPUT_SOURCE_MOUSE)
                                        .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                        .actionButton(AMOTION_EVENT_BUTTON_PRIMARY)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(400))
                                        .build()));
    windowLeft->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_PRESS));

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_BUTTON_RELEASE,
                                                   AINPUT_SOURCE_MOUSE)
                                        .buttonState(0)
                                        .actionButton(AMOTION_EVENT_BUTTON_PRIMARY)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(400))
                                        .build()));
    windowLeft->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_RELEASE));

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_MOUSE)
                                        .buttonState(0)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(400))
                                        .build()));
    windowLeft->consumeMotionUp(ui::LogicalDisplayId::DEFAULT);

    // Move mouse cursor back to right window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE,
                                                   AINPUT_SOURCE_MOUSE)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(900).y(400))
                                        .build()));
    windowRight->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER));

    // No more events
    windowLeft->assertNoEvents();
    windowRight->assertNoEvents();
}

/**
 * Put two fingers down (and don't release them) and click the mouse button.
 * The clicking of mouse is a new ACTION_DOWN event. Since it's from a different device, the
 * currently active gesture should be canceled, and the new one should proceed.
 */
TEST_F(InputDispatcherTest, TwoPointersDownMouseClick_legacy) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 600, 800));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    const int32_t touchDeviceId = 4;
    const int32_t mouseDeviceId = 6;

    // Two pointers down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                      .build());

    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(120).y(120))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_DOWN));
    window->consumeMotionEvent(WithMotionAction(POINTER_1_DOWN));

    // Inject a series of mouse events for a mouse click
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(400))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(touchDeviceId),
                                     WithPointerCount(2u)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(mouseDeviceId)));

    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_BUTTON_PRESS, AINPUT_SOURCE_MOUSE)
                    .deviceId(mouseDeviceId)
                    .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                    .actionButton(AMOTION_EVENT_BUTTON_PRIMARY)
                    .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(400))
                    .build());
    window->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_PRESS));

    // Try to send more touch events while the mouse is down. Since it's a continuation of an
    // already canceled gesture, it should be ignored.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(101).y(101))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(121).y(121))
                                      .build());
    window->assertNoEvents();
}

/**
 * Put two fingers down (and don't release them) and click the mouse button.
 * The clicking of mouse is a new ACTION_DOWN event. Since it's from a different device, the
 * currently active gesture should not be canceled, and the new one should proceed in parallel.
 */
TEST_F(InputDispatcherTest, TwoPointersDownMouseClick) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 600, 800));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    const int32_t touchDeviceId = 4;
    const int32_t mouseDeviceId = 6;

    // Two pointers down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                      .build());

    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(120).y(120))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_DOWN));
    window->consumeMotionEvent(WithMotionAction(POINTER_1_DOWN));

    // Send a series of mouse events for a mouse click
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(400))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(mouseDeviceId)));

    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_BUTTON_PRESS, AINPUT_SOURCE_MOUSE)
                    .deviceId(mouseDeviceId)
                    .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                    .actionButton(AMOTION_EVENT_BUTTON_PRIMARY)
                    .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(400))
                    .build());
    window->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_PRESS));

    // Try to send more touch events while the mouse is down. Since it's a continuation of an
    // already active gesture, it should be sent normally.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(101).y(101))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(121).y(121))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));
    window->assertNoEvents();
}

TEST_F(InputDispatcherTest, HoverWithSpyWindows) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> spyWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Spy",
                                                                ui::LogicalDisplayId::DEFAULT);
    spyWindow->setFrame(Rect(0, 0, 600, 800));
    spyWindow->setTrustedOverlay(true);
    spyWindow->setSpy(true);
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 600, 800));

    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);
    mDispatcher->onWindowInfosChanged({{*spyWindow->getInfo(), *window->getInfo()}, {}, 0, 0});

    // Send mouse cursor to the window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER,
                                                   AINPUT_SOURCE_MOUSE)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(100))
                                        .build()));

    window->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER),
                                     WithSource(AINPUT_SOURCE_MOUSE)));
    spyWindow->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER),
                                        WithSource(AINPUT_SOURCE_MOUSE)));

    window->assertNoEvents();
    spyWindow->assertNoEvents();
}

TEST_F(InputDispatcherTest, MouseAndTouchWithSpyWindows_legacy) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> spyWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Spy",
                                                                ui::LogicalDisplayId::DEFAULT);
    spyWindow->setFrame(Rect(0, 0, 600, 800));
    spyWindow->setTrustedOverlay(true);
    spyWindow->setSpy(true);
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 600, 800));

    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);
    mDispatcher->onWindowInfosChanged({{*spyWindow->getInfo(), *window->getInfo()}, {}, 0, 0});

    // Send mouse cursor to the window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER,
                                                   AINPUT_SOURCE_MOUSE)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(100))
                                        .build()));

    // Move mouse cursor
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE,
                                                   AINPUT_SOURCE_MOUSE)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(110).y(110))
                                        .build()));

    window->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER),
                                     WithSource(AINPUT_SOURCE_MOUSE)));
    spyWindow->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER),
                                        WithSource(AINPUT_SOURCE_MOUSE)));
    window->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_MOVE),
                                     WithSource(AINPUT_SOURCE_MOUSE)));
    spyWindow->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_MOVE),
                                        WithSource(AINPUT_SOURCE_MOUSE)));
    // Touch down on the window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_DOWN,
                                                   AINPUT_SOURCE_TOUCHSCREEN)
                                        .deviceId(SECOND_DEVICE_ID)
                                        .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(200))
                                        .build()));
    window->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_EXIT),
                                     WithSource(AINPUT_SOURCE_MOUSE)));
    spyWindow->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_EXIT),
                                        WithSource(AINPUT_SOURCE_MOUSE)));
    window->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_DOWN),
                                     WithSource(AINPUT_SOURCE_TOUCHSCREEN)));
    spyWindow->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_DOWN),
                                        WithSource(AINPUT_SOURCE_TOUCHSCREEN)));

    // pilfer the motion, retaining the gesture on the spy window.
    EXPECT_EQ(OK, mDispatcher->pilferPointers(spyWindow->getToken()));
    window->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_CANCEL),
                                     WithSource(AINPUT_SOURCE_TOUCHSCREEN)));

    // Touch UP on the window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_UP,
                                                   AINPUT_SOURCE_TOUCHSCREEN)
                                        .deviceId(SECOND_DEVICE_ID)
                                        .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(200))
                                        .build()));
    spyWindow->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_UP),
                                        WithSource(AINPUT_SOURCE_TOUCHSCREEN)));

    // Previously, a touch was pilfered. However, that gesture was just finished. Now, we are going
    // to send a new gesture. It should again go to both windows (spy and the window below), just
    // like the first gesture did, before pilfering. The window configuration has not changed.

    // One more tap - DOWN
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_DOWN,
                                                   AINPUT_SOURCE_TOUCHSCREEN)
                                        .deviceId(SECOND_DEVICE_ID)
                                        .pointer(PointerBuilder(0, ToolType::FINGER).x(250).y(250))
                                        .build()));
    window->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_DOWN),
                                     WithSource(AINPUT_SOURCE_TOUCHSCREEN)));
    spyWindow->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_DOWN),
                                        WithSource(AINPUT_SOURCE_TOUCHSCREEN)));

    // Touch UP on the window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_UP,
                                                   AINPUT_SOURCE_TOUCHSCREEN)
                                        .deviceId(SECOND_DEVICE_ID)
                                        .pointer(PointerBuilder(0, ToolType::FINGER).x(250).y(250))
                                        .build()));
    window->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_UP),
                                     WithSource(AINPUT_SOURCE_TOUCHSCREEN)));
    spyWindow->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_UP),
                                        WithSource(AINPUT_SOURCE_TOUCHSCREEN)));

    window->assertNoEvents();
    spyWindow->assertNoEvents();
}

TEST_F(InputDispatcherTest, MouseAndTouchWithSpyWindows) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> spyWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Spy",
                                                                ui::LogicalDisplayId::DEFAULT);
    spyWindow->setFrame(Rect(0, 0, 600, 800));
    spyWindow->setTrustedOverlay(true);
    spyWindow->setSpy(true);
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 600, 800));

    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);
    mDispatcher->onWindowInfosChanged({{*spyWindow->getInfo(), *window->getInfo()}, {}, 0, 0});

    // Send mouse cursor to the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(100))
                                      .build());

    // Move mouse cursor
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(110).y(110))
                                      .build());

    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithSource(AINPUT_SOURCE_MOUSE)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithSource(AINPUT_SOURCE_MOUSE)));
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_MOVE), WithSource(AINPUT_SOURCE_MOUSE)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_MOVE), WithSource(AINPUT_SOURCE_MOUSE)));
    // Touch down on the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(SECOND_DEVICE_ID)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(200))
                                      .build());
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithSource(AINPUT_SOURCE_TOUCHSCREEN)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithSource(AINPUT_SOURCE_TOUCHSCREEN)));

    // pilfer the motion, retaining the gesture on the spy window.
    EXPECT_EQ(OK, mDispatcher->pilferPointers(spyWindow->getToken()));
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithSource(AINPUT_SOURCE_TOUCHSCREEN)));
    // Mouse hover is not pilfered

    // Touch UP on the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(SECOND_DEVICE_ID)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(200))
                                      .build());
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_UP), WithSource(AINPUT_SOURCE_TOUCHSCREEN)));

    // Previously, a touch was pilfered. However, that gesture was just finished. Now, we are going
    // to send a new gesture. It should again go to both windows (spy and the window below), just
    // like the first gesture did, before pilfering. The window configuration has not changed.

    // One more tap - DOWN
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(SECOND_DEVICE_ID)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(250).y(250))
                                      .build());
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithSource(AINPUT_SOURCE_TOUCHSCREEN)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithSource(AINPUT_SOURCE_TOUCHSCREEN)));

    // Touch UP on the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(SECOND_DEVICE_ID)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(250).y(250))
                                      .build());
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_UP), WithSource(AINPUT_SOURCE_TOUCHSCREEN)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_UP), WithSource(AINPUT_SOURCE_TOUCHSCREEN)));

    // Mouse movement continues normally as well
    // Move mouse cursor
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(120).y(130))
                                      .build());
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_MOVE), WithSource(AINPUT_SOURCE_MOUSE)));
    spyWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_MOVE), WithSource(AINPUT_SOURCE_MOUSE)));

    window->assertNoEvents();
    spyWindow->assertNoEvents();
}

// This test is different from the test above that HOVER_ENTER and HOVER_EXIT events are injected
// directly in this test.
TEST_F(InputDispatcherTest, HoverEnterMouseClickAndHoverExit) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 1200, 800));

    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER,
                                                   AINPUT_SOURCE_MOUSE)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(400))
                                        .build()));
    window->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER));
    // Inject a series of mouse events for a mouse click
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_MOUSE)
                                        .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(400))
                                        .build()));
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_EXIT));
    window->consumeMotionEvent(WithMotionAction(ACTION_DOWN));

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_BUTTON_PRESS,
                                                   AINPUT_SOURCE_MOUSE)
                                        .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                        .actionButton(AMOTION_EVENT_BUTTON_PRIMARY)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(400))
                                        .build()));
    window->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_PRESS));

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_BUTTON_RELEASE,
                                                   AINPUT_SOURCE_MOUSE)
                                        .buttonState(0)
                                        .actionButton(AMOTION_EVENT_BUTTON_PRIMARY)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(400))
                                        .build()));
    window->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_RELEASE));

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_MOUSE)
                                        .buttonState(0)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(400))
                                        .build()));
    window->consumeMotionUp(ui::LogicalDisplayId::DEFAULT);

    // We already canceled the hovering implicitly by injecting the "DOWN" event without lifting the
    // hover first. Therefore, injection of HOVER_EXIT is inconsistent, and should fail.
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_HOVER_EXIT,
                                                   AINPUT_SOURCE_MOUSE)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(400))
                                        .build()));
    window->assertNoEvents();
}

/**
 * Hover over a window, and then remove that window. Make sure that HOVER_EXIT for that event
 * is generated.
 */
TEST_F(InputDispatcherTest, HoverExitIsSentToRemovedWindow) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 1200, 800));

    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER,
                                                   AINPUT_SOURCE_MOUSE)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(400))
                                        .build()));
    window->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER));

    // Remove the window, but keep the channel.
    mDispatcher->onWindowInfosChanged({{}, {}, 0, 0});
    window->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_EXIT));
}

/**
 * Test that invalid HOVER events sent by accessibility do not cause a fatal crash.
 */
TEST_F_WITH_FLAGS(InputDispatcherTest, InvalidA11yHoverStreamDoesNotCrash,
                  REQUIRES_FLAGS_DISABLED(ACONFIG_FLAG(com::android::input::flags,
                                                       a11y_crash_on_inconsistent_event_stream))) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 1200, 800));

    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    MotionEventBuilder hoverEnterBuilder =
            MotionEventBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                    .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(400))
                    .addFlag(AMOTION_EVENT_FLAG_IS_ACCESSIBILITY_EVENT);
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, hoverEnterBuilder.build()));
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, hoverEnterBuilder.build()));
    window->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER));
    window->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER));
}

/**
 * If mouse is hovering when the touch goes down, the hovering should be stopped via HOVER_EXIT.
 */
TEST_F(InputDispatcherTest, TouchDownAfterMouseHover_legacy) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 100, 100));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    const int32_t mouseDeviceId = 7;
    const int32_t touchDeviceId = 4;

    // Start hovering with the mouse
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(10).y(10))
                                      .build());
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithDeviceId(mouseDeviceId)));

    // Touch goes down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .build());

    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_EXIT), WithDeviceId(mouseDeviceId)));
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));
}

/**
 * If mouse is hovering when the touch goes down, the hovering should not be stopped.
 */
TEST_F(InputDispatcherTest, TouchDownAfterMouseHover) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 100, 100));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    const int32_t mouseDeviceId = 7;
    const int32_t touchDeviceId = 4;

    // Start hovering with the mouse
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                                      .deviceId(mouseDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(10).y(10))
                                      .build());
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), WithDeviceId(mouseDeviceId)));

    // Touch goes down
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .deviceId(touchDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));
}

/**
 * Inject a mouse hover event followed by a tap from touchscreen.
 * The tap causes a HOVER_EXIT event to be generated because the current event
 * stream's source has been switched.
 */
TEST_F(InputDispatcherTest, MouseHoverAndTouchTap_legacy) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 100, 100));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(50).y(50))
                                      .build());
    ASSERT_NO_FATAL_FAILURE(
            window->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER),
                                             WithSource(AINPUT_SOURCE_MOUSE))));

    // Tap on the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(10).y(10))
                                      .build());
    ASSERT_NO_FATAL_FAILURE(
            window->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_EXIT),
                                             WithSource(AINPUT_SOURCE_MOUSE))));

    ASSERT_NO_FATAL_FAILURE(
            window->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_DOWN),
                                             WithSource(AINPUT_SOURCE_TOUCHSCREEN))));

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(10).y(10))
                                      .build());
    ASSERT_NO_FATAL_FAILURE(
            window->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_UP),
                                             WithSource(AINPUT_SOURCE_TOUCHSCREEN))));
}

/**
 * Send a mouse hover event followed by a tap from touchscreen.
 * The tap causes a HOVER_EXIT event to be generated because the current event
 * stream's source has been switched.
 */
TEST_F(InputDispatcherTest, MouseHoverAndTouchTap) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 100, 100));

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(50).y(50))
                                      .build());

    window->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER),
                                     WithSource(AINPUT_SOURCE_MOUSE)));

    // Tap on the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(10).y(10))
                                      .build());

    window->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_EXIT),
                                     WithSource(AINPUT_SOURCE_MOUSE)));

    window->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_DOWN),
                                     WithSource(AINPUT_SOURCE_TOUCHSCREEN)));

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(10).y(10))
                                      .build());

    window->consumeMotionEvent(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_UP),
                                     WithSource(AINPUT_SOURCE_TOUCHSCREEN)));
}

TEST_F(InputDispatcherTest, HoverEnterMoveRemoveWindowsInSecondDisplay) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> windowDefaultDisplay =
            sp<FakeWindowHandle>::make(application, mDispatcher, "DefaultDisplay",
                                       ui::LogicalDisplayId::DEFAULT);
    windowDefaultDisplay->setFrame(Rect(0, 0, 600, 800));
    sp<FakeWindowHandle> windowSecondDisplay =
            sp<FakeWindowHandle>::make(application, mDispatcher, "SecondDisplay",
                                       SECOND_DISPLAY_ID);
    windowSecondDisplay->setFrame(Rect(0, 0, 600, 800));

    mDispatcher->onWindowInfosChanged(
            {{*windowDefaultDisplay->getInfo(), *windowSecondDisplay->getInfo()}, {}, 0, 0});

    // Set cursor position in window in default display and check that hover enter and move
    // events are generated.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE,
                                                   AINPUT_SOURCE_MOUSE)
                                        .displayId(ui::LogicalDisplayId::DEFAULT)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(300).y(600))
                                        .build()));
    windowDefaultDisplay->consumeMotionEvent(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER));

    // Remove all windows in secondary display and check that no event happens on window in
    // primary display.
    mDispatcher->onWindowInfosChanged({{*windowDefaultDisplay->getInfo()}, {}, 0, 0});

    windowDefaultDisplay->assertNoEvents();

    // Move cursor position in window in default display and check that only hover move
    // event is generated and not hover enter event.
    mDispatcher->onWindowInfosChanged(
            {{*windowDefaultDisplay->getInfo(), *windowSecondDisplay->getInfo()}, {}, 0, 0});
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE,
                                                   AINPUT_SOURCE_MOUSE)
                                        .displayId(ui::LogicalDisplayId::DEFAULT)
                                        .pointer(PointerBuilder(0, ToolType::MOUSE).x(400).y(700))
                                        .build()));
    windowDefaultDisplay->consumeMotionEvent(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_MOVE),
                  WithSource(AINPUT_SOURCE_MOUSE)));
    windowDefaultDisplay->assertNoEvents();
}

TEST_F(InputDispatcherTest, DispatchMouseEventsUnderCursor) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> windowLeft = sp<FakeWindowHandle>::make(application, mDispatcher, "Left",
                                                                 ui::LogicalDisplayId::DEFAULT);
    windowLeft->setFrame(Rect(0, 0, 600, 800));
    sp<FakeWindowHandle> windowRight = sp<FakeWindowHandle>::make(application, mDispatcher, "Right",
                                                                  ui::LogicalDisplayId::DEFAULT);
    windowRight->setFrame(Rect(600, 0, 1200, 800));

    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);

    mDispatcher->onWindowInfosChanged(
            {{*windowLeft->getInfo(), *windowRight->getInfo()}, {}, 0, 0});

    // Inject an event with coordinate in the area of right window, with mouse cursor in the area of
    // left window. This event should be dispatched to the left window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_MOUSE,
                                ui::LogicalDisplayId::DEFAULT, {610, 400}, {599, 400}));
    windowLeft->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    windowRight->assertNoEvents();
}

TEST_F(InputDispatcherTest, NotifyDeviceReset_CancelsKeyStream) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);
    window->setFocusable(true);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);

    window->consumeFocusEvent(true);

    mDispatcher->notifyKey(generateKeyArgs(AKEY_EVENT_ACTION_DOWN, ui::LogicalDisplayId::DEFAULT));

    // Window should receive key down event.
    window->consumeKeyDown(ui::LogicalDisplayId::DEFAULT);

    // When device reset happens, that key stream should be terminated with FLAG_CANCELED
    // on the app side.
    mDispatcher->notifyDeviceReset({/*id=*/10, /*eventTime=*/20, DEVICE_ID});
    window->consumeKeyUp(ui::LogicalDisplayId::DEFAULT, AKEY_EVENT_FLAG_CANCELED);
}

TEST_F(InputDispatcherTest, NotifyDeviceReset_CancelsMotionStream) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));

    // Window should receive motion down event.
    window->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);

    // When device reset happens, that motion stream should be terminated with ACTION_CANCEL
    // on the app side.
    mDispatcher->notifyDeviceReset({/*id=*/10, /*eventTime=*/20, DEVICE_ID});
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDisplayId(ui::LogicalDisplayId::DEFAULT)));
}

TEST_F(InputDispatcherTest, NotifyDeviceResetCancelsHoveringStream) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(10).y(10))
                                      .build());

    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));

    // When device reset happens, that hover stream should be terminated with ACTION_HOVER_EXIT
    mDispatcher->notifyDeviceReset({/*id=*/10, /*eventTime=*/20, DEVICE_ID});
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_EXIT));

    // After the device has been reset, a new hovering stream can be sent to the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(15).y(15))
                                      .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));
}

TEST_F(InputDispatcherTest, InterceptKeyByPolicy) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);
    window->setFocusable(true);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);

    window->consumeFocusEvent(true);

    const NotifyKeyArgs keyArgs =
            generateKeyArgs(AKEY_EVENT_ACTION_DOWN, ui::LogicalDisplayId::DEFAULT);
    const std::chrono::milliseconds interceptKeyTimeout = 50ms;
    const nsecs_t injectTime = keyArgs.eventTime;
    mFakePolicy->setInterceptKeyTimeout(interceptKeyTimeout);
    mDispatcher->notifyKey(keyArgs);
    // The dispatching time should be always greater than or equal to intercept key timeout.
    window->consumeKeyDown(ui::LogicalDisplayId::DEFAULT);
    ASSERT_TRUE((systemTime(SYSTEM_TIME_MONOTONIC) - injectTime) >=
                std::chrono::nanoseconds(interceptKeyTimeout).count());
}

/**
 * Keys with ACTION_UP are delivered immediately, even if a long 'intercept key timeout' is set.
 */
TEST_F(InputDispatcherTest, InterceptKeyIfKeyUp) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);
    window->setFocusable(true);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);

    window->consumeFocusEvent(true);

    mDispatcher->notifyKey(generateKeyArgs(AKEY_EVENT_ACTION_DOWN, ui::LogicalDisplayId::DEFAULT));
    window->consumeKeyDown(ui::LogicalDisplayId::DEFAULT);

    // Set a value that's significantly larger than the default consumption timeout. If the
    // implementation is correct, the actual value doesn't matter; it won't slow down the test.
    mFakePolicy->setInterceptKeyTimeout(600ms);
    mDispatcher->notifyKey(generateKeyArgs(AKEY_EVENT_ACTION_UP, ui::LogicalDisplayId::DEFAULT));
    // Window should receive key event immediately when same key up.
    window->consumeKeyUp(ui::LogicalDisplayId::DEFAULT);
}

/**
 * Two windows. First is a regular window. Second does not overlap with the first, and has
 * WATCH_OUTSIDE_TOUCH.
 * Both windows are owned by the same UID.
 * Tap first window. Make sure that the second window receives ACTION_OUTSIDE with correct, non-zero
 * coordinates. The coordinates are not zeroed out because both windows are owned by the same UID.
 */
TEST_F(InputDispatcherTest, ActionOutsideForOwnedWindowHasValidCoordinates) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect{0, 0, 100, 100});

    sp<FakeWindowHandle> outsideWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Outside Window",
                                       ui::LogicalDisplayId::DEFAULT);
    outsideWindow->setFrame(Rect{100, 100, 200, 200});
    outsideWindow->setWatchOutsideTouch(true);
    // outsideWindow must be above 'window' to receive ACTION_OUTSIDE events when 'window' is tapped
    mDispatcher->onWindowInfosChanged({{*outsideWindow->getInfo(), *window->getInfo()}, {}, 0, 0});

    // Tap on first window.
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT, {PointF{50, 50}}));
    window->consumeMotionDown();
    // The coordinates of the tap in 'outsideWindow' are relative to its top left corner.
    // Therefore, we should offset them by (100, 100) relative to the screen's top left corner.
    outsideWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_OUTSIDE), WithCoords(-50, -50)));

    // Ensure outsideWindow doesn't get any more events for the gesture.
    mDispatcher->notifyMotion(generateMotionArgs(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT, {PointF{51, 51}}));
    window->consumeMotionMove();
    outsideWindow->assertNoEvents();
}

/**
 * Three windows:
 * - Left window
 * - Right window
 * - Outside window(watch for ACTION_OUTSIDE events)
 * The windows "left" and "outside" share the same owner, the window "right" has a different owner,
 * In order to allow the outside window can receive the ACTION_OUTSIDE events, the outside window is
 * positioned above the "left" and "right" windows, and it doesn't overlap with them.
 *
 * First, device A report a down event landed in the right window, the outside window can receive
 * an ACTION_OUTSIDE event that with zeroed coordinates, the device B report a down event landed
 * in the left window, the outside window can receive an ACTION_OUTSIDE event the with valid
 * coordinates, after these, device A and device B continue report MOVE event, the right and left
 * window can receive it, but outside window event can't receive it.
 */
TEST_F(InputDispatcherTest, ActionOutsideForOwnedWindowHasValidCoordinatesWhenMultiDevice) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> leftWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Left Window",
                                       ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect{0, 0, 100, 100});
    leftWindow->setOwnerInfo(gui::Pid{1}, gui::Uid{101});

    sp<FakeWindowHandle> outsideWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Outside Window",
                                       ui::LogicalDisplayId::DEFAULT);
    outsideWindow->setFrame(Rect{100, 100, 200, 200});
    outsideWindow->setOwnerInfo(gui::Pid{1}, gui::Uid{101});
    outsideWindow->setWatchOutsideTouch(true);

    std::shared_ptr<FakeApplicationHandle> anotherApplication =
            std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> rightWindow =
            sp<FakeWindowHandle>::make(anotherApplication, mDispatcher, "Right Window",
                                       ui::LogicalDisplayId::DEFAULT);
    rightWindow->setFrame(Rect{100, 0, 200, 100});
    rightWindow->setOwnerInfo(gui::Pid{2}, gui::Uid{202});

    // OutsideWindow must be above left window and right window to receive ACTION_OUTSIDE events
    // when left window or right window is tapped
    mDispatcher->onWindowInfosChanged(
            {{*outsideWindow->getInfo(), *leftWindow->getInfo(), *rightWindow->getInfo()},
             {},
             0,
             0});

    const DeviceId deviceA = 9;
    const DeviceId deviceB = 3;

    // Tap on right window use device A
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(150).y(50))
                                      .deviceId(deviceA)
                                      .build());
    leftWindow->assertNoEvents();
    rightWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(deviceA)));
    // Right window is belonged to another owner, so outsideWindow should receive ACTION_OUTSIDE
    // with zeroed coords.
    outsideWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_OUTSIDE), WithDeviceId(deviceA), WithCoords(0, 0)));

    // Tap on left window use device B
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .deviceId(deviceB)
                                      .build());
    leftWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(deviceB)));
    rightWindow->assertNoEvents();
    // Because new gesture down on the left window that has the same owner with outside Window, the
    // outside Window should receive the ACTION_OUTSIDE with coords.
    outsideWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_OUTSIDE), WithDeviceId(deviceB), WithCoords(-50, -50)));

    // Ensure that windows that can only accept outside do not receive remaining gestures
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(151).y(51))
                                      .deviceId(deviceA)
                                      .build());
    leftWindow->assertNoEvents();
    rightWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(deviceA)));

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(51).y(51))
                                      .deviceId(deviceB)
                                      .build());
    leftWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(deviceB)));
    rightWindow->assertNoEvents();
    outsideWindow->assertNoEvents();
}

/**
 * This test documents the behavior of WATCH_OUTSIDE_TOUCH. The window will get ACTION_OUTSIDE when
 * a another pointer causes ACTION_DOWN to be sent to another window for the first time. Only one
 * ACTION_OUTSIDE event is sent per gesture.
 */
TEST_F(InputDispatcherTest, ActionOutsideSentOnlyWhenAWindowIsTouched) {
    // There are three windows that do not overlap. `window` wants to WATCH_OUTSIDE_TOUCH.
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "First Window",
                                       ui::LogicalDisplayId::DEFAULT);
    window->setWatchOutsideTouch(true);
    window->setFrame(Rect{0, 0, 100, 100});
    sp<FakeWindowHandle> secondWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Second Window",
                                       ui::LogicalDisplayId::DEFAULT);
    secondWindow->setFrame(Rect{100, 100, 200, 200});
    sp<FakeWindowHandle> thirdWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Third Window",
                                       ui::LogicalDisplayId::DEFAULT);
    thirdWindow->setFrame(Rect{200, 200, 300, 300});
    mDispatcher->onWindowInfosChanged(
            {{*window->getInfo(), *secondWindow->getInfo(), *thirdWindow->getInfo()}, {}, 0, 0});

    // First pointer lands outside all windows. `window` does not get ACTION_OUTSIDE.
    mDispatcher->notifyMotion(
            generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {PointF{-10, -10}}));
    window->assertNoEvents();
    secondWindow->assertNoEvents();

    // The second pointer lands inside `secondWindow`, which should receive a DOWN event.
    // Now, `window` should get ACTION_OUTSIDE.
    mDispatcher->notifyMotion(generateMotionArgs(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT,
                                                 {PointF{-10, -10}, PointF{105, 105}}));
    const std::map<int32_t, PointF> expectedPointers{{0, PointF{-10, -10}}, {1, PointF{105, 105}}};
    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_OUTSIDE), WithPointers(expectedPointers)));
    secondWindow->consumeMotionDown();
    thirdWindow->assertNoEvents();

    // The third pointer lands inside `thirdWindow`, which should receive a DOWN event. There is
    // no ACTION_OUTSIDE sent to `window` because one has already been sent for this gesture.
    mDispatcher->notifyMotion(
            generateMotionArgs(POINTER_2_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT,
                               {PointF{-10, -10}, PointF{105, 105}, PointF{205, 205}}));
    window->assertNoEvents();
    secondWindow->consumeMotionMove();
    thirdWindow->consumeMotionDown();
}

TEST_F(InputDispatcherTest, OnWindowInfosChanged_RemoveAllWindowsOnDisplay) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);
    window->setFocusable(true);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);

    window->consumeFocusEvent(true);

    const NotifyKeyArgs keyDown =
            generateKeyArgs(AKEY_EVENT_ACTION_DOWN, ui::LogicalDisplayId::DEFAULT);
    const NotifyKeyArgs keyUp =
            generateKeyArgs(AKEY_EVENT_ACTION_UP, ui::LogicalDisplayId::DEFAULT);
    mDispatcher->notifyKey(keyDown);
    mDispatcher->notifyKey(keyUp);

    window->consumeKeyDown(ui::LogicalDisplayId::DEFAULT);
    window->consumeKeyUp(ui::LogicalDisplayId::DEFAULT);

    // All windows are removed from the display. Ensure that we can no longer dispatch to it.
    mDispatcher->onWindowInfosChanged({{}, {}, 0, 0});

    window->consumeFocusEvent(false);

    mDispatcher->notifyKey(keyDown);
    mDispatcher->notifyKey(keyUp);
    window->assertNoEvents();
}

TEST_F(InputDispatcherTest, NonSplitTouchableWindowReceivesMultiTouch) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);
    // Ensure window is non-split and have some transform.
    window->setPreventSplitting(true);
    window->setWindowOffset(20, 40);
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {50, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    window->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);

    const MotionEvent secondFingerDownEvent =
            MotionEventBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .displayId(ui::LogicalDisplayId::DEFAULT)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(50).y(50))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(-30).y(-50))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerDownEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    std::unique_ptr<MotionEvent> event = window->consumeMotionEvent();
    ASSERT_NE(nullptr, event);
    EXPECT_EQ(POINTER_1_DOWN, event->getAction());
    EXPECT_EQ(70, event->getX(0));  // 50 + 20
    EXPECT_EQ(90, event->getY(0));  // 50 + 40
    EXPECT_EQ(-10, event->getX(1)); // -30 + 20
    EXPECT_EQ(-10, event->getY(1)); // -50 + 40
}

/**
 * Two windows: a splittable and a non-splittable.
 * The non-splittable window shouldn't receive any "incomplete" gestures.
 * Send the first pointer to the splittable window, and then touch the non-splittable window.
 * The second pointer should be dropped because the initial window is splittable, so it won't get
 * any pointers outside of it, and the second window is non-splittable, so it shouldn't get any
 * "incomplete" gestures.
 */
TEST_F(InputDispatcherTest, SplittableAndNonSplittableWindows) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> leftWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Left splittable Window",
                                       ui::LogicalDisplayId::DEFAULT);
    leftWindow->setPreventSplitting(false);
    leftWindow->setFrame(Rect(0, 0, 100, 100));
    sp<FakeWindowHandle> rightWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Right non-splittable Window",
                                       ui::LogicalDisplayId::DEFAULT);
    rightWindow->setPreventSplitting(true);
    rightWindow->setFrame(Rect(100, 100, 200, 200));
    mDispatcher->onWindowInfosChanged(
            {{*leftWindow->getInfo(), *rightWindow->getInfo()}, {}, 0, 0});

    // Touch down on left, splittable window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .build());
    leftWindow->consumeMotionEvent(WithMotionAction(ACTION_DOWN));

    mDispatcher->notifyMotion(
            MotionArgsBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(50).y(50))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(150).y(150))
                    .build());
    leftWindow->assertNoEvents();
    rightWindow->assertNoEvents();
}

TEST_F(InputDispatcherTest, TouchpadThreeFingerSwipeOnlySentToTrustedOverlays) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 400, 400));
    sp<FakeWindowHandle> trustedOverlay =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Trusted Overlay",
                                       ui::LogicalDisplayId::DEFAULT);
    trustedOverlay->setSpy(true);
    trustedOverlay->setTrustedOverlay(true);

    mDispatcher->onWindowInfosChanged({{*trustedOverlay->getInfo(), *window->getInfo()}, {}, 0, 0});

    // Start a three-finger touchpad swipe
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(100))
                                      .classification(MotionClassification::MULTI_FINGER_SWIPE)
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_DOWN, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(250).y(100))
                                      .classification(MotionClassification::MULTI_FINGER_SWIPE)
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_2_DOWN, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(250).y(100))
                                      .pointer(PointerBuilder(2, ToolType::FINGER).x(300).y(100))
                                      .classification(MotionClassification::MULTI_FINGER_SWIPE)
                                      .build());

    trustedOverlay->consumeMotionEvent(WithMotionAction(ACTION_DOWN));
    trustedOverlay->consumeMotionEvent(WithMotionAction(POINTER_1_DOWN));
    trustedOverlay->consumeMotionEvent(WithMotionAction(POINTER_2_DOWN));

    // Move the swipe a bit
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(105))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(250).y(105))
                                      .pointer(PointerBuilder(2, ToolType::FINGER).x(300).y(105))
                                      .classification(MotionClassification::MULTI_FINGER_SWIPE)
                                      .build());

    trustedOverlay->consumeMotionEvent(WithMotionAction(ACTION_MOVE));

    // End the swipe
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_2_UP, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(105))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(250).y(105))
                                      .pointer(PointerBuilder(2, ToolType::FINGER).x(300).y(105))
                                      .classification(MotionClassification::MULTI_FINGER_SWIPE)
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_UP, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(105))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(250).y(105))
                                      .classification(MotionClassification::MULTI_FINGER_SWIPE)
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_UP, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(105))
                                      .classification(MotionClassification::MULTI_FINGER_SWIPE)
                                      .build());

    trustedOverlay->consumeMotionEvent(WithMotionAction(POINTER_2_UP));
    trustedOverlay->consumeMotionEvent(WithMotionAction(POINTER_1_UP));
    trustedOverlay->consumeMotionEvent(WithMotionAction(ACTION_UP));

    window->assertNoEvents();
}

TEST_F(InputDispatcherTest, TouchpadThreeFingerSwipeNotSentToSingleWindow) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 400, 400));
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    // Start a three-finger touchpad swipe
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(100))
                                      .classification(MotionClassification::MULTI_FINGER_SWIPE)
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_DOWN, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(250).y(100))
                                      .classification(MotionClassification::MULTI_FINGER_SWIPE)
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_2_DOWN, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(250).y(100))
                                      .pointer(PointerBuilder(2, ToolType::FINGER).x(300).y(100))
                                      .classification(MotionClassification::MULTI_FINGER_SWIPE)
                                      .build());

    // Move the swipe a bit
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(105))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(250).y(105))
                                      .pointer(PointerBuilder(2, ToolType::FINGER).x(300).y(105))
                                      .classification(MotionClassification::MULTI_FINGER_SWIPE)
                                      .build());

    // End the swipe
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_2_UP, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(105))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(250).y(105))
                                      .pointer(PointerBuilder(2, ToolType::FINGER).x(300).y(105))
                                      .classification(MotionClassification::MULTI_FINGER_SWIPE)
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_UP, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(105))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(250).y(105))
                                      .classification(MotionClassification::MULTI_FINGER_SWIPE)
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_UP, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(105))
                                      .classification(MotionClassification::MULTI_FINGER_SWIPE)
                                      .build());

    window->assertNoEvents();
}

/**
 * Send a two-pointer gesture to a single window. The window's orientation changes in response to
 * the first pointer.
 * Ensure that the second pointer and the subsequent gesture is correctly delivered to the window.
 */
TEST_F(InputDispatcherTest, MultiplePointersWithRotatingWindow) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 400, 400));
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    const nsecs_t baseTime = systemTime(SYSTEM_TIME_MONOTONIC);
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .downTime(baseTime + 10)
                                      .eventTime(baseTime + 10)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                      .build());

    window->consumeMotionEvent(WithMotionAction(ACTION_DOWN));

    // Change the transform so that the orientation is now different from original.
    window->setWindowTransform(0, -1, 1, 0);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .downTime(baseTime + 10)
                                      .eventTime(baseTime + 30)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(200).y(200))
                                      .build());

    window->consumeMotionEvent(WithMotionAction(POINTER_1_DOWN));

    // Finish the gesture and start a new one. Ensure all events are sent to the window.
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .downTime(baseTime + 10)
                                      .eventTime(baseTime + 40)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(200).y(200))
                                      .build());

    window->consumeMotionEvent(WithMotionAction(POINTER_1_UP));

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .downTime(baseTime + 10)
                                      .eventTime(baseTime + 50)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                      .build());

    window->consumeMotionEvent(WithMotionAction(ACTION_UP));

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .downTime(baseTime + 60)
                                      .eventTime(baseTime + 60)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(40).y(40))
                                      .build());

    window->consumeMotionEvent(WithMotionAction(ACTION_DOWN));
}

/**
 * When there are multiple screens, such as screen projection to TV or screen recording, if the
 * cancel event occurs, the coordinates of the cancel event should be sent to the target screen, and
 * its coordinates should be converted by the transform of the windows of target screen.
 */
TEST_F(InputDispatcherTest, WhenMultiDisplayWindowSameToken_DispatchCancelToTargetDisplay) {
    // This case will create a window and a spy window on the default display and mirror
    //  window on the second display. cancel event is sent through spy  window pilferPointers
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> spyWindowDefaultDisplay =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Spy",
                                       ui::LogicalDisplayId::DEFAULT);
    spyWindowDefaultDisplay->setTrustedOverlay(true);
    spyWindowDefaultDisplay->setSpy(true);

    sp<FakeWindowHandle> windowDefaultDisplay =
            sp<FakeWindowHandle>::make(application, mDispatcher, "DefaultDisplay",
                                       ui::LogicalDisplayId::DEFAULT);
    windowDefaultDisplay->setWindowTransform(1, 0, 0, 1);

    sp<FakeWindowHandle> windowSecondDisplay = windowDefaultDisplay->clone(SECOND_DISPLAY_ID);
    windowSecondDisplay->setWindowTransform(2, 0, 0, 2);

    // Add the windows to the dispatcher
    mDispatcher->onWindowInfosChanged(
            {{*spyWindowDefaultDisplay->getInfo(), *windowDefaultDisplay->getInfo(),
              *windowSecondDisplay->getInfo()},
             {},
             0,
             0});

    // Send down to ui::LogicalDisplayId::DEFAULT
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {100, 100}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    spyWindowDefaultDisplay->consumeMotionDown();
    windowDefaultDisplay->consumeMotionDown();

    EXPECT_EQ(OK, mDispatcher->pilferPointers(spyWindowDefaultDisplay->getToken()));

    // windowDefaultDisplay gets cancel
    std::unique_ptr<MotionEvent> event = windowDefaultDisplay->consumeMotionEvent();
    ASSERT_NE(nullptr, event);
    EXPECT_EQ(AMOTION_EVENT_ACTION_CANCEL, event->getAction());

    // The cancel event is sent to windowDefaultDisplay of the ui::LogicalDisplayId::DEFAULT
    // display, so the coordinates of the cancel are converted by windowDefaultDisplay's transform,
    // the x and y coordinates are both 100, otherwise if the cancel event is sent to
    // windowSecondDisplay of SECOND_DISPLAY_ID, the x and y coordinates are 200
    EXPECT_EQ(100, event->getX(0));
    EXPECT_EQ(100, event->getY(0));
}

/**
 * Ensure the correct coordinate spaces are used by InputDispatcher.
 *
 * InputDispatcher works in the display space, so its coordinate system is relative to the display
 * panel. Windows get events in the window space, and get raw coordinates in the logical display
 * space.
 */
class InputDispatcherDisplayProjectionTest : public InputDispatcherTest {
public:
    void SetUp() override {
        InputDispatcherTest::SetUp();
        removeAllWindowsAndDisplays();
    }

    void addDisplayInfo(ui::LogicalDisplayId displayId, const ui::Transform& transform) {
        gui::DisplayInfo info;
        info.displayId = displayId;
        info.transform = transform;
        mDisplayInfos.push_back(std::move(info));
        mDispatcher->onWindowInfosChanged({mWindowInfos, mDisplayInfos, 0, 0});
    }

    void addWindow(const sp<WindowInfoHandle>& windowHandle) {
        mWindowInfos.push_back(*windowHandle->getInfo());
        mDispatcher->onWindowInfosChanged({mWindowInfos, mDisplayInfos, 0, 0});
    }

    void removeAllWindowsAndDisplays() {
        mDisplayInfos.clear();
        mWindowInfos.clear();
    }

    // Set up a test scenario where the display has a scaled projection and there are two windows
    // on the display.
    std::pair<sp<FakeWindowHandle>, sp<FakeWindowHandle>> setupScaledDisplayScenario() {
        // The display has a projection that has a scale factor of 2 and 4 in the x and y directions
        // respectively.
        ui::Transform displayTransform;
        displayTransform.set(2, 0, 0, 4);
        addDisplayInfo(ui::LogicalDisplayId::DEFAULT, displayTransform);

        std::shared_ptr<FakeApplicationHandle> application =
                std::make_shared<FakeApplicationHandle>();

        // Add two windows to the display. Their frames are represented in the display space.
        sp<FakeWindowHandle> firstWindow =
                sp<FakeWindowHandle>::make(application, mDispatcher, "First Window",
                                           ui::LogicalDisplayId::DEFAULT);
        firstWindow->setFrame(Rect(0, 0, 100, 200), displayTransform);
        addWindow(firstWindow);

        sp<FakeWindowHandle> secondWindow =
                sp<FakeWindowHandle>::make(application, mDispatcher, "Second Window",
                                           ui::LogicalDisplayId::DEFAULT);
        secondWindow->setFrame(Rect(100, 200, 200, 400), displayTransform);
        addWindow(secondWindow);
        return {std::move(firstWindow), std::move(secondWindow)};
    }

private:
    std::vector<gui::DisplayInfo> mDisplayInfos;
    std::vector<gui::WindowInfo> mWindowInfos;
};

TEST_F(InputDispatcherDisplayProjectionTest, HitTestCoordinateSpaceConsistency) {
    auto [firstWindow, secondWindow] = setupScaledDisplayScenario();
    // Send down to the first window. The point is represented in the display space. The point is
    // selected so that if the hit test was performed with the point and the bounds being in
    // different coordinate spaces, the event would end up in the incorrect window.
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT, {PointF{75, 55}}));

    firstWindow->consumeMotionDown();
    secondWindow->assertNoEvents();
}

// Ensure that when a MotionEvent is injected through the InputDispatcher::injectInputEvent() API,
// the event should be treated as being in the logical display space.
TEST_F(InputDispatcherDisplayProjectionTest, InjectionInLogicalDisplaySpace) {
    auto [firstWindow, secondWindow] = setupScaledDisplayScenario();
    // Send down to the first window. The point is represented in the logical display space. The
    // point is selected so that if the hit test was done in logical display space, then it would
    // end up in the incorrect window.
    injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, ui::LogicalDisplayId::DEFAULT,
                     PointF{75 * 2, 55 * 4});

    firstWindow->consumeMotionDown();
    secondWindow->assertNoEvents();
}

// Ensure that when a MotionEvent that has a custom transform is injected, the post-transformed
// event should be treated as being in the logical display space.
TEST_F(InputDispatcherDisplayProjectionTest, InjectionWithTransformInLogicalDisplaySpace) {
    auto [firstWindow, secondWindow] = setupScaledDisplayScenario();

    const std::array<float, 9> matrix = {1.1, 2.2, 3.3, 4.4, 5.5, 6.6, 0.0, 0.0, 1.0};
    ui::Transform injectedEventTransform;
    injectedEventTransform.set(matrix);
    const vec2 expectedPoint{75, 55}; // The injected point in the logical display space.
    const vec2 untransformedPoint = injectedEventTransform.inverse().transform(expectedPoint);

    MotionEvent event = MotionEventBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                .displayId(ui::LogicalDisplayId::DEFAULT)
                                .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                                .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER)
                                                 .x(untransformedPoint.x)
                                                 .y(untransformedPoint.y))
                                .build();
    event.transform(matrix);

    injectMotionEvent(*mDispatcher, event, INJECT_EVENT_TIMEOUT,
                      InputEventInjectionSync::WAIT_FOR_RESULT);

    firstWindow->consumeMotionDown();
    secondWindow->assertNoEvents();
}

TEST_F(InputDispatcherDisplayProjectionTest, WindowGetsEventsInCorrectCoordinateSpace) {
    auto [firstWindow, secondWindow] = setupScaledDisplayScenario();

    // Send down to the second window.
    mDispatcher->notifyMotion(
            generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {PointF{150, 220}}));

    firstWindow->assertNoEvents();
    std::unique_ptr<MotionEvent> event = secondWindow->consumeMotionEvent();
    ASSERT_NE(nullptr, event);
    EXPECT_EQ(AMOTION_EVENT_ACTION_DOWN, event->getAction());

    // Ensure that the events from the "getRaw" API are in logical display coordinates.
    EXPECT_EQ(300, event->getRawX(0));
    EXPECT_EQ(880, event->getRawY(0));

    // Ensure that the x and y values are in the window's coordinate space.
    // The left-top of the second window is at (100, 200) in display space, which is (200, 800) in
    // the logical display space. This will be the origin of the window space.
    EXPECT_EQ(100, event->getX(0));
    EXPECT_EQ(80, event->getY(0));
}

TEST_F(InputDispatcherDisplayProjectionTest, CancelMotionWithCorrectCoordinates) {
    auto [firstWindow, secondWindow] = setupScaledDisplayScenario();
    // The monitor will always receive events in the logical display's coordinate space, because
    // it does not have a window.
    FakeMonitorReceiver monitor{*mDispatcher, "Monitor", ui::LogicalDisplayId::DEFAULT};

    // Send down to the first window.
    mDispatcher->notifyMotion(generateMotionArgs(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT, {PointF{50, 100}}));
    firstWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithCoords(100, 400)));
    monitor.consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithCoords(100, 400)));

    // Second pointer goes down on second window.
    mDispatcher->notifyMotion(generateMotionArgs(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT,
                                                 {PointF{50, 100}, PointF{150, 220}}));
    secondWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithCoords(100, 80)));
    const std::map<int32_t, PointF> expectedMonitorPointers{{0, PointF{100, 400}},
                                                            {1, PointF{300, 880}}};
    monitor.consumeMotionEvent(
            AllOf(WithMotionAction(POINTER_1_DOWN), WithPointers(expectedMonitorPointers)));

    mDispatcher->cancelCurrentTouch();

    firstWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_CANCEL), WithCoords(100, 400)));
    secondWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_CANCEL), WithCoords(100, 80)));
    monitor.consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithPointers(expectedMonitorPointers)));
}

TEST_F(InputDispatcherDisplayProjectionTest, SynthesizeDownWithCorrectCoordinates) {
    auto [firstWindow, secondWindow] = setupScaledDisplayScenario();

    // Send down to the first window.
    mDispatcher->notifyMotion(generateMotionArgs(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT, {PointF{50, 100}}));
    firstWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithCoords(100, 400)));

    // The pointer is transferred to the second window, and the second window receives it in the
    // correct coordinate space.
    mDispatcher->transferTouchGesture(firstWindow->getToken(), secondWindow->getToken());
    firstWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_CANCEL), WithCoords(100, 400)));
    secondWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithCoords(-100, -400)));
}

TEST_F(InputDispatcherDisplayProjectionTest, SynthesizeHoverEnterExitWithCorrectCoordinates) {
    auto [firstWindow, secondWindow] = setupScaledDisplayScenario();

    // Send hover move to the second window, and ensure it shows up as hover enter.
    mDispatcher->notifyMotion(generateMotionArgs(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS,
                                                 ui::LogicalDisplayId::DEFAULT,
                                                 {PointF{150, 220}}));
    secondWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_ENTER),
                                           WithCoords(100, 80), WithRawCoords(300, 880)));

    // Touch down at the same location and ensure a hover exit is synthesized.
    mDispatcher->notifyMotion(generateMotionArgs(ACTION_DOWN, AINPUT_SOURCE_STYLUS,
                                                 ui::LogicalDisplayId::DEFAULT,
                                                 {PointF{150, 220}}));
    secondWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_EXIT), WithCoords(100, 80),
                                           WithRawCoords(300, 880)));
    secondWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithCoords(100, 80), WithRawCoords(300, 880)));
    secondWindow->assertNoEvents();
    firstWindow->assertNoEvents();
}

// Same as above, but while the window is being mirrored.
TEST_F(InputDispatcherDisplayProjectionTest,
       SynthesizeHoverEnterExitWithCorrectCoordinatesWhenMirrored) {
    auto [firstWindow, secondWindow] = setupScaledDisplayScenario();

    const std::array<float, 9> matrix = {1.1, 2.2, 3.3, 4.4, 5.5, 6.6, 0.0, 0.0, 1.0};
    ui::Transform secondDisplayTransform;
    secondDisplayTransform.set(matrix);
    addDisplayInfo(SECOND_DISPLAY_ID, secondDisplayTransform);

    sp<FakeWindowHandle> secondWindowClone = secondWindow->clone(SECOND_DISPLAY_ID);
    secondWindowClone->setWindowTransform(1.1, 2.2, 3.3, 4.4);
    addWindow(secondWindowClone);

    // Send hover move to the second window, and ensure it shows up as hover enter.
    mDispatcher->notifyMotion(generateMotionArgs(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS,
                                                 ui::LogicalDisplayId::DEFAULT,
                                                 {PointF{150, 220}}));
    secondWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_ENTER),
                                           WithCoords(100, 80), WithRawCoords(300, 880)));

    // Touch down at the same location and ensure a hover exit is synthesized for the correct
    // display.
    mDispatcher->notifyMotion(generateMotionArgs(ACTION_DOWN, AINPUT_SOURCE_STYLUS,
                                                 ui::LogicalDisplayId::DEFAULT,
                                                 {PointF{150, 220}}));
    secondWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_EXIT), WithCoords(100, 80),
                                           WithRawCoords(300, 880)));
    secondWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithCoords(100, 80), WithRawCoords(300, 880)));
    secondWindow->assertNoEvents();
    firstWindow->assertNoEvents();
}

TEST_F(InputDispatcherDisplayProjectionTest, SynthesizeHoverCancelationWithCorrectCoordinates) {
    auto [firstWindow, secondWindow] = setupScaledDisplayScenario();

    // Send hover enter to second window
    mDispatcher->notifyMotion(generateMotionArgs(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS,
                                                 ui::LogicalDisplayId::DEFAULT,
                                                 {PointF{150, 220}}));
    secondWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_ENTER),
                                           WithCoords(100, 80), WithRawCoords(300, 880)));

    mDispatcher->cancelCurrentTouch();

    secondWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_EXIT), WithCoords(100, 80),
                                           WithRawCoords(300, 880)));
    secondWindow->assertNoEvents();
    firstWindow->assertNoEvents();
}

// Same as above, but while the window is being mirrored.
TEST_F(InputDispatcherDisplayProjectionTest,
       SynthesizeHoverCancelationWithCorrectCoordinatesWhenMirrored) {
    auto [firstWindow, secondWindow] = setupScaledDisplayScenario();

    const std::array<float, 9> matrix = {1.1, 2.2, 3.3, 4.4, 5.5, 6.6, 0.0, 0.0, 1.0};
    ui::Transform secondDisplayTransform;
    secondDisplayTransform.set(matrix);
    addDisplayInfo(SECOND_DISPLAY_ID, secondDisplayTransform);

    sp<FakeWindowHandle> secondWindowClone = secondWindow->clone(SECOND_DISPLAY_ID);
    secondWindowClone->setWindowTransform(1.1, 2.2, 3.3, 4.4);
    addWindow(secondWindowClone);

    // Send hover enter to second window
    mDispatcher->notifyMotion(generateMotionArgs(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS,
                                                 ui::LogicalDisplayId::DEFAULT,
                                                 {PointF{150, 220}}));
    secondWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_ENTER),
                                           WithCoords(100, 80), WithRawCoords(300, 880),
                                           WithDisplayId(ui::LogicalDisplayId::DEFAULT)));

    mDispatcher->cancelCurrentTouch();

    // Ensure the cancelation happens with the correct displayId and the correct coordinates.
    secondWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_EXIT), WithCoords(100, 80),
                                           WithRawCoords(300, 880),
                                           WithDisplayId(ui::LogicalDisplayId::DEFAULT)));
    secondWindow->assertNoEvents();
    firstWindow->assertNoEvents();
}

/** Ensure consistent behavior of InputDispatcher in all orientations. */
class InputDispatcherDisplayOrientationFixture
      : public InputDispatcherDisplayProjectionTest,
        public ::testing::WithParamInterface<ui::Rotation> {};

// This test verifies the touchable region of a window for all rotations of the display by tapping
// in different locations on the display, specifically points close to the four corners of a
// window.
TEST_P(InputDispatcherDisplayOrientationFixture, HitTestInDifferentOrientations) {
    constexpr static int32_t displayWidth = 400;
    constexpr static int32_t displayHeight = 800;

    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    const auto rotation = GetParam();

    // Set up the display with the specified rotation.
    const bool isRotated = rotation == ui::ROTATION_90 || rotation == ui::ROTATION_270;
    const int32_t logicalDisplayWidth = isRotated ? displayHeight : displayWidth;
    const int32_t logicalDisplayHeight = isRotated ? displayWidth : displayHeight;
    const ui::Transform displayTransform(ui::Transform::toRotationFlags(rotation),
                                         logicalDisplayWidth, logicalDisplayHeight);
    addDisplayInfo(ui::LogicalDisplayId::DEFAULT, displayTransform);

    // Create a window with its bounds determined in the logical display.
    const Rect frameInLogicalDisplay(100, 100, 200, 300);
    const Rect frameInDisplay = displayTransform.inverse().transform(frameInLogicalDisplay);
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(frameInDisplay, displayTransform);
    addWindow(window);

    // The following points in logical display space should be inside the window.
    static const std::array<vec2, 4> insidePoints{
            {{100, 100}, {199.99, 100}, {100, 299.99}, {199.99, 299.99}}};
    for (const auto pointInsideWindow : insidePoints) {
        const vec2 p = displayTransform.inverse().transform(pointInsideWindow);
        const PointF pointInDisplaySpace{p.x, p.y};
        mDispatcher->notifyMotion(
                generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                                   ui::LogicalDisplayId::DEFAULT, {pointInDisplaySpace}));
        window->consumeMotionDown();

        mDispatcher->notifyMotion(
                generateMotionArgs(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                   ui::LogicalDisplayId::DEFAULT, {pointInDisplaySpace}));
        window->consumeMotionUp();
    }

    // The following points in logical display space should be outside the window.
    static const std::array<vec2, 5> outsidePoints{
            {{200, 100}, {100, 300}, {200, 300}, {100, 99.99}, {99.99, 100}}};
    for (const auto pointOutsideWindow : outsidePoints) {
        const vec2 p = displayTransform.inverse().transform(pointOutsideWindow);
        const PointF pointInDisplaySpace{p.x, p.y};
        mDispatcher->notifyMotion(
                generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                                   ui::LogicalDisplayId::DEFAULT, {pointInDisplaySpace}));

        mDispatcher->notifyMotion(
                generateMotionArgs(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                   ui::LogicalDisplayId::DEFAULT, {pointInDisplaySpace}));
    }
    window->assertNoEvents();
}

// This test verifies the occlusion detection for all rotations of the display by tapping
// in different locations on the display, specifically points close to the four corners of a
// window.
TEST_P(InputDispatcherDisplayOrientationFixture, BlockUntrustClickInDifferentOrientations) {
    constexpr static int32_t displayWidth = 400;
    constexpr static int32_t displayHeight = 800;

    std::shared_ptr<FakeApplicationHandle> untrustedWindowApplication =
            std::make_shared<FakeApplicationHandle>();
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    const auto rotation = GetParam();

    // Set up the display with the specified rotation.
    const bool isRotated = rotation == ui::ROTATION_90 || rotation == ui::ROTATION_270;
    const int32_t logicalDisplayWidth = isRotated ? displayHeight : displayWidth;
    const int32_t logicalDisplayHeight = isRotated ? displayWidth : displayHeight;
    const ui::Transform displayTransform(ui::Transform::toRotationFlags(rotation),
                                         logicalDisplayWidth, logicalDisplayHeight);
    addDisplayInfo(ui::LogicalDisplayId::DEFAULT, displayTransform);

    // Create a window that not trusted.
    const Rect untrustedWindowFrameInLogicalDisplay(100, 100, 200, 300);

    const Rect untrustedWindowFrameInDisplay =
            displayTransform.inverse().transform(untrustedWindowFrameInLogicalDisplay);

    sp<FakeWindowHandle> untrustedWindow =
            sp<FakeWindowHandle>::make(untrustedWindowApplication, mDispatcher, "UntrustedWindow",
                                       ui::LogicalDisplayId::DEFAULT);
    untrustedWindow->setFrame(untrustedWindowFrameInDisplay, displayTransform);
    untrustedWindow->setTrustedOverlay(false);
    untrustedWindow->setTouchOcclusionMode(TouchOcclusionMode::BLOCK_UNTRUSTED);
    untrustedWindow->setTouchable(false);
    untrustedWindow->setAlpha(1.0f);
    untrustedWindow->setOwnerInfo(gui::Pid{1}, gui::Uid{101});
    addWindow(untrustedWindow);

    // Create a simple app window below the untrusted window.
    const Rect simpleAppWindowFrameInLogicalDisplay(0, 0, 300, 600);
    const Rect simpleAppWindowFrameInDisplay =
            displayTransform.inverse().transform(simpleAppWindowFrameInLogicalDisplay);

    sp<FakeWindowHandle> simpleAppWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "SimpleAppWindow",
                                       ui::LogicalDisplayId::DEFAULT);
    simpleAppWindow->setFrame(simpleAppWindowFrameInDisplay, displayTransform);
    simpleAppWindow->setOwnerInfo(gui::Pid{2}, gui::Uid{202});
    addWindow(simpleAppWindow);

    // The following points in logical display space should be inside the untrusted window, so
    // the simple window could not receive events that coordinate is these point.
    static const std::array<vec2, 4> untrustedPoints{
            {{100, 100}, {199.99, 100}, {100, 299.99}, {199.99, 299.99}}};

    for (const auto untrustedPoint : untrustedPoints) {
        const vec2 p = displayTransform.inverse().transform(untrustedPoint);
        const PointF pointInDisplaySpace{p.x, p.y};
        mDispatcher->notifyMotion(
                generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                                   ui::LogicalDisplayId::DEFAULT, {pointInDisplaySpace}));
        mDispatcher->notifyMotion(
                generateMotionArgs(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                   ui::LogicalDisplayId::DEFAULT, {pointInDisplaySpace}));
    }
    untrustedWindow->assertNoEvents();
    simpleAppWindow->assertNoEvents();
    // The following points in logical display space should be outside the untrusted window, so
    // the simple window should receive events that coordinate is these point.
    static const std::array<vec2, 5> trustedPoints{
            {{200, 100}, {100, 300}, {200, 300}, {100, 99.99}, {99.99, 100}}};
    for (const auto trustedPoint : trustedPoints) {
        const vec2 p = displayTransform.inverse().transform(trustedPoint);
        const PointF pointInDisplaySpace{p.x, p.y};
        mDispatcher->notifyMotion(
                generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                                   ui::LogicalDisplayId::DEFAULT, {pointInDisplaySpace}));
        simpleAppWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT,
                                           AMOTION_EVENT_FLAG_WINDOW_IS_PARTIALLY_OBSCURED);
        mDispatcher->notifyMotion(
                generateMotionArgs(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                   ui::LogicalDisplayId::DEFAULT, {pointInDisplaySpace}));
        simpleAppWindow->consumeMotionUp(ui::LogicalDisplayId::DEFAULT,
                                         AMOTION_EVENT_FLAG_WINDOW_IS_PARTIALLY_OBSCURED);
    }
    untrustedWindow->assertNoEvents();
}

// Run the precision tests for all rotations.
INSTANTIATE_TEST_SUITE_P(InputDispatcherDisplayOrientationTests,
                         InputDispatcherDisplayOrientationFixture,
                         ::testing::Values(ui::ROTATION_0, ui::ROTATION_90, ui::ROTATION_180,
                                           ui::ROTATION_270),
                         [](const testing::TestParamInfo<ui::Rotation>& testParamInfo) {
                             return ftl::enum_string(testParamInfo.param);
                         });

using TransferFunction = std::function<bool(const std::unique_ptr<InputDispatcher>& dispatcher,
                                            sp<IBinder>, sp<IBinder>)>;

class TransferTouchFixture : public InputDispatcherTest,
                             public ::testing::WithParamInterface<TransferFunction> {};

TEST_P(TransferTouchFixture, TransferTouch_OnePointer) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    // Create a couple of windows
    sp<FakeWindowHandle> firstWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "First Window",
                                       ui::LogicalDisplayId::DEFAULT);
    firstWindow->setDupTouchToWallpaper(true);
    sp<FakeWindowHandle> secondWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Second Window",
                                       ui::LogicalDisplayId::DEFAULT);
    sp<FakeWindowHandle> wallpaper =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Wallpaper",
                                       ui::LogicalDisplayId::DEFAULT);
    wallpaper->setIsWallpaper(true);
    // Add the windows to the dispatcher, and ensure the first window is focused
    mDispatcher->onWindowInfosChanged(
            {{*firstWindow->getInfo(), *secondWindow->getInfo(), *wallpaper->getInfo()}, {}, 0, 0});
    setFocusedWindow(firstWindow);
    firstWindow->consumeFocusEvent(true);

    // Send down to the first window
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));

    // Only the first window should get the down event
    firstWindow->consumeMotionDown();
    secondWindow->assertNoEvents();
    wallpaper->consumeMotionDown(ui::LogicalDisplayId::DEFAULT, EXPECTED_WALLPAPER_FLAGS);
    // Dispatcher reports pointer down outside focus for the wallpaper
    mFakePolicy->assertOnPointerDownEquals(wallpaper->getToken());

    // Transfer touch to the second window
    TransferFunction f = GetParam();
    const bool success = f(mDispatcher, firstWindow->getToken(), secondWindow->getToken());
    ASSERT_TRUE(success);
    // The first window gets cancel and the second gets down
    firstWindow->consumeMotionCancel();
    secondWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT,
                                    AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    wallpaper->consumeMotionCancel(ui::LogicalDisplayId::DEFAULT, EXPECTED_WALLPAPER_FLAGS);
    // There should not be any changes to the focused window when transferring touch
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertOnPointerDownWasNotCalled());

    // Send up event to the second window
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));
    // The first window gets no events and the second gets up
    firstWindow->assertNoEvents();
    secondWindow->consumeMotionUp(ui::LogicalDisplayId::DEFAULT,
                                  AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    wallpaper->assertNoEvents();
}

/**
 * When 'transferTouchGesture' API is invoked, dispatcher needs to find the "best" window to take
 * touch from. When we have spy windows, there are several windows to choose from: either spy, or
 * the 'real' (non-spy) window. Always prefer the 'real' window because that's what would be most
 * natural to the user.
 * In this test, we are sending a pointer to both spy window and first window. We then try to
 * transfer touch to the second window. The dispatcher should identify the first window as the
 * one that should lose the gesture, and therefore the action should be to move the gesture from
 * the first window to the second.
 * The main goal here is to test the behaviour of 'transferTouchGesture' API, but it's still valid
 * to test the other API, as well.
 */
TEST_P(TransferTouchFixture, TransferTouch_MultipleWindowsWithSpy) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    // Create a couple of windows + a spy window
    sp<FakeWindowHandle> spyWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Spy",
                                                                ui::LogicalDisplayId::DEFAULT);
    spyWindow->setTrustedOverlay(true);
    spyWindow->setSpy(true);
    sp<FakeWindowHandle> firstWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "First",
                                                                  ui::LogicalDisplayId::DEFAULT);
    sp<FakeWindowHandle> secondWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Second",
                                       ui::LogicalDisplayId::DEFAULT);

    // Add the windows to the dispatcher
    mDispatcher->onWindowInfosChanged(
            {{*spyWindow->getInfo(), *firstWindow->getInfo(), *secondWindow->getInfo()}, {}, 0, 0});

    // Send down to the first window
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));
    // Only the first window and spy should get the down event
    spyWindow->consumeMotionDown();
    firstWindow->consumeMotionDown();

    // Transfer touch to the second window. Non-spy window should be preferred over the spy window
    // if f === 'transferTouchGesture'.
    TransferFunction f = GetParam();
    const bool success = f(mDispatcher, firstWindow->getToken(), secondWindow->getToken());
    ASSERT_TRUE(success);
    // The first window gets cancel and the second gets down
    firstWindow->consumeMotionCancel();
    secondWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT,
                                    AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);

    // Send up event to the second window
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));
    // The first  window gets no events and the second+spy get up
    firstWindow->assertNoEvents();
    spyWindow->consumeMotionUp();
    secondWindow->consumeMotionUp(ui::LogicalDisplayId::DEFAULT,
                                  AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
}

TEST_P(TransferTouchFixture, TransferTouch_TwoPointersNonSplitTouch) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    PointF touchPoint = {10, 10};

    // Create a couple of windows
    sp<FakeWindowHandle> firstWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "First Window",
                                       ui::LogicalDisplayId::DEFAULT);
    firstWindow->setPreventSplitting(true);
    sp<FakeWindowHandle> secondWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Second Window",
                                       ui::LogicalDisplayId::DEFAULT);
    secondWindow->setPreventSplitting(true);

    // Add the windows to the dispatcher
    mDispatcher->onWindowInfosChanged(
            {{*firstWindow->getInfo(), *secondWindow->getInfo()}, {}, 0, 0});

    // Send down to the first window
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT, {touchPoint}));
    // Only the first window should get the down event
    firstWindow->consumeMotionDown();
    secondWindow->assertNoEvents();

    // Send pointer down to the first window
    mDispatcher->notifyMotion(generateMotionArgs(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT,
                                                 {touchPoint, touchPoint}));
    // Only the first window should get the pointer down event
    firstWindow->consumeMotionPointerDown(1);
    secondWindow->assertNoEvents();

    // Transfer touch focus to the second window
    TransferFunction f = GetParam();
    bool success = f(mDispatcher, firstWindow->getToken(), secondWindow->getToken());
    ASSERT_TRUE(success);
    // The first window gets cancel and the second gets down and pointer down
    firstWindow->consumeMotionCancel();
    secondWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT,
                                    AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    secondWindow->consumeMotionPointerDown(1, ui::LogicalDisplayId::DEFAULT,
                                           AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);

    // Send pointer up to the second window
    mDispatcher->notifyMotion(generateMotionArgs(POINTER_1_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT,
                                                 {touchPoint, touchPoint}));
    // The first window gets nothing and the second gets pointer up
    firstWindow->assertNoEvents();
    secondWindow->consumeMotionPointerUp(/*pointerIdx=*/1,
                                         AllOf(WithDisplayId(ui::LogicalDisplayId::DEFAULT),
                                               WithFlags(AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE),
                                               WithPointerCount(2)));

    // Send up event to the second window
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));
    // The first window gets nothing and the second gets up
    firstWindow->assertNoEvents();
    secondWindow->consumeMotionUp(ui::LogicalDisplayId::DEFAULT,
                                  AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
}

TEST_P(TransferTouchFixture, TransferTouch_MultipleWallpapers) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    // Create a couple of windows
    sp<FakeWindowHandle> firstWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "First Window",
                                       ui::LogicalDisplayId::DEFAULT);
    firstWindow->setDupTouchToWallpaper(true);
    sp<FakeWindowHandle> secondWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Second Window",
                                       ui::LogicalDisplayId::DEFAULT);
    secondWindow->setDupTouchToWallpaper(true);

    sp<FakeWindowHandle> wallpaper1 =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Wallpaper1",
                                       ui::LogicalDisplayId::DEFAULT);
    wallpaper1->setIsWallpaper(true);

    sp<FakeWindowHandle> wallpaper2 =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Wallpaper2",
                                       ui::LogicalDisplayId::DEFAULT);
    wallpaper2->setIsWallpaper(true);
    // Add the windows to the dispatcher
    mDispatcher->onWindowInfosChanged({{*firstWindow->getInfo(), *wallpaper1->getInfo(),
                                        *secondWindow->getInfo(), *wallpaper2->getInfo()},
                                       {},
                                       0,
                                       0});

    // Send down to the first window
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));

    // Only the first window should get the down event
    firstWindow->consumeMotionDown();
    secondWindow->assertNoEvents();
    wallpaper1->consumeMotionDown(ui::LogicalDisplayId::DEFAULT, EXPECTED_WALLPAPER_FLAGS);
    wallpaper2->assertNoEvents();

    // Transfer touch focus to the second window
    TransferFunction f = GetParam();
    bool success = f(mDispatcher, firstWindow->getToken(), secondWindow->getToken());
    ASSERT_TRUE(success);

    // The first window gets cancel and the second gets down
    firstWindow->consumeMotionCancel();
    secondWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT,
                                    AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    wallpaper1->consumeMotionCancel(ui::LogicalDisplayId::DEFAULT, EXPECTED_WALLPAPER_FLAGS);
    wallpaper2->consumeMotionDown(ui::LogicalDisplayId::DEFAULT,
                                  EXPECTED_WALLPAPER_FLAGS | AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);

    // Send up event to the second window
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));
    // The first  window gets no events and the second gets up
    firstWindow->assertNoEvents();
    secondWindow->consumeMotionUp(ui::LogicalDisplayId::DEFAULT,
                                  AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    wallpaper1->assertNoEvents();
    wallpaper2->consumeMotionUp(ui::LogicalDisplayId::DEFAULT,
                                EXPECTED_WALLPAPER_FLAGS | AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
}

// For the cases of single pointer touch and two pointers non-split touch, the api's
// 'transferTouchGesture' and 'transferTouchOnDisplay' are equivalent in behaviour. They only differ
// for the case where there are multiple pointers split across several windows.
INSTANTIATE_TEST_SUITE_P(
        InputDispatcherTransferFunctionTests, TransferTouchFixture,
        ::testing::Values(
                [&](const std::unique_ptr<InputDispatcher>& dispatcher, sp<IBinder> /*ignored*/,
                    sp<IBinder> destChannelToken) {
                    return dispatcher->transferTouchOnDisplay(destChannelToken,
                                                              ui::LogicalDisplayId::DEFAULT);
                },
                [&](const std::unique_ptr<InputDispatcher>& dispatcher, sp<IBinder> from,
                    sp<IBinder> to) {
                    return dispatcher->transferTouchGesture(from, to,
                                                            /*isDragAndDrop=*/false);
                }));

TEST_F(InputDispatcherTest, TransferTouch_TwoPointersSplitTouch) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> firstWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "First Window",
                                       ui::LogicalDisplayId::DEFAULT);
    firstWindow->setFrame(Rect(0, 0, 600, 400));

    sp<FakeWindowHandle> secondWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Second Window",
                                       ui::LogicalDisplayId::DEFAULT);
    secondWindow->setFrame(Rect(0, 400, 600, 800));

    // Add the windows to the dispatcher
    mDispatcher->onWindowInfosChanged(
            {{*firstWindow->getInfo(), *secondWindow->getInfo()}, {}, 0, 0});

    PointF pointInFirst = {300, 200};
    PointF pointInSecond = {300, 600};

    // Send down to the first window
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT, {pointInFirst}));
    // Only the first window should get the down event
    firstWindow->consumeMotionDown();
    secondWindow->assertNoEvents();

    // Send down to the second window
    mDispatcher->notifyMotion(generateMotionArgs(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT,
                                                 {pointInFirst, pointInSecond}));
    // The first window gets a move and the second a down
    firstWindow->consumeMotionMove();
    secondWindow->consumeMotionDown();

    // Transfer touch to the second window
    mDispatcher->transferTouchGesture(firstWindow->getToken(), secondWindow->getToken());
    // The first window gets cancel and the new gets pointer down (it already saw down)
    firstWindow->consumeMotionCancel();
    secondWindow->consumeMotionPointerDown(1, ui::LogicalDisplayId::DEFAULT,
                                           AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);

    // Send pointer up to the second window
    mDispatcher->notifyMotion(generateMotionArgs(POINTER_1_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT,
                                                 {pointInFirst, pointInSecond}));
    // The first window gets nothing and the second gets pointer up
    firstWindow->assertNoEvents();
    secondWindow->consumeMotionPointerUp(/*pointerIdx=*/1,
                                         AllOf(WithDisplayId(ui::LogicalDisplayId::DEFAULT),
                                               WithFlags(AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE),
                                               WithPointerCount(2)));

    // Send up event to the second window
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));
    // The first window gets nothing and the second gets up
    firstWindow->assertNoEvents();
    secondWindow->consumeMotionUp(ui::LogicalDisplayId::DEFAULT,
                                  AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
}

// Same as TransferTouch_TwoPointersSplitTouch, but using 'transferTouchOnDisplay' api.
// Unlike 'transferTouchGesture', calling 'transferTouchOnDisplay' when there are two windows
// receiving touch is not supported, so the touch should continue on those windows and the
// transferred-to window should get nothing.
TEST_F(InputDispatcherTest, TransferTouchOnDisplay_TwoPointersSplitTouch) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> firstWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "First Window",
                                       ui::LogicalDisplayId::DEFAULT);
    firstWindow->setFrame(Rect(0, 0, 600, 400));

    sp<FakeWindowHandle> secondWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Second Window",
                                       ui::LogicalDisplayId::DEFAULT);
    secondWindow->setFrame(Rect(0, 400, 600, 800));

    // Add the windows to the dispatcher
    mDispatcher->onWindowInfosChanged(
            {{*firstWindow->getInfo(), *secondWindow->getInfo()}, {}, 0, 0});

    PointF pointInFirst = {300, 200};
    PointF pointInSecond = {300, 600};

    // Send down to the first window
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT, {pointInFirst}));
    // Only the first window should get the down event
    firstWindow->consumeMotionDown();
    secondWindow->assertNoEvents();

    // Send down to the second window
    mDispatcher->notifyMotion(generateMotionArgs(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT,
                                                 {pointInFirst, pointInSecond}));
    // The first window gets a move and the second a down
    firstWindow->consumeMotionMove();
    secondWindow->consumeMotionDown();

    // Transfer touch focus to the second window
    const bool transferred = mDispatcher->transferTouchOnDisplay(secondWindow->getToken(),
                                                                 ui::LogicalDisplayId::DEFAULT);
    // The 'transferTouchOnDisplay' call should not succeed, because there are 2 touched windows
    ASSERT_FALSE(transferred);
    firstWindow->assertNoEvents();
    secondWindow->assertNoEvents();

    // The rest of the dispatch should proceed as normal
    // Send pointer up to the second window
    mDispatcher->notifyMotion(generateMotionArgs(POINTER_1_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT,
                                                 {pointInFirst, pointInSecond}));
    // The first window gets MOVE and the second gets pointer up
    firstWindow->consumeMotionMove();
    secondWindow->consumeMotionUp();

    // Send up event to the first window
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));
    // The first window gets nothing and the second gets up
    firstWindow->consumeMotionUp();
    secondWindow->assertNoEvents();
}

// This case will create two windows and one mirrored window on the default display and mirror
// two windows on the second display. It will test if 'transferTouchGesture' works fine if we put
// the windows info of second display before default display.
TEST_F(InputDispatcherTest, TransferTouch_CloneSurface) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> firstWindowInPrimary =
            sp<FakeWindowHandle>::make(application, mDispatcher, "D_1_W1",
                                       ui::LogicalDisplayId::DEFAULT);
    firstWindowInPrimary->setFrame(Rect(0, 0, 100, 100));
    sp<FakeWindowHandle> secondWindowInPrimary =
            sp<FakeWindowHandle>::make(application, mDispatcher, "D_1_W2",
                                       ui::LogicalDisplayId::DEFAULT);
    secondWindowInPrimary->setFrame(Rect(100, 0, 200, 100));

    sp<FakeWindowHandle> mirrorWindowInPrimary =
            firstWindowInPrimary->clone(ui::LogicalDisplayId::DEFAULT);
    mirrorWindowInPrimary->setFrame(Rect(0, 100, 100, 200));

    sp<FakeWindowHandle> firstWindowInSecondary = firstWindowInPrimary->clone(SECOND_DISPLAY_ID);
    firstWindowInSecondary->setFrame(Rect(0, 0, 100, 100));

    sp<FakeWindowHandle> secondWindowInSecondary = secondWindowInPrimary->clone(SECOND_DISPLAY_ID);
    secondWindowInPrimary->setFrame(Rect(100, 0, 200, 100));

    // Update window info, let it find window handle of second display first.
    mDispatcher->onWindowInfosChanged(
            {{*firstWindowInSecondary->getInfo(), *secondWindowInSecondary->getInfo(),
              *mirrorWindowInPrimary->getInfo(), *firstWindowInPrimary->getInfo(),
              *secondWindowInPrimary->getInfo()},
             {},
             0,
             0});

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {50, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    // Window should receive motion event.
    firstWindowInPrimary->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);

    // Transfer touch
    ASSERT_TRUE(mDispatcher->transferTouchGesture(firstWindowInPrimary->getToken(),
                                                  secondWindowInPrimary->getToken()));
    // The first window gets cancel.
    firstWindowInPrimary->consumeMotionCancel();
    secondWindowInPrimary->consumeMotionDown(ui::LogicalDisplayId::DEFAULT,
                                             AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {150, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    firstWindowInPrimary->assertNoEvents();
    secondWindowInPrimary->consumeMotionMove(ui::LogicalDisplayId::DEFAULT,
                                             AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionUp(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, ui::LogicalDisplayId::DEFAULT,
                             {150, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    firstWindowInPrimary->assertNoEvents();
    secondWindowInPrimary->consumeMotionUp(ui::LogicalDisplayId::DEFAULT,
                                           AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
}

// Same as TransferTouch_CloneSurface, but this touch on the secondary display and use
// 'transferTouchOnDisplay' api.
TEST_F(InputDispatcherTest, TransferTouchOnDisplay_CloneSurface) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> firstWindowInPrimary =
            sp<FakeWindowHandle>::make(application, mDispatcher, "D_1_W1",
                                       ui::LogicalDisplayId::DEFAULT);
    firstWindowInPrimary->setFrame(Rect(0, 0, 100, 100));
    sp<FakeWindowHandle> secondWindowInPrimary =
            sp<FakeWindowHandle>::make(application, mDispatcher, "D_1_W2",
                                       ui::LogicalDisplayId::DEFAULT);
    secondWindowInPrimary->setFrame(Rect(100, 0, 200, 100));

    sp<FakeWindowHandle> mirrorWindowInPrimary =
            firstWindowInPrimary->clone(ui::LogicalDisplayId::DEFAULT);
    mirrorWindowInPrimary->setFrame(Rect(0, 100, 100, 200));

    sp<FakeWindowHandle> firstWindowInSecondary = firstWindowInPrimary->clone(SECOND_DISPLAY_ID);
    firstWindowInSecondary->setFrame(Rect(0, 0, 100, 100));

    sp<FakeWindowHandle> secondWindowInSecondary = secondWindowInPrimary->clone(SECOND_DISPLAY_ID);
    secondWindowInPrimary->setFrame(Rect(100, 0, 200, 100));

    // Update window info, let it find window handle of second display first.
    mDispatcher->onWindowInfosChanged(
            {{*firstWindowInSecondary->getInfo(), *secondWindowInSecondary->getInfo(),
              *mirrorWindowInPrimary->getInfo(), *firstWindowInPrimary->getInfo(),
              *secondWindowInPrimary->getInfo()},
             {},
             0,
             0});

    // Touch on second display.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, SECOND_DISPLAY_ID,
                               {50, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    // Window should receive motion event.
    firstWindowInSecondary->consumeMotionDown(SECOND_DISPLAY_ID);

    // Transfer touch focus
    ASSERT_TRUE(mDispatcher->transferTouchOnDisplay(secondWindowInSecondary->getToken(),
                                                    SECOND_DISPLAY_ID));

    // The first window gets cancel.
    firstWindowInSecondary->consumeMotionCancel(SECOND_DISPLAY_ID);
    secondWindowInSecondary->consumeMotionDown(SECOND_DISPLAY_ID,
                                               AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                SECOND_DISPLAY_ID, {150, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    firstWindowInSecondary->assertNoEvents();
    secondWindowInSecondary->consumeMotionMove(SECOND_DISPLAY_ID,
                                               AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionUp(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, SECOND_DISPLAY_ID, {150, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    firstWindowInSecondary->assertNoEvents();
    secondWindowInSecondary->consumeMotionUp(SECOND_DISPLAY_ID, AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
}

TEST_F(InputDispatcherTest, FocusedWindow_ReceivesFocusEventAndKeyEvent) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);

    window->setFocusable(true);
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);

    window->consumeFocusEvent(true);

    mDispatcher->notifyKey(generateKeyArgs(AKEY_EVENT_ACTION_DOWN, ui::LogicalDisplayId::DEFAULT));

    // Window should receive key down event.
    window->consumeKeyDown(ui::LogicalDisplayId::DEFAULT);

    // Should have poked user activity
    mDispatcher->waitForIdle();
    mFakePolicy->assertUserActivityPoked();
}

TEST_F(InputDispatcherTest, FocusedWindow_DisableUserActivity) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);

    window->setDisableUserActivity(true);
    window->setFocusable(true);
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);

    window->consumeFocusEvent(true);

    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());

    // Window should receive key down event.
    window->consumeKeyDown(ui::LogicalDisplayId::DEFAULT);

    // Should have not poked user activity
    mDispatcher->waitForIdle();
    mFakePolicy->assertUserActivityNotPoked();
}

TEST_F(InputDispatcherTest, FocusedWindow_DoesNotReceivePolicyConsumedKey) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);

    window->setFocusable(true);
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);

    window->consumeFocusEvent(true);

    mFakePolicy->setConsumeKeyBeforeDispatching(true);

    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());
    mDispatcher->waitForIdle();

    // Key is not passed down
    window->assertNoEvents();

    // Should have poked user activity
    mFakePolicy->assertUserActivityPoked();
}

TEST_F(InputDispatcherTest, FocusedWindow_PolicyConsumedKeyIgnoresDisableUserActivity) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);

    window->setDisableUserActivity(true);
    window->setFocusable(true);
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);

    window->consumeFocusEvent(true);

    mFakePolicy->setConsumeKeyBeforeDispatching(true);

    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());
    mDispatcher->waitForIdle();

    // System key is not passed down
    window->assertNoEvents();

    // Should have poked user activity
    mFakePolicy->assertUserActivityPoked();
}

class DisableUserActivityInputDispatcherTest : public InputDispatcherTest,
                                               public ::testing::WithParamInterface<bool> {};

TEST_P(DisableUserActivityInputDispatcherTest, NotPassedToUserUserActivity) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);

    window->setDisableUserActivity(GetParam());

    window->setFocusable(true);
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);

    window->consumeFocusEvent(true);

    mDispatcher->notifyKey(KeyArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_KEYBOARD)
                                   .keyCode(AKEYCODE_A)
                                   .policyFlags(0)
                                   .build());
    mDispatcher->waitForIdle();

    // Key is not passed down
    window->assertNoEvents();

    // Should not have poked user activity
    mFakePolicy->assertUserActivityNotPoked();
}

INSTANTIATE_TEST_CASE_P(DisableUserActivity, DisableUserActivityInputDispatcherTest,
                        ::testing::Bool());

TEST_F(InputDispatcherTest, InjectedTouchesPokeUserActivity) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {100, 100}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    window->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDisplayId(ui::LogicalDisplayId::DEFAULT)));

    // Should have poked user activity
    mDispatcher->waitForIdle();
    mFakePolicy->assertUserActivityPoked();
}

TEST_F(InputDispatcherTest, UnfocusedWindow_DoesNotReceiveFocusEventOrKeyEvent) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    mDispatcher->notifyKey(generateKeyArgs(AKEY_EVENT_ACTION_DOWN, ui::LogicalDisplayId::DEFAULT));
    mDispatcher->waitForIdle();

    window->assertNoEvents();
}

// If a window is touchable, but does not have focus, it should receive motion events, but not keys
TEST_F(InputDispatcherTest, UnfocusedWindow_ReceivesMotionsButNotKeys) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    // Send key
    mDispatcher->notifyKey(generateKeyArgs(AKEY_EVENT_ACTION_DOWN, ui::LogicalDisplayId::DEFAULT));
    // Send motion
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));

    // Window should receive only the motion event
    window->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    window->assertNoEvents(); // Key event or focus event will not be received
}

TEST_F(InputDispatcherTest, PointerCancel_SendCancelWhenSplitTouch) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> firstWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "First Window",
                                       ui::LogicalDisplayId::DEFAULT);
    firstWindow->setFrame(Rect(0, 0, 600, 400));

    sp<FakeWindowHandle> secondWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Second Window",
                                       ui::LogicalDisplayId::DEFAULT);
    secondWindow->setFrame(Rect(0, 400, 600, 800));

    // Add the windows to the dispatcher
    mDispatcher->onWindowInfosChanged(
            {{*firstWindow->getInfo(), *secondWindow->getInfo()}, {}, 0, 0});

    PointF pointInFirst = {300, 200};
    PointF pointInSecond = {300, 600};

    // Send down to the first window
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT, {pointInFirst}));
    // Only the first window should get the down event
    firstWindow->consumeMotionDown();
    secondWindow->assertNoEvents();

    // Send down to the second window
    mDispatcher->notifyMotion(generateMotionArgs(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT,
                                                 {pointInFirst, pointInSecond}));
    // The first window gets a move and the second a down
    firstWindow->consumeMotionMove();
    secondWindow->consumeMotionDown();

    // Send pointer cancel to the second window
    NotifyMotionArgs pointerUpMotionArgs =
            generateMotionArgs(POINTER_1_UP, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {pointInFirst, pointInSecond});
    pointerUpMotionArgs.flags |= AMOTION_EVENT_FLAG_CANCELED;
    mDispatcher->notifyMotion(pointerUpMotionArgs);
    // The first window gets move and the second gets cancel.
    firstWindow->consumeMotionMove(ui::LogicalDisplayId::DEFAULT, AMOTION_EVENT_FLAG_CANCELED);
    secondWindow->consumeMotionCancel(ui::LogicalDisplayId::DEFAULT, AMOTION_EVENT_FLAG_CANCELED);

    // Send up event.
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));
    // The first window gets up and the second gets nothing.
    firstWindow->consumeMotionUp();
    secondWindow->assertNoEvents();
}

TEST_F(InputDispatcherTest, SendTimeline_DoesNotCrashDispatcher) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    std::array<nsecs_t, GraphicsTimeline::SIZE> graphicsTimeline;
    graphicsTimeline[GraphicsTimeline::GPU_COMPLETED_TIME] = 2;
    graphicsTimeline[GraphicsTimeline::PRESENT_TIME] = 3;

    window->sendTimeline(/*inputEventId=*/1, graphicsTimeline);
    window->assertNoEvents();
    mDispatcher->waitForIdle();
}

using InputDispatcherMonitorTest = InputDispatcherTest;

/**
 * Two entities that receive touch: A window, and a global monitor.
 * The touch goes to the window, and then the window disappears.
 * The monitor does not get cancel right away. But if more events come in, the touch gets canceled
 * for the monitor, as well.
 * 1. foregroundWindow
 * 2. monitor <-- global monitor (doesn't observe z order, receives all events)
 */
TEST_F(InputDispatcherMonitorTest, MonitorTouchIsCanceledWhenForegroundWindowDisappears) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Foreground",
                                                             ui::LogicalDisplayId::DEFAULT);

    FakeMonitorReceiver monitor =
            FakeMonitorReceiver(*mDispatcher, "M_1", ui::LogicalDisplayId::DEFAULT);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {100, 200}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    // Both the foreground window and the global monitor should receive the touch down
    window->consumeMotionDown();
    monitor.consumeMotionDown(ui::LogicalDisplayId::DEFAULT);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {110, 200}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    window->consumeMotionMove();
    monitor.consumeMotionMove(ui::LogicalDisplayId::DEFAULT);

    // Now the foreground window goes away
    mDispatcher->onWindowInfosChanged({{}, {}, 0, 0});
    window->consumeMotionCancel();
    monitor.assertNoEvents(); // Global monitor does not get a cancel yet

    // If more events come in, there will be no more foreground window to send them to. This will
    // cause a cancel for the monitor, as well.
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {120, 200}))
            << "Injection should fail because the window was removed";
    window->assertNoEvents();
    // Global monitor now gets the cancel
    monitor.consumeMotionCancel(ui::LogicalDisplayId::DEFAULT);
}

TEST_F(InputDispatcherMonitorTest, ReceivesMotionEvents) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    FakeMonitorReceiver monitor =
            FakeMonitorReceiver(*mDispatcher, "M_1", ui::LogicalDisplayId::DEFAULT);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    window->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    monitor.consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
}

TEST_F(InputDispatcherMonitorTest, MonitorCannotPilferPointers) {
    FakeMonitorReceiver monitor =
            FakeMonitorReceiver(*mDispatcher, "M_1", ui::LogicalDisplayId::DEFAULT);

    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    monitor.consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    window->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);

    // Pilfer pointers from the monitor.
    // This should not do anything and the window should continue to receive events.
    EXPECT_NE(OK, mDispatcher->pilferPointers(monitor.getToken()));

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    monitor.consumeMotionMove(ui::LogicalDisplayId::DEFAULT);
    window->consumeMotionMove(ui::LogicalDisplayId::DEFAULT);
}

TEST_F(InputDispatcherMonitorTest, NoWindowTransform) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    window->setWindowOffset(20, 40);
    window->setWindowTransform(0, 1, -1, 0);

    FakeMonitorReceiver monitor =
            FakeMonitorReceiver(*mDispatcher, "M_1", ui::LogicalDisplayId::DEFAULT);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    window->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    std::unique_ptr<MotionEvent> event = monitor.consumeMotion();
    ASSERT_NE(nullptr, event);
    // Even though window has transform, gesture monitor must not.
    ASSERT_EQ(ui::Transform(), event->getTransform());
}

TEST_F(InputDispatcherMonitorTest, InjectionFailsWithNoWindow) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    FakeMonitorReceiver monitor =
            FakeMonitorReceiver(*mDispatcher, "M_1", ui::LogicalDisplayId::DEFAULT);

    ASSERT_EQ(InputEventInjectionResult::FAILED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT))
            << "Injection should fail if there is a monitor, but no touchable window";
    monitor.assertNoEvents();
}

/**
 * Two displays
 * The first monitor has a foreground window, a monitor
 * The second window has only one monitor.
 * We first inject a Down event into the first display, this injection should succeed and both
 * the foreground window and monitor should receive a down event, then inject a Down event into
 * the second display as well, this injection should fail, at this point, the first display
 * window and monitor should not receive a cancel or any other event.
 * Continue to inject Move and UP events to the first display, the events should be received
 * normally by the foreground window and monitor.
 */
TEST_F(InputDispatcherMonitorTest, MonitorTouchIsNotCanceledWhenAnotherEmptyDisplayReceiveEvents) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Foreground",
                                                             ui::LogicalDisplayId::DEFAULT);

    FakeMonitorReceiver monitor =
            FakeMonitorReceiver(*mDispatcher, "M_1", ui::LogicalDisplayId::DEFAULT);
    FakeMonitorReceiver secondMonitor = FakeMonitorReceiver(*mDispatcher, "M_2", SECOND_DISPLAY_ID);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {100, 200}))
            << "The down event injected into the first display should succeed";

    window->consumeMotionDown();
    monitor.consumeMotionDown(ui::LogicalDisplayId::DEFAULT);

    ASSERT_EQ(InputEventInjectionResult::FAILED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, SECOND_DISPLAY_ID,
                               {100, 200}))
            << "The down event injected into the second display should fail since there's no "
               "touchable window";

    // Continue to inject event to first display.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {110, 220}))
            << "The move event injected into the first display should succeed";

    window->consumeMotionMove();
    monitor.consumeMotionMove(ui::LogicalDisplayId::DEFAULT);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionUp(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, ui::LogicalDisplayId::DEFAULT,
                             {110, 220}))
            << "The up event injected into the first display should succeed";

    window->consumeMotionUp();
    monitor.consumeMotionUp(ui::LogicalDisplayId::DEFAULT);

    window->assertNoEvents();
    monitor.assertNoEvents();
    secondMonitor.assertNoEvents();
}

/**
 * Two displays
 * There is a monitor and foreground window on each display.
 * First, we inject down events into each of the two displays, at this point, the foreground windows
 * and monitors on both displays should receive down events.
 * At this point, the foreground window of the second display goes away, the gone window should
 * receive the cancel event, and the other windows and monitors should not receive any events.
 * Inject a move event into the second display. At this point, the injection should fail because
 * the second display no longer has a foreground window. At this point, the monitor on the second
 * display should receive a cancel event, and any windows or monitors on the first display should
 * not receive any events, and any subsequent injection of events into the second display should
 * also fail.
 * Continue to inject events into the first display, and the events should all be injected
 * successfully and received normally.
 */
TEST_F(InputDispatcherMonitorTest, MonitorTouchIsNotCancelWhenAnotherDisplayMonitorTouchCanceled) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Foreground",
                                                             ui::LogicalDisplayId::DEFAULT);
    sp<FakeWindowHandle> secondWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "SecondForeground",
                                       SECOND_DISPLAY_ID);

    FakeMonitorReceiver monitor =
            FakeMonitorReceiver(*mDispatcher, "M_1", ui::LogicalDisplayId::DEFAULT);
    FakeMonitorReceiver secondMonitor = FakeMonitorReceiver(*mDispatcher, "M_2", SECOND_DISPLAY_ID);

    // There is a foreground window on both displays.
    mDispatcher->onWindowInfosChanged({{*window->getInfo(), *secondWindow->getInfo()}, {}, 0, 0});
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {100, 200}))
            << "The down event injected into the first display should succeed";

    window->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    monitor.consumeMotionDown(ui::LogicalDisplayId::DEFAULT);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, SECOND_DISPLAY_ID,
                               {100, 200}))
            << "The down event injected into the second display should succeed";

    secondWindow->consumeMotionDown(SECOND_DISPLAY_ID);
    secondMonitor.consumeMotionDown(SECOND_DISPLAY_ID);

    // Now second window is gone away.
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    // The gone window should receive a cancel, and the monitor on the second display should not
    // receive any events.
    secondWindow->consumeMotionCancel(SECOND_DISPLAY_ID);
    secondMonitor.assertNoEvents();

    ASSERT_EQ(InputEventInjectionResult::FAILED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                SECOND_DISPLAY_ID, {110, 220}))
            << "The move event injected into the second display should fail because there's no "
               "touchable window";
    // Now the monitor on the second display should receive a cancel event.
    secondMonitor.consumeMotionCancel(SECOND_DISPLAY_ID);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {110, 200}))
            << "The move event injected into the first display should succeed";

    window->consumeMotionMove();
    monitor.consumeMotionMove(ui::LogicalDisplayId::DEFAULT);

    ASSERT_EQ(InputEventInjectionResult::FAILED,
              injectMotionUp(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, SECOND_DISPLAY_ID,
                             {110, 220}))
            << "The up event injected into the second display should fail because there's no "
               "touchable window";

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionUp(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, ui::LogicalDisplayId::DEFAULT,
                             {110, 220}))
            << "The up event injected into the first display should succeed";

    window->consumeMotionUp(ui::LogicalDisplayId::DEFAULT);
    monitor.consumeMotionUp(ui::LogicalDisplayId::DEFAULT);

    window->assertNoEvents();
    monitor.assertNoEvents();
    secondWindow->assertNoEvents();
    secondMonitor.assertNoEvents();
}

/**
 * One display with transform
 * There is a foreground window and a monitor on the display
 * Inject down event and move event sequentially, the foreground window and monitor can receive down
 * event and move event, then let the foreground window go away, the foreground window receives
 * cancel event, inject move event again, the monitor receives cancel event, all the events received
 * by the monitor should be with the same transform as the display
 */
TEST_F(InputDispatcherMonitorTest, MonitorTouchCancelEventWithDisplayTransform) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Foreground",
                                                             ui::LogicalDisplayId::DEFAULT);
    FakeMonitorReceiver monitor =
            FakeMonitorReceiver(*mDispatcher, "M_1", ui::LogicalDisplayId::DEFAULT);

    ui::Transform transform;
    transform.set({1.1, 2.2, 3.3, 4.4, 5.5, 6.6, 0, 0, 1});

    gui::DisplayInfo displayInfo;
    displayInfo.displayId = ui::LogicalDisplayId::DEFAULT;
    displayInfo.transform = transform;

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {displayInfo}, 0, 0});

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {100, 200}))
            << "The down event injected should succeed";

    window->consumeMotionDown();
    std::unique_ptr<MotionEvent> downMotionEvent = monitor.consumeMotion();
    EXPECT_EQ(transform, downMotionEvent->getTransform());
    EXPECT_EQ(AMOTION_EVENT_ACTION_DOWN, downMotionEvent->getAction());

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {110, 220}))
            << "The move event injected should succeed";

    window->consumeMotionMove();
    std::unique_ptr<MotionEvent> moveMotionEvent = monitor.consumeMotion();
    EXPECT_EQ(transform, moveMotionEvent->getTransform());
    EXPECT_EQ(AMOTION_EVENT_ACTION_MOVE, moveMotionEvent->getAction());

    // Let foreground window gone
    mDispatcher->onWindowInfosChanged({{}, {displayInfo}, 0, 0});

    // Foreground window should receive a cancel event, but not the monitor.
    window->consumeMotionCancel();

    ASSERT_EQ(InputEventInjectionResult::FAILED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {110, 220}))
            << "The move event injected should failed";
    // Now foreground should not receive any events, but monitor should receive a cancel event
    // with transform that same as display's display.
    std::unique_ptr<MotionEvent> cancelMotionEvent = monitor.consumeMotion();
    EXPECT_EQ(transform, cancelMotionEvent->getTransform());
    EXPECT_EQ(ui::LogicalDisplayId::DEFAULT, cancelMotionEvent->getDisplayId());
    EXPECT_EQ(AMOTION_EVENT_ACTION_CANCEL, cancelMotionEvent->getAction());

    // Other event inject to this display should fail.
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {110, 220}))
            << "The up event injected should fail because the touched window was removed";
    window->assertNoEvents();
    monitor.assertNoEvents();
}

TEST_F(InputDispatcherTest, TestMoveEvent) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                       ui::LogicalDisplayId::DEFAULT);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    NotifyMotionArgs motionArgs =
            generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT);

    mDispatcher->notifyMotion(motionArgs);
    // Window should receive motion down event.
    window->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);

    motionArgs.action = AMOTION_EVENT_ACTION_MOVE;
    motionArgs.id += 1;
    motionArgs.eventTime = systemTime(SYSTEM_TIME_MONOTONIC);
    motionArgs.pointerCoords[0].setAxisValue(AMOTION_EVENT_AXIS_X,
                                             motionArgs.pointerCoords[0].getX() - 10);

    mDispatcher->notifyMotion(motionArgs);
    window->consumeMotionMove(ui::LogicalDisplayId::DEFAULT, /*expectedFlags=*/0);
}

/**
 * Dispatcher has touch mode enabled by default. Typically, the policy overrides that value to
 * the device default right away. In the test scenario, we check both the default value,
 * and the action of enabling / disabling.
 */
TEST_F(InputDispatcherTest, TouchModeState_IsSentToApps) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Test window",
                                       ui::LogicalDisplayId::DEFAULT);
    const WindowInfo& windowInfo = *window->getInfo();

    // Set focused application.
    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);
    window->setFocusable(true);

    SCOPED_TRACE("Check default value of touch mode");
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);
    window->consumeFocusEvent(/*hasFocus=*/true, /*inTouchMode=*/true);

    SCOPED_TRACE("Remove the window to trigger focus loss");
    window->setFocusable(false);
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    window->consumeFocusEvent(/*hasFocus=*/false, /*inTouchMode=*/true);

    SCOPED_TRACE("Disable touch mode");
    mDispatcher->setInTouchMode(false, windowInfo.ownerPid, windowInfo.ownerUid,
                                /*hasPermission=*/true, ui::LogicalDisplayId::DEFAULT);
    window->consumeTouchModeEvent(false);
    window->setFocusable(true);
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);
    window->consumeFocusEvent(/*hasFocus=*/true, /*inTouchMode=*/false);

    SCOPED_TRACE("Remove the window to trigger focus loss");
    window->setFocusable(false);
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    window->consumeFocusEvent(/*hasFocus=*/false, /*inTouchMode=*/false);

    SCOPED_TRACE("Enable touch mode again");
    mDispatcher->setInTouchMode(true, windowInfo.ownerPid, windowInfo.ownerUid,
                                /*hasPermission=*/true, ui::LogicalDisplayId::DEFAULT);
    window->consumeTouchModeEvent(true);
    window->setFocusable(true);
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);
    window->consumeFocusEvent(/*hasFocus=*/true, /*inTouchMode=*/true);

    window->assertNoEvents();
}

TEST_F(InputDispatcherTest, VerifyInputEvent_KeyEvent) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Test window",
                                       ui::LogicalDisplayId::DEFAULT);

    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);
    window->setFocusable(true);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);

    window->consumeFocusEvent(/*hasFocus=*/true, /*inTouchMode=*/true);

    const NotifyKeyArgs keyArgs = generateKeyArgs(AKEY_EVENT_ACTION_DOWN);
    mDispatcher->notifyKey(keyArgs);

    std::unique_ptr<KeyEvent> event = window->consumeKey();
    ASSERT_NE(event, nullptr);
    std::unique_ptr<VerifiedInputEvent> verified = mDispatcher->verifyInputEvent(*event);
    ASSERT_NE(verified, nullptr);
    ASSERT_EQ(verified->type, VerifiedInputEvent::Type::KEY);

    ASSERT_EQ(keyArgs.eventTime, verified->eventTimeNanos);
    ASSERT_EQ(keyArgs.deviceId, verified->deviceId);
    ASSERT_EQ(keyArgs.source, verified->source);
    ASSERT_EQ(keyArgs.displayId, verified->displayId);

    const VerifiedKeyEvent& verifiedKey = static_cast<const VerifiedKeyEvent&>(*verified);

    ASSERT_EQ(keyArgs.action, verifiedKey.action);
    ASSERT_EQ(keyArgs.flags & VERIFIED_KEY_EVENT_FLAGS, verifiedKey.flags);
    ASSERT_EQ(keyArgs.downTime, verifiedKey.downTimeNanos);
    ASSERT_EQ(keyArgs.keyCode, verifiedKey.keyCode);
    ASSERT_EQ(keyArgs.scanCode, verifiedKey.scanCode);
    ASSERT_EQ(keyArgs.metaState, verifiedKey.metaState);
    ASSERT_EQ(0, verifiedKey.repeatCount);
}

TEST_F(InputDispatcherTest, VerifyInputEvent_MotionEvent) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Test window",
                                       ui::LogicalDisplayId::DEFAULT);

    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);

    ui::Transform transform;
    transform.set({1.1, 2.2, 3.3, 4.4, 5.5, 6.6, 0, 0, 1});

    gui::DisplayInfo displayInfo;
    displayInfo.displayId = ui::LogicalDisplayId::DEFAULT;
    displayInfo.transform = transform;

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {displayInfo}, 0, 0});

    const NotifyMotionArgs motionArgs =
            generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT);
    mDispatcher->notifyMotion(motionArgs);

    std::unique_ptr<MotionEvent> event = window->consumeMotionEvent();
    ASSERT_NE(nullptr, event);
    std::unique_ptr<VerifiedInputEvent> verified = mDispatcher->verifyInputEvent(*event);
    ASSERT_NE(verified, nullptr);
    ASSERT_EQ(verified->type, VerifiedInputEvent::Type::MOTION);

    EXPECT_EQ(motionArgs.eventTime, verified->eventTimeNanos);
    EXPECT_EQ(motionArgs.deviceId, verified->deviceId);
    EXPECT_EQ(motionArgs.source, verified->source);
    EXPECT_EQ(motionArgs.displayId, verified->displayId);

    const VerifiedMotionEvent& verifiedMotion = static_cast<const VerifiedMotionEvent&>(*verified);

    const vec2 rawXY =
            MotionEvent::calculateTransformedXY(motionArgs.source, transform,
                                                motionArgs.pointerCoords[0].getXYValue());
    EXPECT_EQ(rawXY.x, verifiedMotion.rawX);
    EXPECT_EQ(rawXY.y, verifiedMotion.rawY);
    EXPECT_EQ(motionArgs.action & AMOTION_EVENT_ACTION_MASK, verifiedMotion.actionMasked);
    EXPECT_EQ(motionArgs.flags & VERIFIED_MOTION_EVENT_FLAGS, verifiedMotion.flags);
    EXPECT_EQ(motionArgs.downTime, verifiedMotion.downTimeNanos);
    EXPECT_EQ(motionArgs.metaState, verifiedMotion.metaState);
    EXPECT_EQ(motionArgs.buttonState, verifiedMotion.buttonState);
}

/**
 * Ensure that separate calls to sign the same data are generating the same key.
 * We avoid asserting against INVALID_HMAC. Since the key is random, there is a non-zero chance
 * that a specific key and data combination would produce INVALID_HMAC, which would cause flaky
 * tests.
 */
TEST_F(InputDispatcherTest, GeneratedHmac_IsConsistent) {
    KeyEvent event = getTestKeyEvent();
    VerifiedKeyEvent verifiedEvent = verifiedKeyEventFromKeyEvent(event);

    std::array<uint8_t, 32> hmac1 = mDispatcher->sign(verifiedEvent);
    std::array<uint8_t, 32> hmac2 = mDispatcher->sign(verifiedEvent);
    ASSERT_EQ(hmac1, hmac2);
}

/**
 * Ensure that changes in VerifiedKeyEvent produce a different hmac.
 */
TEST_F(InputDispatcherTest, GeneratedHmac_ChangesWhenFieldsChange) {
    KeyEvent event = getTestKeyEvent();
    VerifiedKeyEvent verifiedEvent = verifiedKeyEventFromKeyEvent(event);
    std::array<uint8_t, 32> initialHmac = mDispatcher->sign(verifiedEvent);

    verifiedEvent.deviceId += 1;
    ASSERT_NE(initialHmac, mDispatcher->sign(verifiedEvent));

    verifiedEvent.source += 1;
    ASSERT_NE(initialHmac, mDispatcher->sign(verifiedEvent));

    verifiedEvent.eventTimeNanos += 1;
    ASSERT_NE(initialHmac, mDispatcher->sign(verifiedEvent));

    verifiedEvent.displayId = ui::LogicalDisplayId{verifiedEvent.displayId.val() + 1};
    ASSERT_NE(initialHmac, mDispatcher->sign(verifiedEvent));

    verifiedEvent.action += 1;
    ASSERT_NE(initialHmac, mDispatcher->sign(verifiedEvent));

    verifiedEvent.downTimeNanos += 1;
    ASSERT_NE(initialHmac, mDispatcher->sign(verifiedEvent));

    verifiedEvent.flags += 1;
    ASSERT_NE(initialHmac, mDispatcher->sign(verifiedEvent));

    verifiedEvent.keyCode += 1;
    ASSERT_NE(initialHmac, mDispatcher->sign(verifiedEvent));

    verifiedEvent.scanCode += 1;
    ASSERT_NE(initialHmac, mDispatcher->sign(verifiedEvent));

    verifiedEvent.metaState += 1;
    ASSERT_NE(initialHmac, mDispatcher->sign(verifiedEvent));

    verifiedEvent.repeatCount += 1;
    ASSERT_NE(initialHmac, mDispatcher->sign(verifiedEvent));
}

TEST_F(InputDispatcherTest, SetFocusedWindow) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> windowTop = sp<FakeWindowHandle>::make(application, mDispatcher, "Top",
                                                                ui::LogicalDisplayId::DEFAULT);
    sp<FakeWindowHandle> windowSecond =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Second",
                                       ui::LogicalDisplayId::DEFAULT);
    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);

    // Top window is also focusable but is not granted focus.
    windowTop->setFocusable(true);
    windowSecond->setFocusable(true);
    mDispatcher->onWindowInfosChanged(
            {{*windowTop->getInfo(), *windowSecond->getInfo()}, {}, 0, 0});
    setFocusedWindow(windowSecond);

    windowSecond->consumeFocusEvent(true);
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyDown(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";

    // Focused window should receive event.
    windowSecond->consumeKeyDown(ui::LogicalDisplayId::INVALID);
    windowTop->assertNoEvents();
}

TEST_F(InputDispatcherTest, SetFocusedWindow_DropRequestInvalidChannel) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "TestWindow",
                                                             ui::LogicalDisplayId::DEFAULT);
    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);

    window->setFocusable(true);
    // Release channel for window is no longer valid.
    window->releaseChannel();
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);

    // Test inject a key down, should timeout.
    ASSERT_NO_FATAL_FAILURE(assertInjectedKeyTimesOut(*mDispatcher));

    // window channel is invalid, so it should not receive any input event.
    window->assertNoEvents();
}

TEST_F(InputDispatcherTest, SetFocusedWindow_DropRequestNoFocusableWindow) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "TestWindow",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFocusable(false);
    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);

    // Test inject a key down, should timeout.
    ASSERT_NO_FATAL_FAILURE(assertInjectedKeyTimesOut(*mDispatcher));

    // window is not focusable, so it should not receive any input event.
    window->assertNoEvents();
}

TEST_F(InputDispatcherTest, SetFocusedWindow_CheckFocusedToken) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> windowTop = sp<FakeWindowHandle>::make(application, mDispatcher, "Top",
                                                                ui::LogicalDisplayId::DEFAULT);
    sp<FakeWindowHandle> windowSecond =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Second",
                                       ui::LogicalDisplayId::DEFAULT);
    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);

    windowTop->setFocusable(true);
    windowSecond->setFocusable(true);
    mDispatcher->onWindowInfosChanged(
            {{*windowTop->getInfo(), *windowSecond->getInfo()}, {}, 0, 0});
    setFocusedWindow(windowTop);
    windowTop->consumeFocusEvent(true);

    windowTop->editInfo()->focusTransferTarget = windowSecond->getToken();
    mDispatcher->onWindowInfosChanged(
            {{*windowTop->getInfo(), *windowSecond->getInfo()}, {}, 0, 0});
    windowSecond->consumeFocusEvent(true);
    windowTop->consumeFocusEvent(false);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyDown(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";

    // Focused window should receive event.
    windowSecond->consumeKeyDown(ui::LogicalDisplayId::INVALID);
}

TEST_F(InputDispatcherTest, SetFocusedWindow_TransferFocusTokenNotFocusable) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> windowTop = sp<FakeWindowHandle>::make(application, mDispatcher, "Top",
                                                                ui::LogicalDisplayId::DEFAULT);
    sp<FakeWindowHandle> windowSecond =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Second",
                                       ui::LogicalDisplayId::DEFAULT);
    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);

    windowTop->setFocusable(true);
    windowSecond->setFocusable(false);
    windowTop->editInfo()->focusTransferTarget = windowSecond->getToken();
    mDispatcher->onWindowInfosChanged(
            {{*windowTop->getInfo(), *windowSecond->getInfo()}, {}, 0, 0});
    setFocusedWindow(windowTop);
    windowTop->consumeFocusEvent(true);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyDown(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";

    // Event should be dropped.
    windowTop->consumeKeyDown(ui::LogicalDisplayId::INVALID);
    windowSecond->assertNoEvents();
}

TEST_F(InputDispatcherTest, SetFocusedWindow_DeferInvisibleWindow) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "TestWindow",
                                                             ui::LogicalDisplayId::DEFAULT);
    sp<FakeWindowHandle> previousFocusedWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "previousFocusedWindow",
                                       ui::LogicalDisplayId::DEFAULT);
    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);

    window->setFocusable(true);
    previousFocusedWindow->setFocusable(true);
    window->setVisible(false);
    mDispatcher->onWindowInfosChanged(
            {{*window->getInfo(), *previousFocusedWindow->getInfo()}, {}, 0, 0});
    setFocusedWindow(previousFocusedWindow);
    previousFocusedWindow->consumeFocusEvent(true);

    // Requesting focus on invisible window takes focus from currently focused window.
    setFocusedWindow(window);
    previousFocusedWindow->consumeFocusEvent(false);

    // Injected key goes to pending queue.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectKey(*mDispatcher, AKEY_EVENT_ACTION_DOWN, /*repeatCount=*/0,
                        ui::LogicalDisplayId::DEFAULT, InputEventInjectionSync::NONE));

    // Window does not get focus event or key down.
    window->assertNoEvents();

    // Window becomes visible.
    window->setVisible(true);
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    // Window receives focus event.
    window->consumeFocusEvent(true);
    // Focused window receives key down.
    window->consumeKeyDown(ui::LogicalDisplayId::DEFAULT);
}

TEST_F(InputDispatcherTest, DisplayRemoved) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "window",
                                                             ui::LogicalDisplayId::DEFAULT);
    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);

    // window is granted focus.
    window->setFocusable(true);
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);
    window->consumeFocusEvent(true);

    // When a display is removed window loses focus.
    mDispatcher->displayRemoved(ui::LogicalDisplayId::DEFAULT);
    window->consumeFocusEvent(false);
}

/**
 * Launch two windows, with different owners. One window (slipperyExitWindow) has Flag::SLIPPERY,
 * and overlaps the other window, slipperyEnterWindow. The window 'slipperyExitWindow' is on top
 * of the 'slipperyEnterWindow'.
 *
 * Inject touch down into the top window. Upon receipt of the DOWN event, move the window in such
 * a way so that the touched location is no longer covered by the top window.
 *
 * Next, inject a MOVE event. Because the top window already moved earlier, this event is now
 * positioned over the bottom (slipperyEnterWindow) only. And because the top window had
 * Flag::SLIPPERY, this will cause the top window to lose the touch event (it will receive
 * ACTION_CANCEL instead), and the bottom window will receive a newly generated gesture (starting
 * with ACTION_DOWN).
 * Thus, the touch has been transferred from the top window into the bottom window, because the top
 * window moved itself away from the touched location and had Flag::SLIPPERY.
 *
 * Even though the top window moved away from the touched location, it is still obscuring the bottom
 * window. It's just not obscuring it at the touched location. That means, FLAG_WINDOW_IS_PARTIALLY_
 * OBSCURED should be set for the MotionEvent that reaches the bottom window.
 *
 * In this test, we ensure that the event received by the bottom window has
 * FLAG_WINDOW_IS_PARTIALLY_OBSCURED.
 */
TEST_F(InputDispatcherTest, SlipperyWindow_SetsFlagPartiallyObscured) {
    constexpr gui::Pid SLIPPERY_PID{WINDOW_PID.val() + 1};
    constexpr gui::Uid SLIPPERY_UID{WINDOW_UID.val() + 1};

    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);

    sp<FakeWindowHandle> slipperyExitWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Top",
                                       ui::LogicalDisplayId::DEFAULT);
    slipperyExitWindow->setSlippery(true);
    // Make sure this one overlaps the bottom window
    slipperyExitWindow->setFrame(Rect(25, 25, 75, 75));
    // Change the owner uid/pid of the window so that it is considered to be occluding the bottom
    // one. Windows with the same owner are not considered to be occluding each other.
    slipperyExitWindow->setOwnerInfo(SLIPPERY_PID, SLIPPERY_UID);

    sp<FakeWindowHandle> slipperyEnterWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Second",
                                       ui::LogicalDisplayId::DEFAULT);
    slipperyExitWindow->setFrame(Rect(0, 0, 100, 100));

    mDispatcher->onWindowInfosChanged(
            {{*slipperyExitWindow->getInfo(), *slipperyEnterWindow->getInfo()}, {}, 0, 0});

    // Use notifyMotion instead of injecting to avoid dealing with injection permissions
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT, {{50, 50}}));
    slipperyExitWindow->consumeMotionDown();
    slipperyExitWindow->setFrame(Rect(70, 70, 100, 100));
    mDispatcher->onWindowInfosChanged(
            {{*slipperyExitWindow->getInfo(), *slipperyEnterWindow->getInfo()}, {}, 0, 0});

    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_MOVE,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT, {{51, 51}}));

    slipperyExitWindow->consumeMotionCancel();

    slipperyEnterWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT,
                                           AMOTION_EVENT_FLAG_WINDOW_IS_PARTIALLY_OBSCURED);
}

/**
 * Two windows, one on the left and another on the right. The left window is slippery. The right
 * window isn't eligible to receive touch because it specifies InputConfig::DROP_INPUT. When the
 * touch moves from the left window into the right window, the gesture should continue to go to the
 * left window. Touch shouldn't slip because the right window can't receive touches. This test
 * reproduces a crash.
 */
TEST_F(InputDispatcherTest, TouchSlippingIntoWindowThatDropsTouches) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> leftSlipperyWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Left",
                                       ui::LogicalDisplayId::DEFAULT);
    leftSlipperyWindow->setSlippery(true);
    leftSlipperyWindow->setFrame(Rect(0, 0, 100, 100));

    sp<FakeWindowHandle> rightDropTouchesWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Right",
                                       ui::LogicalDisplayId::DEFAULT);
    rightDropTouchesWindow->setFrame(Rect(100, 0, 200, 100));
    rightDropTouchesWindow->setDropInput(true);

    mDispatcher->onWindowInfosChanged(
            {{*leftSlipperyWindow->getInfo(), *rightDropTouchesWindow->getInfo()}, {}, 0, 0});

    // Start touch in the left window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .build());
    leftSlipperyWindow->consumeMotionDown();

    // And move it into the right window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(150).y(50))
                                      .build());

    // Since the right window isn't eligible to receive input, touch does not slip.
    // The left window continues to receive the gesture.
    leftSlipperyWindow->consumeMotionEvent(WithMotionAction(ACTION_MOVE));
    rightDropTouchesWindow->assertNoEvents();
}

/**
 * A single window is on screen first. Touch is injected into that window. Next, a second window
 * appears. Since the first window is slippery, touch will move from the first window to the second.
 */
TEST_F(InputDispatcherTest, InjectedTouchSlips) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> originalWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Original",
                                       ui::LogicalDisplayId::DEFAULT);
    originalWindow->setFrame(Rect(0, 0, 200, 200));
    originalWindow->setSlippery(true);

    sp<FakeWindowHandle> appearingWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Appearing",
                                       ui::LogicalDisplayId::DEFAULT);
    appearingWindow->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*originalWindow->getInfo()}, {}, 0, 0});

    // Touch down on the original window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                        .pointer(PointerBuilder(1, ToolType::FINGER).x(100).y(100))
                                        .build()));
    originalWindow->consumeMotionEvent(WithMotionAction(ACTION_DOWN));

    // Now, a new window appears. This could be, for example, a notification shade that appears
    // after user starts to drag down on the launcher window.
    mDispatcher->onWindowInfosChanged(
            {{*appearingWindow->getInfo(), *originalWindow->getInfo()}, {}, 0, 0});
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                        .pointer(PointerBuilder(1, ToolType::FINGER).x(110).y(110))
                                        .build()));
    originalWindow->consumeMotionEvent(WithMotionAction(ACTION_CANCEL));
    appearingWindow->consumeMotionEvent(WithMotionAction(ACTION_DOWN));
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                        .pointer(PointerBuilder(1, ToolType::FINGER).x(120).y(120))
                                        .build()));
    appearingWindow->consumeMotionEvent(WithMotionAction(ACTION_MOVE));

    originalWindow->assertNoEvents();
    appearingWindow->assertNoEvents();
}

/**
 * Three windows:
 * - left window, which has FLAG_SLIPPERY, so it supports slippery exit
 * - right window
 * - spy window
 * The three windows do not overlap.
 *
 * We have two devices reporting events:
 * - Device A reports ACTION_DOWN, which lands in the left window
 * - Device B reports ACTION_DOWN, which lands in the spy window.
 * - Now, device B reports ACTION_MOVE events which move to the right window.
 *
 * The right window should not receive any events because the spy window is not a foreground window,
 * and also it does not support slippery touches.
 */
TEST_F(InputDispatcherTest, MultiDeviceSpyWindowSlipTest) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> leftWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Left window",
                                       ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect(0, 0, 100, 100));
    leftWindow->setSlippery(true);

    sp<FakeWindowHandle> rightWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Right window",
                                       ui::LogicalDisplayId::DEFAULT);
    rightWindow->setFrame(Rect(100, 0, 200, 100));

    sp<FakeWindowHandle> spyWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Spy Window",
                                       ui::LogicalDisplayId::DEFAULT);
    spyWindow->setFrame(Rect(200, 0, 300, 100));
    spyWindow->setSpy(true);
    spyWindow->setTrustedOverlay(true);

    mDispatcher->onWindowInfosChanged(
            {{*leftWindow->getInfo(), *rightWindow->getInfo(), *spyWindow->getInfo()}, {}, 0, 0});

    const DeviceId deviceA = 9;
    const DeviceId deviceB = 3;

    // Tap on left window with device A
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .deviceId(deviceA)
                                      .build());
    leftWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(deviceA)));

    // Tap on spy window with device B
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(250).y(50))
                                      .deviceId(deviceB)
                                      .build());
    spyWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(deviceB)));

    // Move to right window with device B. Touches should not slip to the right window, because spy
    // window is not a foreground window, and it does not have FLAG_SLIPPERY
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(150).y(50))
                                      .deviceId(deviceB)
                                      .build());
    leftWindow->assertNoEvents();
    rightWindow->assertNoEvents();
    spyWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(deviceB)));
}

/**
 * Three windows arranged horizontally and without any overlap.
 * The left and right windows have FLAG_SLIPPERY. The middle window does not have any special flags.
 *
 * We have two devices reporting events:
 * - Device A reports ACTION_DOWN which lands in the left window
 * - Device B reports ACTION_DOWN which lands in the right window
 * - Device B reports ACTION_MOVE that shifts to the middle window.
 * This should cause touches for Device B to slip from the right window to the middle window.
 * The right window should receive ACTION_CANCEL for device B and the
 * middle window should receive down event for Device B.
 * If device B reports more ACTION_MOVE events, the middle window should receive remaining events.
 */
TEST_F(InputDispatcherTest, MultiDeviceSlipperyWindowTest) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> leftWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Left window",
                                       ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect(0, 0, 100, 100));
    leftWindow->setSlippery(true);

    sp<FakeWindowHandle> middleWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "middle window",
                                       ui::LogicalDisplayId::DEFAULT);
    middleWindow->setFrame(Rect(100, 0, 200, 100));

    sp<FakeWindowHandle> rightWindow =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Right window",
                                       ui::LogicalDisplayId::DEFAULT);
    rightWindow->setFrame(Rect(200, 0, 300, 100));
    rightWindow->setSlippery(true);

    mDispatcher->onWindowInfosChanged(
            {{*leftWindow->getInfo(), *middleWindow->getInfo(), *rightWindow->getInfo()},
             {},
             0,
             0});

    const DeviceId deviceA = 9;
    const DeviceId deviceB = 3;

    // Tap on left window with device A
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .deviceId(deviceA)
                                      .build());
    leftWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(deviceA)));

    // Tap on right window with device B
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(250).y(50))
                                      .deviceId(deviceB)
                                      .build());
    rightWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(deviceB)));

    // Move to middle window with device B. Touches should slip to middle window, because right
    // window is a foreground window that's associated with device B and has FLAG_SLIPPERY.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(150).y(50))
                                      .deviceId(deviceB)
                                      .build());
    rightWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(deviceB)));
    middleWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(deviceB)));

    // Move to middle window with device A. Touches should slip to middle window, because left
    // window is a foreground window that's associated with device A and has FLAG_SLIPPERY.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(150).y(50))
                                      .deviceId(deviceA)
                                      .build());
    leftWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(deviceA)));
    middleWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(deviceA)));

    // Ensure that middle window can receive the remaining move events.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(151).y(51))
                                      .deviceId(deviceB)
                                      .build());
    leftWindow->assertNoEvents();
    middleWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(deviceB)));
    rightWindow->assertNoEvents();
}

TEST_F(InputDispatcherTest, NotifiesDeviceInteractionsWithMotions) {
    using Uid = gui::Uid;
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> leftWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Left",
                                                                 ui::LogicalDisplayId::DEFAULT);
    leftWindow->setFrame(Rect(0, 0, 100, 100));
    leftWindow->setOwnerInfo(gui::Pid{1}, Uid{101});

    sp<FakeWindowHandle> rightSpy =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Right spy",
                                       ui::LogicalDisplayId::DEFAULT);
    rightSpy->setFrame(Rect(100, 0, 200, 100));
    rightSpy->setOwnerInfo(gui::Pid{2}, Uid{102});
    rightSpy->setSpy(true);
    rightSpy->setTrustedOverlay(true);

    sp<FakeWindowHandle> rightWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Right",
                                                                  ui::LogicalDisplayId::DEFAULT);
    rightWindow->setFrame(Rect(100, 0, 200, 100));
    rightWindow->setOwnerInfo(gui::Pid{3}, Uid{103});

    mDispatcher->onWindowInfosChanged(
            {{*rightSpy->getInfo(), *rightWindow->getInfo(), *leftWindow->getInfo()}, {}, 0, 0});

    // Touch in the left window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .build());
    ASSERT_NO_FATAL_FAILURE(leftWindow->consumeMotionDown());
    mDispatcher->waitForIdle();
    ASSERT_NO_FATAL_FAILURE(
            mFakePolicy->assertNotifyDeviceInteractionWasCalled(DEVICE_ID, {Uid{101}}));

    // Touch another finger over the right windows
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(150).y(50))
                                      .build());
    ASSERT_NO_FATAL_FAILURE(rightSpy->consumeMotionDown());
    ASSERT_NO_FATAL_FAILURE(rightWindow->consumeMotionDown());
    ASSERT_NO_FATAL_FAILURE(leftWindow->consumeMotionMove());
    mDispatcher->waitForIdle();
    ASSERT_NO_FATAL_FAILURE(
            mFakePolicy->assertNotifyDeviceInteractionWasCalled(DEVICE_ID,
                                                                {Uid{101}, Uid{102}, Uid{103}}));

    // Release finger over left window. The UP actions are not treated as device interaction.
    // The windows that did not receive the UP pointer will receive MOVE events, but since this
    // is part of the UP action, we do not treat this as device interaction.
    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_0_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(150).y(50))
                                      .build());
    ASSERT_NO_FATAL_FAILURE(leftWindow->consumeMotionUp());
    ASSERT_NO_FATAL_FAILURE(rightSpy->consumeMotionMove());
    ASSERT_NO_FATAL_FAILURE(rightWindow->consumeMotionMove());
    mDispatcher->waitForIdle();
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertNotifyDeviceInteractionWasNotCalled());

    // Move remaining finger
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(150).y(50))
                                      .build());
    ASSERT_NO_FATAL_FAILURE(rightSpy->consumeMotionMove());
    ASSERT_NO_FATAL_FAILURE(rightWindow->consumeMotionMove());
    mDispatcher->waitForIdle();
    ASSERT_NO_FATAL_FAILURE(
            mFakePolicy->assertNotifyDeviceInteractionWasCalled(DEVICE_ID, {Uid{102}, Uid{103}}));

    // Release all fingers
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(150).y(50))
                                      .build());
    ASSERT_NO_FATAL_FAILURE(rightSpy->consumeMotionUp());
    ASSERT_NO_FATAL_FAILURE(rightWindow->consumeMotionUp());
    mDispatcher->waitForIdle();
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertNotifyDeviceInteractionWasNotCalled());
}

TEST_F(InputDispatcherTest, NotifiesDeviceInteractionsWithKeys) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 100, 100));
    window->setOwnerInfo(gui::Pid{1}, gui::Uid{101});

    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);
    ASSERT_NO_FATAL_FAILURE(window->consumeFocusEvent(true));

    mDispatcher->notifyKey(KeyArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).build());
    ASSERT_NO_FATAL_FAILURE(window->consumeKeyDown(ui::LogicalDisplayId::DEFAULT));
    mDispatcher->waitForIdle();
    ASSERT_NO_FATAL_FAILURE(
            mFakePolicy->assertNotifyDeviceInteractionWasCalled(DEVICE_ID, {gui::Uid{101}}));

    // The UP actions are not treated as device interaction.
    mDispatcher->notifyKey(KeyArgsBuilder(ACTION_UP, AINPUT_SOURCE_KEYBOARD).build());
    ASSERT_NO_FATAL_FAILURE(window->consumeKeyUp(ui::LogicalDisplayId::DEFAULT));
    mDispatcher->waitForIdle();
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertNotifyDeviceInteractionWasNotCalled());
}

TEST_F(InputDispatcherTest, HoverEnterExitSynthesisUsesNewEventId) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> left = sp<FakeWindowHandle>::make(application, mDispatcher, "Left Window",
                                                           ui::LogicalDisplayId::DEFAULT);
    left->setFrame(Rect(0, 0, 100, 100));
    sp<FakeWindowHandle> right =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Right Window",
                                       ui::LogicalDisplayId::DEFAULT);
    right->setFrame(Rect(100, 0, 200, 100));
    sp<FakeWindowHandle> spy = sp<FakeWindowHandle>::make(application, mDispatcher, "Spy Window",
                                                          ui::LogicalDisplayId::DEFAULT);
    spy->setFrame(Rect(0, 0, 200, 100));
    spy->setTrustedOverlay(true);
    spy->setSpy(true);

    mDispatcher->onWindowInfosChanged(
            {{*spy->getInfo(), *left->getInfo(), *right->getInfo()}, {}, 0, 0});

    // Send hover move to the left window, and ensure hover enter is synthesized with a new eventId.
    NotifyMotionArgs notifyArgs =
            generateMotionArgs(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS,
                               ui::LogicalDisplayId::DEFAULT, {PointF{50, 50}});
    mDispatcher->notifyMotion(notifyArgs);

    std::unique_ptr<MotionEvent> leftEnter = left->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_ENTER), Not(WithEventId(notifyArgs.id)),
                  WithEventIdSource(IdGenerator::Source::INPUT_DISPATCHER)));
    ASSERT_NE(nullptr, leftEnter);
    spy->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_ENTER),
                                  Not(WithEventId(notifyArgs.id)),
                                  Not(WithEventId(leftEnter->getId())),
                                  WithEventIdSource(IdGenerator::Source::INPUT_DISPATCHER)));

    // Send move to the right window, and ensure hover exit and enter are synthesized with new ids.
    notifyArgs = generateMotionArgs(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS,
                                    ui::LogicalDisplayId::DEFAULT, {PointF{150, 50}});
    mDispatcher->notifyMotion(notifyArgs);

    std::unique_ptr<MotionEvent> leftExit = left->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_EXIT), Not(WithEventId(notifyArgs.id)),
                  WithEventIdSource(IdGenerator::Source::INPUT_DISPATCHER)));
    ASSERT_NE(nullptr, leftExit);
    right->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_ENTER),
                                    Not(WithEventId(notifyArgs.id)),
                                    Not(WithEventId(leftExit->getId())),
                                    WithEventIdSource(IdGenerator::Source::INPUT_DISPATCHER)));

    spy->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_MOVE), WithEventId(notifyArgs.id)));
}

/**
 * When a device reports a DOWN event, which lands in a window that supports splits, and then the
 * device then reports a POINTER_DOWN, which lands in the location of a non-existing window, then
 * the previous window should receive this event and not be dropped.
 */
TEST_F(InputDispatcherMultiDeviceTest, SingleDevicePointerDownEventRetentionWithoutWindowTarget) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 100, 100));
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .build());

    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN)));

    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(200).y(200))
                                      .build());

    window->consumeMotionEvent(AllOf(WithMotionAction(POINTER_1_DOWN)));
}

/**
 * When deviceA reports a DOWN event, which lands in a window that supports splits, and then deviceB
 * also reports a DOWN event, which lands in the location of a non-existing window, then the
 * previous window should receive deviceB's event and it should be dropped.
 */
TEST_F(InputDispatcherMultiDeviceTest, SecondDeviceDownEventDroppedWithoutWindowTarget) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window = sp<FakeWindowHandle>::make(application, mDispatcher, "Window",
                                                             ui::LogicalDisplayId::DEFAULT);
    window->setFrame(Rect(0, 0, 100, 100));
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    const DeviceId deviceA = 9;
    const DeviceId deviceB = 3;

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .deviceId(deviceA)
                                      .build());
    window->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(deviceA)));

    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(200).y(200))
                                      .deviceId(deviceB)
                                      .build());
    window->assertNoEvents();
}

class InputDispatcherFallbackKeyTest : public InputDispatcherTest {
protected:
    std::shared_ptr<FakeApplicationHandle> mApp;
    sp<FakeWindowHandle> mWindow;

    virtual void SetUp() override {
        InputDispatcherTest::SetUp();

        mApp = std::make_shared<FakeApplicationHandle>();

        mWindow = sp<FakeWindowHandle>::make(mApp, mDispatcher, "Window",
                                             ui::LogicalDisplayId::DEFAULT);
        mWindow->setFrame(Rect(0, 0, 100, 100));

        mDispatcher->onWindowInfosChanged({{*mWindow->getInfo()}, {}, 0, 0});
        setFocusedWindow(mWindow);
        ASSERT_NO_FATAL_FAILURE(mWindow->consumeFocusEvent(/*hasFocus=*/true));
    }

    void setFallback(int32_t keycode) {
        mFakePolicy->setUnhandledKeyHandler([keycode](const KeyEvent& event) {
            return KeyEventBuilder(event).keyCode(keycode).build();
        });
    }

    void consumeKey(bool handled, const ::testing::Matcher<KeyEvent>& matcher) {
        std::unique_ptr<KeyEvent> event = mWindow->consumeKey(handled);
        ASSERT_NE(nullptr, event);
        ASSERT_THAT(*event, matcher);
    }
};

TEST_F(InputDispatcherFallbackKeyTest, PolicyNotNotifiedForHandledKey) {
    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());
    consumeKey(/*handled=*/true, AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_A)));
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyNotReported());
}

TEST_F(InputDispatcherFallbackKeyTest, PolicyNotifiedForUnhandledKey) {
    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());
    consumeKey(/*handled=*/false, AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_A)));
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyReported(AKEYCODE_A));
}

TEST_F(InputDispatcherFallbackKeyTest, NoFallbackRequestedByPolicy) {
    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());

    // Do not handle this key event.
    consumeKey(/*handled=*/false,
               AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_A), WithFlags(0)));
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyReported(AKEYCODE_A));

    // Since the policy did not request any fallback to be generated, ensure there are no events.
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyNotReported());
}

TEST_F(InputDispatcherFallbackKeyTest, FallbackDispatchForUnhandledKey) {
    setFallback(AKEYCODE_B);
    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());

    // Do not handle this key event.
    consumeKey(/*handled=*/false,
               AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_A), WithFlags(0)));

    // Since the key was not handled, ensure the fallback event was dispatched instead.
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyReported(AKEYCODE_A));
    consumeKey(/*handled=*/true,
               AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_B),
                     WithFlags(AKEY_EVENT_FLAG_FALLBACK)));

    // Release the original key, and ensure the fallback key is also released.
    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_UP, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());
    consumeKey(/*handled=*/false,
               AllOf(WithKeyAction(ACTION_UP), WithKeyCode(AKEYCODE_A), WithFlags(0)));
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyReported(AKEYCODE_A));
    consumeKey(/*handled=*/true,
               AllOf(WithKeyAction(ACTION_UP), WithKeyCode(AKEYCODE_B),
                     WithFlags(AKEY_EVENT_FLAG_FALLBACK)));

    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyNotReported());
    mWindow->assertNoEvents();
}

TEST_F(InputDispatcherFallbackKeyTest, AppHandlesPreviouslyUnhandledKey) {
    setFallback(AKEYCODE_B);
    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());

    // Do not handle this key event, but handle the fallback.
    consumeKey(/*handled=*/false,
               AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_A), WithFlags(0)));
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyReported(AKEYCODE_A));
    consumeKey(/*handled=*/true,
               AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_B),
                     WithFlags(AKEY_EVENT_FLAG_FALLBACK)));

    // Release the original key, and ensure the fallback key is also released.
    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_UP, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());
    // But this time, the app handles the original key.
    consumeKey(/*handled=*/true,
               AllOf(WithKeyAction(ACTION_UP), WithKeyCode(AKEYCODE_A), WithFlags(0)));
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyReported(AKEYCODE_A));
    // Ensure the fallback key is canceled.
    consumeKey(/*handled=*/true,
               AllOf(WithKeyAction(ACTION_UP), WithKeyCode(AKEYCODE_B),
                     WithFlags(AKEY_EVENT_FLAG_FALLBACK | AKEY_EVENT_FLAG_CANCELED)));

    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyNotReported());
    mWindow->assertNoEvents();
}

TEST_F(InputDispatcherFallbackKeyTest, AppDoesNotHandleFallback) {
    setFallback(AKEYCODE_B);
    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());

    // Do not handle this key event.
    consumeKey(/*handled=*/false,
               AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_A), WithFlags(0)));
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyReported(AKEYCODE_A));
    // App does not handle the fallback either, so ensure another fallback is not generated.
    setFallback(AKEYCODE_C);
    consumeKey(/*handled=*/false,
               AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_B),
                     WithFlags(AKEY_EVENT_FLAG_FALLBACK)));

    // Release the original key, and ensure the fallback key is also released.
    setFallback(AKEYCODE_B);
    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_UP, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());
    consumeKey(/*handled=*/false,
               AllOf(WithKeyAction(ACTION_UP), WithKeyCode(AKEYCODE_A), WithFlags(0)));
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyReported(AKEYCODE_A));
    consumeKey(/*handled=*/false,
               AllOf(WithKeyAction(ACTION_UP), WithKeyCode(AKEYCODE_B),
                     WithFlags(AKEY_EVENT_FLAG_FALLBACK)));

    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyNotReported());
    mWindow->assertNoEvents();
}

TEST_F(InputDispatcherFallbackKeyTest, InconsistentPolicyCancelsFallback) {
    setFallback(AKEYCODE_B);
    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());

    // Do not handle this key event, so fallback is generated.
    consumeKey(/*handled=*/false,
               AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_A), WithFlags(0)));
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyReported(AKEYCODE_A));
    consumeKey(/*handled=*/true,
               AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_B),
                     WithFlags(AKEY_EVENT_FLAG_FALLBACK)));

    // Release the original key, but assume the policy is misbehaving and it
    // generates an inconsistent fallback to the one from the DOWN event.
    setFallback(AKEYCODE_C);
    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_UP, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());
    consumeKey(/*handled=*/false,
               AllOf(WithKeyAction(ACTION_UP), WithKeyCode(AKEYCODE_A), WithFlags(0)));
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyReported(AKEYCODE_A));
    // Ensure the fallback key reported before as DOWN is canceled due to the inconsistency.
    consumeKey(/*handled=*/true,
               AllOf(WithKeyAction(ACTION_UP), WithKeyCode(AKEYCODE_B),
                     WithFlags(AKEY_EVENT_FLAG_FALLBACK | AKEY_EVENT_FLAG_CANCELED)));

    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyNotReported());
    mWindow->assertNoEvents();
}

TEST_F(InputDispatcherFallbackKeyTest, CanceledKeyCancelsFallback) {
    setFallback(AKEYCODE_B);
    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());

    // Do not handle this key event, so fallback is generated.
    consumeKey(/*handled=*/false,
               AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_A), WithFlags(0)));
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyReported(AKEYCODE_A));
    consumeKey(/*handled=*/true,
               AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_B),
                     WithFlags(AKEY_EVENT_FLAG_FALLBACK)));

    // The original key is canceled.
    mDispatcher->notifyKey(KeyArgsBuilder(ACTION_UP, AINPUT_SOURCE_KEYBOARD)
                                   .keyCode(AKEYCODE_A)
                                   .addFlag(AKEY_EVENT_FLAG_CANCELED)
                                   .build());
    consumeKey(/*handled=*/false,
               AllOf(WithKeyAction(ACTION_UP), WithKeyCode(AKEYCODE_A),
                     WithFlags(AKEY_EVENT_FLAG_CANCELED)));
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyReported(AKEYCODE_A));
    // Ensure the fallback key is also canceled due to the original key being canceled.
    consumeKey(/*handled=*/true,
               AllOf(WithKeyAction(ACTION_UP), WithKeyCode(AKEYCODE_B),
                     WithFlags(AKEY_EVENT_FLAG_FALLBACK | AKEY_EVENT_FLAG_CANCELED)));

    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyNotReported());
    mWindow->assertNoEvents();
}

TEST_F(InputDispatcherFallbackKeyTest, InputChannelRemovedDuringPolicyCall) {
    setFallback(AKEYCODE_B);
    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());

    // Do not handle this key event.
    consumeKey(/*handled=*/false,
               AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_A), WithFlags(0)));
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyReported(AKEYCODE_A));
    consumeKey(/*handled=*/true,
               AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_B),
                     WithFlags(AKEY_EVENT_FLAG_FALLBACK)));

    mFakePolicy->setUnhandledKeyHandler([&](const KeyEvent& event) {
        // When the unhandled key is reported to the policy next, remove the input channel.
        mDispatcher->removeInputChannel(mWindow->getToken());
        return KeyEventBuilder(event).keyCode(AKEYCODE_B).build();
    });
    // Release the original key, and let the app now handle the previously unhandled key.
    // This should result in the previously generated fallback key to be cancelled.
    // Since the policy was notified of the unhandled DOWN event earlier, it will also be notified
    // of the UP event for consistency. The Dispatcher calls into the policy from its own thread
    // without holding the lock, because it need to synchronously fetch the fallback key. While in
    // the policy call, we will now remove the input channel. Once the policy call returns, the
    // Dispatcher will no longer have a channel to send cancellation events to. Ensure this does
    // not cause any crashes.
    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_UP, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());
    consumeKey(/*handled=*/true,
               AllOf(WithKeyAction(ACTION_UP), WithKeyCode(AKEYCODE_A), WithFlags(0)));
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyReported(AKEYCODE_A));
}

TEST_F(InputDispatcherFallbackKeyTest, WindowRemovedDuringPolicyCall) {
    setFallback(AKEYCODE_B);
    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());

    // Do not handle this key event.
    consumeKey(/*handled=*/false,
               AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_A), WithFlags(0)));
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyReported(AKEYCODE_A));
    consumeKey(/*handled=*/true,
               AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_B),
                     WithFlags(AKEY_EVENT_FLAG_FALLBACK)));

    mFakePolicy->setUnhandledKeyHandler([&](const KeyEvent& event) {
        // When the unhandled key is reported to the policy next, remove the window.
        mDispatcher->onWindowInfosChanged({{}, {}, 0, 0});
        return KeyEventBuilder(event).keyCode(AKEYCODE_B).build();
    });
    // Release the original key, which the app will not handle. When this unhandled key is reported
    // to the policy, the window will be removed.
    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_UP, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());
    consumeKey(/*handled=*/false,
               AllOf(WithKeyAction(ACTION_UP), WithKeyCode(AKEYCODE_A), WithFlags(0)));
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyReported(AKEYCODE_A));

    // Since the window was removed, it loses focus, and the channel state will be reset.
    consumeKey(/*handled=*/true,
               AllOf(WithKeyAction(ACTION_UP), WithKeyCode(AKEYCODE_B),
                     WithFlags(AKEY_EVENT_FLAG_FALLBACK | AKEY_EVENT_FLAG_CANCELED)));
    mWindow->consumeFocusEvent(false);
    mWindow->assertNoEvents();
}

TEST_F(InputDispatcherFallbackKeyTest, WindowRemovedWhileAwaitingFinishedSignal) {
    setFallback(AKEYCODE_B);
    mDispatcher->notifyKey(
            KeyArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).keyCode(AKEYCODE_A).build());

    // Do not handle this key event.
    consumeKey(/*handled=*/false,
               AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_A), WithFlags(0)));
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertUnhandledKeyReported(AKEYCODE_A));
    const auto [seq, event] = mWindow->receiveEvent();
    ASSERT_TRUE(seq.has_value() && event != nullptr) << "Failed to receive fallback event";
    ASSERT_EQ(event->getType(), InputEventType::KEY);
    ASSERT_THAT(static_cast<const KeyEvent&>(*event),
                AllOf(WithKeyAction(ACTION_DOWN), WithKeyCode(AKEYCODE_B),
                      WithFlags(AKEY_EVENT_FLAG_FALLBACK)));

    // Remove the window now, which should generate a cancellations and make the window lose focus.
    mDispatcher->onWindowInfosChanged({{}, {}, 0, 0});
    consumeKey(/*handled=*/true,
               AllOf(WithKeyAction(ACTION_UP), WithKeyCode(AKEYCODE_A),
                     WithFlags(AKEY_EVENT_FLAG_CANCELED)));
    consumeKey(/*handled=*/true,
               AllOf(WithKeyAction(ACTION_UP), WithKeyCode(AKEYCODE_B),
                     WithFlags(AKEY_EVENT_FLAG_FALLBACK | AKEY_EVENT_FLAG_CANCELED)));
    mWindow->consumeFocusEvent(false);

    // Finish the event by reporting it as handled.
    mWindow->finishEvent(*seq);
    mWindow->assertNoEvents();
}

class InputDispatcherKeyRepeatTest : public InputDispatcherTest {
protected:
    static constexpr std::chrono::nanoseconds KEY_REPEAT_TIMEOUT = 40ms;
    static constexpr std::chrono::nanoseconds KEY_REPEAT_DELAY = 40ms;

    std::shared_ptr<FakeApplicationHandle> mApp;
    sp<FakeWindowHandle> mWindow;

    virtual void SetUp() override {
        InputDispatcherTest::SetUp();

        mDispatcher->setKeyRepeatConfiguration(KEY_REPEAT_TIMEOUT, KEY_REPEAT_DELAY);
        setUpWindow();
    }

    void setUpWindow() {
        mApp = std::make_shared<FakeApplicationHandle>();
        mWindow = sp<FakeWindowHandle>::make(mApp, mDispatcher, "Fake Window",
                                             ui::LogicalDisplayId::DEFAULT);

        mWindow->setFocusable(true);
        mDispatcher->onWindowInfosChanged({{*mWindow->getInfo()}, {}, 0, 0});
        setFocusedWindow(mWindow);
        mWindow->consumeFocusEvent(true);
    }

    void sendAndConsumeKeyDown(int32_t deviceId) {
        NotifyKeyArgs keyArgs =
                generateKeyArgs(AKEY_EVENT_ACTION_DOWN, ui::LogicalDisplayId::DEFAULT);
        keyArgs.deviceId = deviceId;
        keyArgs.policyFlags |= POLICY_FLAG_TRUSTED; // Otherwise it won't generate repeat event
        mDispatcher->notifyKey(keyArgs);

        // Window should receive key down event.
        mWindow->consumeKeyDown(ui::LogicalDisplayId::DEFAULT);
    }

    void expectKeyRepeatOnce(int32_t repeatCount) {
        SCOPED_TRACE(StringPrintf("Checking event with repeat count %" PRId32, repeatCount));
        mWindow->consumeKeyEvent(
                AllOf(WithKeyAction(AKEY_EVENT_ACTION_DOWN), WithRepeatCount(repeatCount)));
    }

    void sendAndConsumeKeyUp(int32_t deviceId) {
        NotifyKeyArgs keyArgs =
                generateKeyArgs(AKEY_EVENT_ACTION_UP, ui::LogicalDisplayId::DEFAULT);
        keyArgs.deviceId = deviceId;
        keyArgs.policyFlags |= POLICY_FLAG_TRUSTED; // Unless it won't generate repeat event
        mDispatcher->notifyKey(keyArgs);

        // Window should receive key down event.
        mWindow->consumeKeyUp(ui::LogicalDisplayId::DEFAULT,
                              /*expectedFlags=*/0);
    }

    void injectKeyRepeat(int32_t repeatCount) {
        ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
                  injectKey(*mDispatcher, AKEY_EVENT_ACTION_DOWN, repeatCount,
                            ui::LogicalDisplayId::DEFAULT))
                << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    }
};

TEST_F(InputDispatcherKeyRepeatTest, FocusedWindow_ReceivesKeyRepeat) {
    sendAndConsumeKeyDown(/*deviceId=*/1);
    for (int32_t repeatCount = 1; repeatCount <= 10; ++repeatCount) {
        expectKeyRepeatOnce(repeatCount);
    }
}

TEST_F(InputDispatcherKeyRepeatTest, FocusedWindow_ReceivesKeyRepeatFromTwoDevices) {
    sendAndConsumeKeyDown(/*deviceId=*/1);
    for (int32_t repeatCount = 1; repeatCount <= 10; ++repeatCount) {
        expectKeyRepeatOnce(repeatCount);
    }
    sendAndConsumeKeyDown(/*deviceId=*/2);
    /* repeatCount will start from 1 for deviceId 2 */
    for (int32_t repeatCount = 1; repeatCount <= 10; ++repeatCount) {
        expectKeyRepeatOnce(repeatCount);
    }
}

TEST_F(InputDispatcherKeyRepeatTest, FocusedWindow_StopsKeyRepeatAfterUp) {
    sendAndConsumeKeyDown(/*deviceId=*/1);
    expectKeyRepeatOnce(/*repeatCount=*/1);
    sendAndConsumeKeyUp(/*deviceId=*/1);
    mWindow->assertNoEvents();
}

TEST_F(InputDispatcherKeyRepeatTest, FocusedWindow_KeyRepeatAfterStaleDeviceKeyUp) {
    sendAndConsumeKeyDown(/*deviceId=*/1);
    expectKeyRepeatOnce(/*repeatCount=*/1);
    sendAndConsumeKeyDown(/*deviceId=*/2);
    expectKeyRepeatOnce(/*repeatCount=*/1);
    // Stale key up from device 1.
    sendAndConsumeKeyUp(/*deviceId=*/1);
    // Device 2 is still down, keep repeating
    expectKeyRepeatOnce(/*repeatCount=*/2);
    expectKeyRepeatOnce(/*repeatCount=*/3);
    // Device 2 key up
    sendAndConsumeKeyUp(/*deviceId=*/2);
    mWindow->assertNoEvents();
}

TEST_F(InputDispatcherKeyRepeatTest, FocusedWindow_KeyRepeatStopsAfterRepeatingKeyUp) {
    sendAndConsumeKeyDown(/*deviceId=*/1);
    expectKeyRepeatOnce(/*repeatCount=*/1);
    sendAndConsumeKeyDown(/*deviceId=*/2);
    expectKeyRepeatOnce(/*repeatCount=*/1);
    // Device 2 which holds the key repeating goes up, expect the repeating to stop.
    sendAndConsumeKeyUp(/*deviceId=*/2);
    // Device 1 still holds key down, but the repeating was already stopped
    mWindow->assertNoEvents();
}

TEST_F(InputDispatcherKeyRepeatTest, FocusedWindow_StopsKeyRepeatAfterDisableInputDevice) {
    sendAndConsumeKeyDown(DEVICE_ID);
    expectKeyRepeatOnce(/*repeatCount=*/1);
    mDispatcher->notifyDeviceReset({/*id=*/10, /*eventTime=*/20, DEVICE_ID});
    mWindow->consumeKeyUp(ui::LogicalDisplayId::DEFAULT,
                          AKEY_EVENT_FLAG_CANCELED | AKEY_EVENT_FLAG_LONG_PRESS);
    mWindow->assertNoEvents();
}

TEST_F(InputDispatcherKeyRepeatTest, FocusedWindow_RepeatKeyEventsUseEventIdFromInputDispatcher) {
    GTEST_SKIP() << "Flaky test (b/270393106)";
    sendAndConsumeKeyDown(/*deviceId=*/1);
    for (int32_t repeatCount = 1; repeatCount <= 10; ++repeatCount) {
        std::unique_ptr<KeyEvent> repeatEvent = mWindow->consumeKey();
        ASSERT_NE(nullptr, repeatEvent);
        EXPECT_EQ(IdGenerator::Source::INPUT_DISPATCHER,
                  IdGenerator::getSource(repeatEvent->getId()));
    }
}

TEST_F(InputDispatcherKeyRepeatTest, FocusedWindow_RepeatKeyEventsUseUniqueEventId) {
    GTEST_SKIP() << "Flaky test (b/270393106)";
    sendAndConsumeKeyDown(/*deviceId=*/1);

    std::unordered_set<int32_t> idSet;
    for (int32_t repeatCount = 1; repeatCount <= 10; ++repeatCount) {
        std::unique_ptr<KeyEvent> repeatEvent = mWindow->consumeKey();
        ASSERT_NE(nullptr, repeatEvent);
        int32_t id = repeatEvent->getId();
        EXPECT_EQ(idSet.end(), idSet.find(id));
        idSet.insert(id);
    }
}

TEST_F(InputDispatcherKeyRepeatTest, FocusedWindow_CorrectRepeatCountWhenInjectKeyRepeat) {
    injectKeyRepeat(0);
    mWindow->consumeKeyDown(ui::LogicalDisplayId::DEFAULT);
    for (int32_t repeatCount = 1; repeatCount <= 2; ++repeatCount) {
        expectKeyRepeatOnce(repeatCount);
    }
    injectKeyRepeat(1);
    // Expect repeatCount to be 3 instead of 1
    expectKeyRepeatOnce(3);
}

/* Test InputDispatcher for MultiDisplay */
class InputDispatcherFocusOnTwoDisplaysTest : public InputDispatcherTest {
public:
    virtual void SetUp() override {
        InputDispatcherTest::SetUp();

        application1 = std::make_shared<FakeApplicationHandle>();
        windowInPrimary = sp<FakeWindowHandle>::make(application1, mDispatcher, "D_1",
                                                     ui::LogicalDisplayId::DEFAULT);

        // Set focus window for primary display, but focused display would be second one.
        mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application1);
        windowInPrimary->setFocusable(true);
        mDispatcher->onWindowInfosChanged({{*windowInPrimary->getInfo()}, {}, 0, 0});

        setFocusedWindow(windowInPrimary);
        windowInPrimary->consumeFocusEvent(true);

        application2 = std::make_shared<FakeApplicationHandle>();
        windowInSecondary =
                sp<FakeWindowHandle>::make(application2, mDispatcher, "D_2", SECOND_DISPLAY_ID);
        // Set focus to second display window.
        // Set focus display to second one.
        mDispatcher->setFocusedDisplay(SECOND_DISPLAY_ID);
        mFakePolicy->assertFocusedDisplayNotified(SECOND_DISPLAY_ID);

        // Set focus window for second display.
        mDispatcher->setFocusedApplication(SECOND_DISPLAY_ID, application2);
        windowInSecondary->setFocusable(true);
        mDispatcher->onWindowInfosChanged(
                {{*windowInPrimary->getInfo(), *windowInSecondary->getInfo()}, {}, 0, 0});
        setFocusedWindow(windowInSecondary);
        windowInSecondary->consumeFocusEvent(true);
    }

    virtual void TearDown() override {
        InputDispatcherTest::TearDown();

        application1.reset();
        windowInPrimary.clear();
        application2.reset();
        windowInSecondary.clear();
    }

protected:
    std::shared_ptr<FakeApplicationHandle> application1;
    sp<FakeWindowHandle> windowInPrimary;
    std::shared_ptr<FakeApplicationHandle> application2;
    sp<FakeWindowHandle> windowInSecondary;
};

TEST_F(InputDispatcherFocusOnTwoDisplaysTest, SetInputWindow_MultiDisplayTouch) {
    // Test touch down on primary display.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    windowInPrimary->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    windowInSecondary->assertNoEvents();

    // Test touch down on second display.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, SECOND_DISPLAY_ID))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    windowInPrimary->assertNoEvents();
    windowInSecondary->consumeMotionDown(SECOND_DISPLAY_ID);
}

TEST_F(InputDispatcherFocusOnTwoDisplaysTest, SetInputWindow_MultiDisplayFocus) {
    // Test inject a key down with display id specified.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectKeyDownNoRepeat(*mDispatcher, ui::LogicalDisplayId::DEFAULT))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    windowInPrimary->consumeKeyDown(ui::LogicalDisplayId::DEFAULT);
    windowInSecondary->assertNoEvents();

    // Test inject a key down without display id specified.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyDownNoRepeat(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    windowInPrimary->assertNoEvents();
    windowInSecondary->consumeKeyDown(ui::LogicalDisplayId::INVALID);

    // Remove all windows in secondary display.
    mDispatcher->onWindowInfosChanged({{*windowInPrimary->getInfo()}, {}, 0, 0});

    // Old focus should receive a cancel event.
    windowInSecondary->consumeKeyUp(ui::LogicalDisplayId::INVALID, AKEY_EVENT_FLAG_CANCELED);

    // Test inject a key down, should timeout because of no target window.
    ASSERT_NO_FATAL_FAILURE(assertInjectedKeyTimesOut(*mDispatcher));
    windowInPrimary->assertNoEvents();
    windowInSecondary->consumeFocusEvent(false);
    windowInSecondary->assertNoEvents();
}

// Test per-display input monitors for motion event.
TEST_F(InputDispatcherFocusOnTwoDisplaysTest, MonitorMotionEvent_MultiDisplay) {
    FakeMonitorReceiver monitorInPrimary =
            FakeMonitorReceiver(*mDispatcher, "M_1", ui::LogicalDisplayId::DEFAULT);
    FakeMonitorReceiver monitorInSecondary =
            FakeMonitorReceiver(*mDispatcher, "M_2", SECOND_DISPLAY_ID);

    // Test touch down on primary display.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    windowInPrimary->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    monitorInPrimary.consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    windowInSecondary->assertNoEvents();
    monitorInSecondary.assertNoEvents();

    // Test touch down on second display.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, SECOND_DISPLAY_ID))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    windowInPrimary->assertNoEvents();
    monitorInPrimary.assertNoEvents();
    windowInSecondary->consumeMotionDown(SECOND_DISPLAY_ID);
    monitorInSecondary.consumeMotionDown(SECOND_DISPLAY_ID);

    // Lift up the touch from the second display
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionUp(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, SECOND_DISPLAY_ID))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    windowInSecondary->consumeMotionUp(SECOND_DISPLAY_ID);
    monitorInSecondary.consumeMotionUp(SECOND_DISPLAY_ID);

    // Test inject a non-pointer motion event.
    // If specific a display, it will dispatch to the focused window of particular display,
    // or it will dispatch to the focused window of focused display.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TRACKBALL,
                               ui::LogicalDisplayId::INVALID))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    windowInPrimary->assertNoEvents();
    monitorInPrimary.assertNoEvents();
    windowInSecondary->consumeMotionDown(ui::LogicalDisplayId::INVALID);
    monitorInSecondary.consumeMotionDown(ui::LogicalDisplayId::INVALID);
}

// Test per-display input monitors for key event.
TEST_F(InputDispatcherFocusOnTwoDisplaysTest, MonitorKeyEvent_MultiDisplay) {
    // Input monitor per display.
    FakeMonitorReceiver monitorInPrimary =
            FakeMonitorReceiver(*mDispatcher, "M_1", ui::LogicalDisplayId::DEFAULT);
    FakeMonitorReceiver monitorInSecondary =
            FakeMonitorReceiver(*mDispatcher, "M_2", SECOND_DISPLAY_ID);

    // Test inject a key down.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyDown(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    windowInPrimary->assertNoEvents();
    monitorInPrimary.assertNoEvents();
    windowInSecondary->consumeKeyDown(ui::LogicalDisplayId::INVALID);
    monitorInSecondary.consumeKeyDown(ui::LogicalDisplayId::INVALID);
}

TEST_F(InputDispatcherFocusOnTwoDisplaysTest, CanFocusWindowOnUnfocusedDisplay) {
    sp<FakeWindowHandle> secondWindowInPrimary =
            sp<FakeWindowHandle>::make(application1, mDispatcher, "D_1_W2",
                                       ui::LogicalDisplayId::DEFAULT);
    secondWindowInPrimary->setFocusable(true);
    mDispatcher->onWindowInfosChanged(
            {{*windowInPrimary->getInfo(), *secondWindowInPrimary->getInfo(),
              *windowInSecondary->getInfo()},
             {},
             0,
             0});
    setFocusedWindow(secondWindowInPrimary);
    windowInPrimary->consumeFocusEvent(false);
    secondWindowInPrimary->consumeFocusEvent(true);

    // Test inject a key down.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectKeyDown(*mDispatcher, ui::LogicalDisplayId::DEFAULT))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    windowInPrimary->assertNoEvents();
    windowInSecondary->assertNoEvents();
    secondWindowInPrimary->consumeKeyDown(ui::LogicalDisplayId::DEFAULT);
}

TEST_F(InputDispatcherFocusOnTwoDisplaysTest, CancelTouch_MultiDisplay) {
    FakeMonitorReceiver monitorInPrimary =
            FakeMonitorReceiver(*mDispatcher, "M_1", ui::LogicalDisplayId::DEFAULT);
    FakeMonitorReceiver monitorInSecondary =
            FakeMonitorReceiver(*mDispatcher, "M_2", SECOND_DISPLAY_ID);

    // Test touch down on primary display.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    windowInPrimary->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    monitorInPrimary.consumeMotionDown(ui::LogicalDisplayId::DEFAULT);

    // Test touch down on second display.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, SECOND_DISPLAY_ID))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    windowInSecondary->consumeMotionDown(SECOND_DISPLAY_ID);
    monitorInSecondary.consumeMotionDown(SECOND_DISPLAY_ID);

    // Trigger cancel touch.
    mDispatcher->cancelCurrentTouch();
    windowInPrimary->consumeMotionCancel(ui::LogicalDisplayId::DEFAULT);
    monitorInPrimary.consumeMotionCancel(ui::LogicalDisplayId::DEFAULT);
    windowInSecondary->consumeMotionCancel(SECOND_DISPLAY_ID);
    monitorInSecondary.consumeMotionCancel(SECOND_DISPLAY_ID);

    // Test inject a move motion event, no window/monitor should receive the event.
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {110, 200}))
            << "Inject motion event should return InputEventInjectionResult::FAILED";
    windowInPrimary->assertNoEvents();
    monitorInPrimary.assertNoEvents();

    ASSERT_EQ(InputEventInjectionResult::FAILED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                SECOND_DISPLAY_ID, {110, 200}))
            << "Inject motion event should return InputEventInjectionResult::FAILED";
    windowInSecondary->assertNoEvents();
    monitorInSecondary.assertNoEvents();
}

/**
 * Send a key to the primary display and to the secondary display.
 * Then cause the key on the primary display to be canceled by sending in a stale key.
 * Ensure that the key on the primary display is canceled, and that the key on the secondary display
 * does not get canceled.
 */
TEST_F(InputDispatcherFocusOnTwoDisplaysTest, WhenDropKeyEvent_OnlyCancelCorrespondingKeyGesture) {
    // Send a key down on primary display
    mDispatcher->notifyKey(
            KeyArgsBuilder(AKEY_EVENT_ACTION_DOWN, AINPUT_SOURCE_KEYBOARD)
                    .displayId(ui::LogicalDisplayId::DEFAULT)
                    .policyFlags(DEFAULT_POLICY_FLAGS | POLICY_FLAG_DISABLE_KEY_REPEAT)
                    .build());
    windowInPrimary->consumeKeyEvent(AllOf(WithKeyAction(AKEY_EVENT_ACTION_DOWN),
                                           WithDisplayId(ui::LogicalDisplayId::DEFAULT)));
    windowInSecondary->assertNoEvents();

    // Send a key down on second display
    mDispatcher->notifyKey(
            KeyArgsBuilder(AKEY_EVENT_ACTION_DOWN, AINPUT_SOURCE_KEYBOARD)
                    .displayId(SECOND_DISPLAY_ID)
                    .policyFlags(DEFAULT_POLICY_FLAGS | POLICY_FLAG_DISABLE_KEY_REPEAT)
                    .build());
    windowInSecondary->consumeKeyEvent(
            AllOf(WithKeyAction(AKEY_EVENT_ACTION_DOWN), WithDisplayId(SECOND_DISPLAY_ID)));
    windowInPrimary->assertNoEvents();

    // Send a valid key up event on primary display that will be dropped because it is stale
    NotifyKeyArgs staleKeyUp =
            KeyArgsBuilder(AKEY_EVENT_ACTION_UP, AINPUT_SOURCE_KEYBOARD)
                    .displayId(ui::LogicalDisplayId::DEFAULT)
                    .policyFlags(DEFAULT_POLICY_FLAGS | POLICY_FLAG_DISABLE_KEY_REPEAT)
                    .build();
    static constexpr std::chrono::duration STALE_EVENT_TIMEOUT = 10ms;
    mFakePolicy->setStaleEventTimeout(STALE_EVENT_TIMEOUT);
    std::this_thread::sleep_for(STALE_EVENT_TIMEOUT);
    mDispatcher->notifyKey(staleKeyUp);

    // Only the key gesture corresponding to the dropped event should receive the cancel event.
    // Therefore, windowInPrimary should get the cancel event and windowInSecondary should not
    // receive any events.
    windowInPrimary->consumeKeyEvent(AllOf(WithKeyAction(AKEY_EVENT_ACTION_UP),
                                           WithDisplayId(ui::LogicalDisplayId::DEFAULT),
                                           WithFlags(AKEY_EVENT_FLAG_CANCELED)));
    windowInSecondary->assertNoEvents();
}

/**
 * Similar to 'WhenDropKeyEvent_OnlyCancelCorrespondingKeyGesture' but for motion events.
 */
TEST_F(InputDispatcherFocusOnTwoDisplaysTest, WhenDropMotionEvent_OnlyCancelCorrespondingGesture) {
    // Send touch down on primary display.
    mDispatcher->notifyMotion(
            MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(100).y(200))
                    .displayId(ui::LogicalDisplayId::DEFAULT)
                    .build());
    windowInPrimary->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDisplayId(ui::LogicalDisplayId::DEFAULT)));
    windowInSecondary->assertNoEvents();

    // Send touch down on second display.
    mDispatcher->notifyMotion(
            MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(100).y(200))
                    .displayId(SECOND_DISPLAY_ID)
                    .build());
    windowInPrimary->assertNoEvents();
    windowInSecondary->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDisplayId(SECOND_DISPLAY_ID)));

    // inject a valid MotionEvent on primary display that will be stale when it arrives.
    NotifyMotionArgs staleMotionUp =
            MotionArgsBuilder(ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN)
                    .displayId(ui::LogicalDisplayId::DEFAULT)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(100).y(200))
                    .build();
    static constexpr std::chrono::duration STALE_EVENT_TIMEOUT = 10ms;
    mFakePolicy->setStaleEventTimeout(STALE_EVENT_TIMEOUT);
    std::this_thread::sleep_for(STALE_EVENT_TIMEOUT);
    mDispatcher->notifyMotion(staleMotionUp);

    // For stale motion events, we let the gesture to complete. This behaviour is different from key
    // events, where we would cancel the current keys instead.
    windowInPrimary->consumeMotionEvent(WithMotionAction(ACTION_UP));
    windowInSecondary->assertNoEvents();
}

class InputFilterTest : public InputDispatcherTest {
protected:
    void testNotifyMotion(ui::LogicalDisplayId displayId, bool expectToBeFiltered,
                          const ui::Transform& transform = ui::Transform()) {
        NotifyMotionArgs motionArgs;

        motionArgs =
                generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN, displayId);
        mDispatcher->notifyMotion(motionArgs);
        motionArgs =
                generateMotionArgs(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN, displayId);
        mDispatcher->notifyMotion(motionArgs);
        ASSERT_TRUE(mDispatcher->waitForIdle());
        if (expectToBeFiltered) {
            const auto xy = transform.transform(motionArgs.pointerCoords[0].getXYValue());
            mFakePolicy->assertFilterInputEventWasCalled(motionArgs, xy);
        } else {
            mFakePolicy->assertFilterInputEventWasNotCalled();
        }
    }

    void testNotifyKey(bool expectToBeFiltered) {
        NotifyKeyArgs keyArgs;

        keyArgs = generateKeyArgs(AKEY_EVENT_ACTION_DOWN);
        mDispatcher->notifyKey(keyArgs);
        keyArgs = generateKeyArgs(AKEY_EVENT_ACTION_UP);
        mDispatcher->notifyKey(keyArgs);
        ASSERT_TRUE(mDispatcher->waitForIdle());

        if (expectToBeFiltered) {
            mFakePolicy->assertFilterInputEventWasCalled(keyArgs);
        } else {
            mFakePolicy->assertFilterInputEventWasNotCalled();
        }
    }
};

// Test InputFilter for MotionEvent
TEST_F(InputFilterTest, MotionEvent_InputFilter) {
    // Since the InputFilter is disabled by default, check if touch events aren't filtered.
    testNotifyMotion(ui::LogicalDisplayId::DEFAULT, /*expectToBeFiltered=*/false);
    testNotifyMotion(SECOND_DISPLAY_ID, /*expectToBeFiltered=*/false);

    // Enable InputFilter
    mDispatcher->setInputFilterEnabled(true);
    // Test touch on both primary and second display, and check if both events are filtered.
    testNotifyMotion(ui::LogicalDisplayId::DEFAULT, /*expectToBeFiltered=*/true);
    testNotifyMotion(SECOND_DISPLAY_ID, /*expectToBeFiltered=*/true);

    // Disable InputFilter
    mDispatcher->setInputFilterEnabled(false);
    // Test touch on both primary and second display, and check if both events aren't filtered.
    testNotifyMotion(ui::LogicalDisplayId::DEFAULT, /*expectToBeFiltered=*/false);
    testNotifyMotion(SECOND_DISPLAY_ID, /*expectToBeFiltered=*/false);
}

// Test InputFilter for KeyEvent
TEST_F(InputFilterTest, KeyEvent_InputFilter) {
    // Since the InputFilter is disabled by default, check if key event aren't filtered.
    testNotifyKey(/*expectToBeFiltered=*/false);

    // Enable InputFilter
    mDispatcher->setInputFilterEnabled(true);
    // Send a key event, and check if it is filtered.
    testNotifyKey(/*expectToBeFiltered=*/true);

    // Disable InputFilter
    mDispatcher->setInputFilterEnabled(false);
    // Send a key event, and check if it isn't filtered.
    testNotifyKey(/*expectToBeFiltered=*/false);
}

// Ensure that MotionEvents sent to the InputFilter through InputListener are converted to the
// logical display coordinate space.
TEST_F(InputFilterTest, MotionEvent_UsesLogicalDisplayCoordinates_notifyMotion) {
    ui::Transform firstDisplayTransform;
    firstDisplayTransform.set({1.1, 2.2, 3.3, 4.4, 5.5, 6.6, 0, 0, 1});
    ui::Transform secondDisplayTransform;
    secondDisplayTransform.set({-6.6, -5.5, -4.4, -3.3, -2.2, -1.1, 0, 0, 1});

    std::vector<gui::DisplayInfo> displayInfos(2);
    displayInfos[0].displayId = ui::LogicalDisplayId::DEFAULT;
    displayInfos[0].transform = firstDisplayTransform;
    displayInfos[1].displayId = SECOND_DISPLAY_ID;
    displayInfos[1].transform = secondDisplayTransform;

    mDispatcher->onWindowInfosChanged({{}, displayInfos, 0, 0});

    // Enable InputFilter
    mDispatcher->setInputFilterEnabled(true);

    // Ensure the correct transforms are used for the displays.
    testNotifyMotion(ui::LogicalDisplayId::DEFAULT, /*expectToBeFiltered=*/true,
                     firstDisplayTransform);
    testNotifyMotion(SECOND_DISPLAY_ID, /*expectToBeFiltered=*/true, secondDisplayTransform);
}

class InputFilterInjectionPolicyTest : public InputDispatcherTest {
protected:
    virtual void SetUp() override {
        InputDispatcherTest::SetUp();

        /**
         * We don't need to enable input filter to test the injected event policy, but we enabled it
         * here to make the tests more realistic, since this policy only matters when inputfilter is
         * on.
         */
        mDispatcher->setInputFilterEnabled(true);

        std::shared_ptr<InputApplicationHandle> application =
                std::make_shared<FakeApplicationHandle>();
        mWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Test Window",
                                             ui::LogicalDisplayId::DEFAULT);

        mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);
        mWindow->setFocusable(true);
        mDispatcher->onWindowInfosChanged({{*mWindow->getInfo()}, {}, 0, 0});
        setFocusedWindow(mWindow);
        mWindow->consumeFocusEvent(true);
    }

    void testInjectedKey(int32_t policyFlags, int32_t injectedDeviceId, int32_t resolvedDeviceId,
                         int32_t flags) {
        KeyEvent event;

        const nsecs_t eventTime = systemTime(SYSTEM_TIME_MONOTONIC);
        event.initialize(InputEvent::nextId(), injectedDeviceId, AINPUT_SOURCE_KEYBOARD,
                         ui::LogicalDisplayId::INVALID, INVALID_HMAC, AKEY_EVENT_ACTION_DOWN, 0,
                         AKEYCODE_A, KEY_A, AMETA_NONE, /*repeatCount=*/0, eventTime, eventTime);
        const int32_t additionalPolicyFlags =
                POLICY_FLAG_PASS_TO_USER | POLICY_FLAG_DISABLE_KEY_REPEAT;
        ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
                  mDispatcher->injectInputEvent(&event, /*targetUid=*/{},
                                                InputEventInjectionSync::WAIT_FOR_RESULT, 100ms,
                                                policyFlags | additionalPolicyFlags));

        mWindow->consumeKeyEvent(AllOf(WithDeviceId(resolvedDeviceId), WithFlags(flags)));
    }

    void testInjectedMotion(int32_t policyFlags, int32_t injectedDeviceId, int32_t resolvedDeviceId,
                            int32_t flags) {
        MotionEvent event;
        PointerProperties pointerProperties[1];
        PointerCoords pointerCoords[1];
        pointerProperties[0].clear();
        pointerProperties[0].id = 0;
        pointerCoords[0].clear();
        pointerCoords[0].setAxisValue(AMOTION_EVENT_AXIS_X, 300);
        pointerCoords[0].setAxisValue(AMOTION_EVENT_AXIS_Y, 400);

        ui::Transform identityTransform;
        const nsecs_t eventTime = systemTime(SYSTEM_TIME_MONOTONIC);
        event.initialize(InputEvent::nextId(), injectedDeviceId, AINPUT_SOURCE_TOUCHSCREEN,
                         DISPLAY_ID, INVALID_HMAC, AMOTION_EVENT_ACTION_DOWN, 0, 0,
                         AMOTION_EVENT_EDGE_FLAG_NONE, AMETA_NONE, 0, MotionClassification::NONE,
                         identityTransform, 0, 0, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                         AMOTION_EVENT_INVALID_CURSOR_POSITION, identityTransform, eventTime,
                         eventTime,
                         /*pointerCount=*/1, pointerProperties, pointerCoords);

        const int32_t additionalPolicyFlags = POLICY_FLAG_PASS_TO_USER;
        ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
                  mDispatcher->injectInputEvent(&event, /*targetUid=*/{},
                                                InputEventInjectionSync::WAIT_FOR_RESULT, 100ms,
                                                policyFlags | additionalPolicyFlags));

        mWindow->consumeMotionEvent(AllOf(WithFlags(flags), WithDeviceId(resolvedDeviceId)));
    }

private:
    sp<FakeWindowHandle> mWindow;
};

TEST_F(InputFilterInjectionPolicyTest, TrustedFilteredEvents_KeepOriginalDeviceId) {
    // Must have POLICY_FLAG_FILTERED here to indicate that the event has gone through the input
    // filter. Without it, the event will no different from a regularly injected event, and the
    // injected device id will be overwritten.
    testInjectedKey(POLICY_FLAG_FILTERED, /*injectedDeviceId=*/3, /*resolvedDeviceId=*/3,
                    /*flags=*/0);
}

TEST_F(InputFilterInjectionPolicyTest, KeyEventsInjectedFromAccessibility_HaveAccessibilityFlag) {
    testInjectedKey(POLICY_FLAG_FILTERED | POLICY_FLAG_INJECTED_FROM_ACCESSIBILITY,
                    /*injectedDeviceId=*/3, /*resolvedDeviceId=*/3,
                    AKEY_EVENT_FLAG_IS_ACCESSIBILITY_EVENT);
}

TEST_F(InputFilterInjectionPolicyTest,
       MotionEventsInjectedFromAccessibility_HaveAccessibilityFlag) {
    testInjectedMotion(POLICY_FLAG_FILTERED | POLICY_FLAG_INJECTED_FROM_ACCESSIBILITY,
                       /*injectedDeviceId=*/3, /*resolvedDeviceId=*/3,
                       AMOTION_EVENT_FLAG_IS_ACCESSIBILITY_EVENT);
}

TEST_F(InputFilterInjectionPolicyTest, RegularInjectedEvents_ReceiveVirtualDeviceId) {
    testInjectedKey(/*policyFlags=*/0, /*injectedDeviceId=*/3,
                    /*resolvedDeviceId=*/VIRTUAL_KEYBOARD_ID, /*flags=*/0);
}

class InputDispatcherUserActivityPokeTests : public InputDispatcherTest {
protected:
    virtual void SetUp() override {
        InputDispatcherTest::SetUp();

        std::shared_ptr<FakeApplicationHandle> application =
                std::make_shared<FakeApplicationHandle>();
        application->setDispatchingTimeout(100ms);
        mWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "TestWindow",
                                             ui::LogicalDisplayId::DEFAULT);
        mWindow->setFrame(Rect(0, 0, 100, 100));
        mWindow->setDispatchingTimeout(100ms);
        mWindow->setFocusable(true);

        // Set focused application.
        mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);

        mDispatcher->onWindowInfosChanged({{*mWindow->getInfo()}, {}, 0, 0});
        setFocusedWindow(mWindow);
        mWindow->consumeFocusEvent(true);
    }

    void notifyAndConsumeMotion(int32_t action, uint32_t source, ui::LogicalDisplayId displayId,
                                nsecs_t eventTime) {
        mDispatcher->notifyMotion(MotionArgsBuilder(action, source)
                                          .displayId(displayId)
                                          .eventTime(eventTime)
                                          .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                          .build());
        mWindow->consumeMotionEvent(WithMotionAction(action));
    }

private:
    sp<FakeWindowHandle> mWindow;
};

TEST_F_WITH_FLAGS(
        InputDispatcherUserActivityPokeTests, MinPokeTimeObserved,
        REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(com::android::input::flags,
                                            rate_limit_user_activity_poke_in_dispatcher))) {
    mDispatcher->setMinTimeBetweenUserActivityPokes(50ms);

    // First event of type TOUCH. Should poke.
    notifyAndConsumeMotion(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN, ui::LogicalDisplayId::DEFAULT,
                           milliseconds_to_nanoseconds(50));
    mFakePolicy->assertUserActivityPoked(
            {{milliseconds_to_nanoseconds(50), USER_ACTIVITY_EVENT_TOUCH,
              ui::LogicalDisplayId::DEFAULT}});

    // 80ns > 50ns has passed since previous TOUCH event. Should poke.
    notifyAndConsumeMotion(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN, ui::LogicalDisplayId::DEFAULT,
                           milliseconds_to_nanoseconds(130));
    mFakePolicy->assertUserActivityPoked(
            {{milliseconds_to_nanoseconds(130), USER_ACTIVITY_EVENT_TOUCH,
              ui::LogicalDisplayId::DEFAULT}});

    // First event of type OTHER. Should poke (despite being within 50ns of previous TOUCH event).
    notifyAndConsumeMotion(ACTION_SCROLL, AINPUT_SOURCE_ROTARY_ENCODER,
                           ui::LogicalDisplayId::DEFAULT, milliseconds_to_nanoseconds(135));
    mFakePolicy->assertUserActivityPoked(
            {{milliseconds_to_nanoseconds(135), USER_ACTIVITY_EVENT_OTHER,
              ui::LogicalDisplayId::DEFAULT}});

    // Within 50ns of previous TOUCH event. Should NOT poke.
    notifyAndConsumeMotion(ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN, ui::LogicalDisplayId::DEFAULT,
                           milliseconds_to_nanoseconds(140));
    mFakePolicy->assertUserActivityNotPoked();

    // Within 50ns of previous OTHER event. Should NOT poke.
    notifyAndConsumeMotion(ACTION_SCROLL, AINPUT_SOURCE_ROTARY_ENCODER,
                           ui::LogicalDisplayId::DEFAULT, milliseconds_to_nanoseconds(150));
    mFakePolicy->assertUserActivityNotPoked();

    // Within 50ns of previous TOUCH event (which was at time 130). Should NOT poke.
    // Note that STYLUS is mapped to TOUCH user activity, since it's a pointer-type source.
    notifyAndConsumeMotion(ACTION_DOWN, AINPUT_SOURCE_STYLUS, ui::LogicalDisplayId::DEFAULT,
                           milliseconds_to_nanoseconds(160));
    mFakePolicy->assertUserActivityNotPoked();

    // 65ns > 50ns has passed since previous OTHER event. Should poke.
    notifyAndConsumeMotion(ACTION_SCROLL, AINPUT_SOURCE_ROTARY_ENCODER,
                           ui::LogicalDisplayId::DEFAULT, milliseconds_to_nanoseconds(200));
    mFakePolicy->assertUserActivityPoked(
            {{milliseconds_to_nanoseconds(200), USER_ACTIVITY_EVENT_OTHER,
              ui::LogicalDisplayId::DEFAULT}});

    // 170ns > 50ns has passed since previous TOUCH event. Should poke.
    notifyAndConsumeMotion(ACTION_UP, AINPUT_SOURCE_STYLUS, ui::LogicalDisplayId::DEFAULT,
                           milliseconds_to_nanoseconds(300));
    mFakePolicy->assertUserActivityPoked(
            {{milliseconds_to_nanoseconds(300), USER_ACTIVITY_EVENT_TOUCH,
              ui::LogicalDisplayId::DEFAULT}});

    // Assert that there's no more user activity poke event.
    mFakePolicy->assertUserActivityNotPoked();
}

TEST_F_WITH_FLAGS(
        InputDispatcherUserActivityPokeTests, DefaultMinPokeTimeOf100MsUsed,
        REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(com::android::input::flags,
                                            rate_limit_user_activity_poke_in_dispatcher))) {
    notifyAndConsumeMotion(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN, ui::LogicalDisplayId::DEFAULT,
                           milliseconds_to_nanoseconds(200));
    mFakePolicy->assertUserActivityPoked(
            {{milliseconds_to_nanoseconds(200), USER_ACTIVITY_EVENT_TOUCH,
              ui::LogicalDisplayId::DEFAULT}});

    notifyAndConsumeMotion(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN, ui::LogicalDisplayId::DEFAULT,
                           milliseconds_to_nanoseconds(280));
    mFakePolicy->assertUserActivityNotPoked();

    notifyAndConsumeMotion(ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN, ui::LogicalDisplayId::DEFAULT,
                           milliseconds_to_nanoseconds(340));
    mFakePolicy->assertUserActivityPoked(
            {{milliseconds_to_nanoseconds(340), USER_ACTIVITY_EVENT_TOUCH,
              ui::LogicalDisplayId::DEFAULT}});
}

TEST_F_WITH_FLAGS(
        InputDispatcherUserActivityPokeTests, ZeroMinPokeTimeDisablesRateLimiting,
        REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(com::android::input::flags,
                                            rate_limit_user_activity_poke_in_dispatcher))) {
    mDispatcher->setMinTimeBetweenUserActivityPokes(0ms);

    notifyAndConsumeMotion(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN, ui::LogicalDisplayId::DEFAULT,
                           20);
    mFakePolicy->assertUserActivityPoked();

    notifyAndConsumeMotion(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN, ui::LogicalDisplayId::DEFAULT,
                           30);
    mFakePolicy->assertUserActivityPoked();
}

class InputDispatcherOnPointerDownOutsideFocus : public InputDispatcherTest {
    virtual void SetUp() override {
        InputDispatcherTest::SetUp();

        std::shared_ptr<FakeApplicationHandle> application =
                std::make_shared<FakeApplicationHandle>();
        mUnfocusedWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Top",
                                                      ui::LogicalDisplayId::DEFAULT);
        mUnfocusedWindow->setFrame(Rect(0, 0, 30, 30));

        mFocusedWindow = sp<FakeWindowHandle>::make(application, mDispatcher, "Second",
                                                    ui::LogicalDisplayId::DEFAULT);
        mFocusedWindow->setFrame(Rect(50, 50, 100, 100));

        // Set focused application.
        mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);
        mFocusedWindow->setFocusable(true);

        // Expect one focus window exist in display.
        mDispatcher->onWindowInfosChanged(
                {{*mUnfocusedWindow->getInfo(), *mFocusedWindow->getInfo()}, {}, 0, 0});
        setFocusedWindow(mFocusedWindow);
        mFocusedWindow->consumeFocusEvent(true);
    }

    virtual void TearDown() override {
        InputDispatcherTest::TearDown();

        mUnfocusedWindow.clear();
        mFocusedWindow.clear();
    }

protected:
    sp<FakeWindowHandle> mUnfocusedWindow;
    sp<FakeWindowHandle> mFocusedWindow;
    static constexpr PointF FOCUSED_WINDOW_TOUCH_POINT = {60, 60};
};

// Have two windows, one with focus. Inject MotionEvent with source TOUCHSCREEN and action
// DOWN on the window that doesn't have focus. Ensure the window that didn't have focus received
// the onPointerDownOutsideFocus callback.
TEST_F(InputDispatcherOnPointerDownOutsideFocus, OnPointerDownOutsideFocus_Success) {
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {20, 20}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mUnfocusedWindow->consumeMotionDown();

    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertOnPointerDownEquals(mUnfocusedWindow->getToken());
}

// Have two windows, one with focus. Inject MotionEvent with source TRACKBALL and action
// DOWN on the window that doesn't have focus. Ensure no window received the
// onPointerDownOutsideFocus callback.
TEST_F(InputDispatcherOnPointerDownOutsideFocus, OnPointerDownOutsideFocus_NonPointerSource) {
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TRACKBALL, ui::LogicalDisplayId::DEFAULT,
                               {20, 20}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mFocusedWindow->consumeMotionDown();

    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertOnPointerDownWasNotCalled();
}

// Have two windows, one with focus. Inject KeyEvent with action DOWN on the window that doesn't
// have focus. Ensure no window received the onPointerDownOutsideFocus callback.
TEST_F(InputDispatcherOnPointerDownOutsideFocus, OnPointerDownOutsideFocus_NonMotionFailure) {
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectKeyDownNoRepeat(*mDispatcher, ui::LogicalDisplayId::DEFAULT))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    mFocusedWindow->consumeKeyDown(ui::LogicalDisplayId::DEFAULT);

    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertOnPointerDownWasNotCalled();
}

// Have two windows, one with focus. Inject MotionEvent with source TOUCHSCREEN and action
// DOWN on the window that already has focus. Ensure no window received the
// onPointerDownOutsideFocus callback.
TEST_F(InputDispatcherOnPointerDownOutsideFocus, OnPointerDownOutsideFocus_OnAlreadyFocusedWindow) {
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, FOCUSED_WINDOW_TOUCH_POINT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mFocusedWindow->consumeMotionDown();

    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertOnPointerDownWasNotCalled();
}

// Have two windows, one with focus. Injecting a trusted DOWN MotionEvent with the flag
// NO_FOCUS_CHANGE on the unfocused window should not call the onPointerDownOutsideFocus callback.
TEST_F(InputDispatcherOnPointerDownOutsideFocus, NoFocusChangeFlag) {
    const MotionEvent event =
            MotionEventBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_MOUSE)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(20).y(20))
                    .addFlag(AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE)
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectMotionEvent(*mDispatcher, event))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mUnfocusedWindow->consumeAnyMotionDown(ui::LogicalDisplayId::DEFAULT,
                                           AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);

    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertOnPointerDownWasNotCalled();
    // Ensure that the unfocused window did not receive any FOCUS events.
    mUnfocusedWindow->assertNoEvents();
}

// These tests ensures we can send touch events to a single client when there are multiple input
// windows that point to the same client token.
class InputDispatcherMultiWindowSameTokenTests : public InputDispatcherTest {
    virtual void SetUp() override {
        InputDispatcherTest::SetUp();

        std::shared_ptr<FakeApplicationHandle> application =
                std::make_shared<FakeApplicationHandle>();
        mWindow1 = sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window 1",
                                              ui::LogicalDisplayId::DEFAULT);
        mWindow1->setFrame(Rect(0, 0, 100, 100));

        mWindow2 = mWindow1->clone(ui::LogicalDisplayId::DEFAULT);
        mWindow2->setFrame(Rect(100, 100, 200, 200));

        mDispatcher->onWindowInfosChanged({{*mWindow1->getInfo(), *mWindow2->getInfo()}, {}, 0, 0});
    }

protected:
    sp<FakeWindowHandle> mWindow1;
    sp<FakeWindowHandle> mWindow2;

    // Helper function to convert the point from screen coordinates into the window's space
    static PointF getPointInWindow(const WindowInfo* windowInfo, const PointF& point) {
        vec2 vals = windowInfo->transform.transform(point.x, point.y);
        return {vals.x, vals.y};
    }

    void consumeMotionEvent(const sp<FakeWindowHandle>& window, int32_t expectedAction,
                            const std::vector<PointF>& points) {
        const std::string name = window->getName();
        std::unique_ptr<MotionEvent> motionEvent =
                window->consumeMotionEvent(WithMotionAction(expectedAction));
        ASSERT_NE(nullptr, motionEvent);
        ASSERT_EQ(points.size(), motionEvent->getPointerCount());

        for (size_t i = 0; i < points.size(); i++) {
            float expectedX = points[i].x;
            float expectedY = points[i].y;

            EXPECT_EQ(expectedX, motionEvent->getX(i))
                    << "expected " << expectedX << " for x[" << i << "] coord of " << name.c_str()
                    << ", got " << motionEvent->getX(i);
            EXPECT_EQ(expectedY, motionEvent->getY(i))
                    << "expected " << expectedY << " for y[" << i << "] coord of " << name.c_str()
                    << ", got " << motionEvent->getY(i);
        }
    }

    void touchAndAssertPositions(sp<FakeWindowHandle> touchedWindow, int32_t action,
                                 const std::vector<PointF>& touchedPoints,
                                 std::vector<PointF> expectedPoints) {
        mDispatcher->notifyMotion(generateMotionArgs(action, AINPUT_SOURCE_TOUCHSCREEN,
                                                     ui::LogicalDisplayId::DEFAULT, touchedPoints));

        consumeMotionEvent(touchedWindow, action, expectedPoints);
    }
};

TEST_F(InputDispatcherMultiWindowSameTokenTests, SingleTouchSameScale) {
    // Touch Window 1
    PointF touchedPoint = {10, 10};
    PointF expectedPoint = getPointInWindow(mWindow1->getInfo(), touchedPoint);
    touchAndAssertPositions(mWindow1, AMOTION_EVENT_ACTION_DOWN, {touchedPoint}, {expectedPoint});

    // Release touch on Window 1
    touchAndAssertPositions(mWindow1, AMOTION_EVENT_ACTION_UP, {touchedPoint}, {expectedPoint});

    // Touch Window 2
    touchedPoint = {150, 150};
    expectedPoint = getPointInWindow(mWindow2->getInfo(), touchedPoint);
    touchAndAssertPositions(mWindow2, AMOTION_EVENT_ACTION_DOWN, {touchedPoint}, {expectedPoint});
}

TEST_F(InputDispatcherMultiWindowSameTokenTests, SingleTouchDifferentTransform) {
    // Set scale value for window2
    mWindow2->setWindowScale(0.5f, 0.5f);
    mDispatcher->onWindowInfosChanged({{*mWindow1->getInfo(), *mWindow2->getInfo()}, {}, 0, 0});

    // Touch Window 1
    PointF touchedPoint = {10, 10};
    PointF expectedPoint = getPointInWindow(mWindow1->getInfo(), touchedPoint);
    touchAndAssertPositions(mWindow1, AMOTION_EVENT_ACTION_DOWN, {touchedPoint}, {expectedPoint});
    // Release touch on Window 1
    touchAndAssertPositions(mWindow1, AMOTION_EVENT_ACTION_UP, {touchedPoint}, {expectedPoint});

    // Touch Window 2
    touchedPoint = {150, 150};
    expectedPoint = getPointInWindow(mWindow2->getInfo(), touchedPoint);
    touchAndAssertPositions(mWindow2, AMOTION_EVENT_ACTION_DOWN, {touchedPoint}, {expectedPoint});
    touchAndAssertPositions(mWindow2, AMOTION_EVENT_ACTION_UP, {touchedPoint}, {expectedPoint});

    // Update the transform so rotation is set
    mWindow2->setWindowTransform(0, -1, 1, 0);
    mDispatcher->onWindowInfosChanged({{*mWindow1->getInfo(), *mWindow2->getInfo()}, {}, 0, 0});
    expectedPoint = getPointInWindow(mWindow2->getInfo(), touchedPoint);
    touchAndAssertPositions(mWindow2, AMOTION_EVENT_ACTION_DOWN, {touchedPoint}, {expectedPoint});
}

TEST_F(InputDispatcherMultiWindowSameTokenTests, MultipleTouchDifferentTransform) {
    mWindow2->setWindowScale(0.5f, 0.5f);
    mDispatcher->onWindowInfosChanged({{*mWindow1->getInfo(), *mWindow2->getInfo()}, {}, 0, 0});

    // Touch Window 1
    std::vector<PointF> touchedPoints = {PointF{10, 10}};
    std::vector<PointF> expectedPoints = {getPointInWindow(mWindow1->getInfo(), touchedPoints[0])};
    touchAndAssertPositions(mWindow1, AMOTION_EVENT_ACTION_DOWN, touchedPoints, expectedPoints);

    // Touch Window 2
    // Since this is part of the same touch gesture that has already been dispatched to Window 1,
    // the touch stream from Window 2 will be merged with the stream in Window 1. The merged stream
    // will continue to be dispatched through Window 1.
    touchedPoints.push_back(PointF{150, 150});
    expectedPoints.push_back(getPointInWindow(mWindow2->getInfo(), touchedPoints[1]));
    touchAndAssertPositions(mWindow1, POINTER_1_DOWN, touchedPoints, expectedPoints);

    // Release Window 2
    touchAndAssertPositions(mWindow1, POINTER_1_UP, touchedPoints, expectedPoints);
    expectedPoints.pop_back();

    // Update the transform so rotation is set for Window 2
    mWindow2->setWindowTransform(0, -1, 1, 0);
    mDispatcher->onWindowInfosChanged({{*mWindow1->getInfo(), *mWindow2->getInfo()}, {}, 0, 0});
    expectedPoints.push_back(getPointInWindow(mWindow2->getInfo(), touchedPoints[1]));
    touchAndAssertPositions(mWindow1, POINTER_1_DOWN, touchedPoints, expectedPoints);
}

TEST_F(InputDispatcherMultiWindowSameTokenTests, MultipleTouchMoveDifferentTransform) {
    mWindow2->setWindowScale(0.5f, 0.5f);
    mDispatcher->onWindowInfosChanged({{*mWindow1->getInfo(), *mWindow2->getInfo()}, {}, 0, 0});

    // Touch Window 1
    std::vector<PointF> touchedPoints = {PointF{10, 10}};
    std::vector<PointF> expectedPoints = {getPointInWindow(mWindow1->getInfo(), touchedPoints[0])};
    touchAndAssertPositions(mWindow1, AMOTION_EVENT_ACTION_DOWN, touchedPoints, expectedPoints);

    // Touch Window 2
    touchedPoints.push_back(PointF{150, 150});
    expectedPoints.push_back(getPointInWindow(mWindow2->getInfo(), touchedPoints[1]));

    touchAndAssertPositions(mWindow1, POINTER_1_DOWN, touchedPoints, expectedPoints);

    // Move both windows
    touchedPoints = {{20, 20}, {175, 175}};
    expectedPoints = {getPointInWindow(mWindow1->getInfo(), touchedPoints[0]),
                      getPointInWindow(mWindow2->getInfo(), touchedPoints[1])};

    touchAndAssertPositions(mWindow1, AMOTION_EVENT_ACTION_MOVE, touchedPoints, expectedPoints);

    // Release Window 2
    touchAndAssertPositions(mWindow1, POINTER_1_UP, touchedPoints, expectedPoints);
    expectedPoints.pop_back();

    // Touch Window 2
    mWindow2->setWindowTransform(0, -1, 1, 0);
    mDispatcher->onWindowInfosChanged({{*mWindow1->getInfo(), *mWindow2->getInfo()}, {}, 0, 0});
    expectedPoints.push_back(getPointInWindow(mWindow2->getInfo(), touchedPoints[1]));
    touchAndAssertPositions(mWindow1, POINTER_1_DOWN, touchedPoints, expectedPoints);

    // Move both windows
    touchedPoints = {{20, 20}, {175, 175}};
    expectedPoints = {getPointInWindow(mWindow1->getInfo(), touchedPoints[0]),
                      getPointInWindow(mWindow2->getInfo(), touchedPoints[1])};

    touchAndAssertPositions(mWindow1, AMOTION_EVENT_ACTION_MOVE, touchedPoints, expectedPoints);
}

TEST_F(InputDispatcherMultiWindowSameTokenTests, MultipleWindowsFirstTouchWithScale) {
    mWindow1->setWindowScale(0.5f, 0.5f);
    mDispatcher->onWindowInfosChanged({{*mWindow1->getInfo(), *mWindow2->getInfo()}, {}, 0, 0});

    // Touch Window 1
    std::vector<PointF> touchedPoints = {PointF{10, 10}};
    std::vector<PointF> expectedPoints = {getPointInWindow(mWindow1->getInfo(), touchedPoints[0])};
    touchAndAssertPositions(mWindow1, AMOTION_EVENT_ACTION_DOWN, touchedPoints, expectedPoints);

    // Touch Window 2
    touchedPoints.push_back(PointF{150, 150});
    expectedPoints.push_back(getPointInWindow(mWindow2->getInfo(), touchedPoints[1]));

    touchAndAssertPositions(mWindow1, POINTER_1_DOWN, touchedPoints, expectedPoints);

    // Move both windows
    touchedPoints = {{20, 20}, {175, 175}};
    expectedPoints = {getPointInWindow(mWindow1->getInfo(), touchedPoints[0]),
                      getPointInWindow(mWindow2->getInfo(), touchedPoints[1])};

    touchAndAssertPositions(mWindow1, AMOTION_EVENT_ACTION_MOVE, touchedPoints, expectedPoints);
}

/**
 * When one of the windows is slippery, the touch should not slip into the other window with the
 * same input channel.
 */
TEST_F(InputDispatcherMultiWindowSameTokenTests, TouchDoesNotSlipEvenIfSlippery) {
    mWindow1->setSlippery(true);
    mDispatcher->onWindowInfosChanged({{*mWindow1->getInfo(), *mWindow2->getInfo()}, {}, 0, 0});

    // Touch down in window 1
    mDispatcher->notifyMotion(generateMotionArgs(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT, {{50, 50}}));
    consumeMotionEvent(mWindow1, ACTION_DOWN, {{50, 50}});

    // Move touch to be above window 2. Even though window 1 is slippery, touch should not slip.
    // That means the gesture should continue normally, without any ACTION_CANCEL or ACTION_DOWN
    // getting generated.
    mDispatcher->notifyMotion(generateMotionArgs(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT, {{150, 150}}));

    consumeMotionEvent(mWindow1, ACTION_MOVE, {{150, 150}});
}

/**
 * When hover starts in one window and continues into the other, there should be a HOVER_EXIT and
 * a HOVER_ENTER generated, even if the windows have the same token. This is because the new window
 * that the pointer is hovering over may have a different transform.
 */
TEST_F(InputDispatcherMultiWindowSameTokenTests, HoverIntoClone) {
    mDispatcher->onWindowInfosChanged({{*mWindow1->getInfo(), *mWindow2->getInfo()}, {}, 0, 0});

    // Start hover in window 1
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .build());
    consumeMotionEvent(mWindow1, ACTION_HOVER_ENTER,
                       {getPointInWindow(mWindow1->getInfo(), PointF{50, 50})});
    // Move hover to window 2.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(150).y(150))
                                      .build());
    consumeMotionEvent(mWindow1, ACTION_HOVER_EXIT, {{50, 50}});
    consumeMotionEvent(mWindow2, ACTION_HOVER_ENTER,
                       {getPointInWindow(mWindow2->getInfo(), PointF{150, 150})});
}

class InputDispatcherSingleWindowAnr : public InputDispatcherTest {
    virtual void SetUp() override {
        InputDispatcherTest::SetUp();

        mApplication = std::make_shared<FakeApplicationHandle>();
        mApplication->setDispatchingTimeout(100ms);
        mWindow = sp<FakeWindowHandle>::make(mApplication, mDispatcher, "TestWindow",
                                             ui::LogicalDisplayId::DEFAULT);
        mWindow->setFrame(Rect(0, 0, 30, 30));
        mWindow->setDispatchingTimeout(100ms);
        mWindow->setFocusable(true);

        // Set focused application.
        mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, mApplication);

        mDispatcher->onWindowInfosChanged({{*mWindow->getInfo()}, {}, 0, 0});
        setFocusedWindow(mWindow);
        mWindow->consumeFocusEvent(true);
    }

    virtual void TearDown() override {
        InputDispatcherTest::TearDown();
        mWindow.clear();
    }

protected:
    static constexpr std::chrono::duration SPY_TIMEOUT = 200ms;
    std::shared_ptr<FakeApplicationHandle> mApplication;
    sp<FakeWindowHandle> mWindow;
    static constexpr PointF WINDOW_LOCATION = {20, 20};

    void tapOnWindow() {
        const auto touchingPointer = PointerBuilder(/*id=*/0, ToolType::FINGER)
                                             .x(WINDOW_LOCATION.x)
                                             .y(WINDOW_LOCATION.y);
        mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                          .pointer(touchingPointer)
                                          .build());
        mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                          .pointer(touchingPointer)
                                          .build());
    }

    sp<FakeWindowHandle> addSpyWindow() {
        sp<FakeWindowHandle> spy = sp<FakeWindowHandle>::make(mApplication, mDispatcher, "Spy",
                                                              ui::LogicalDisplayId::DEFAULT);
        spy->setTrustedOverlay(true);
        spy->setFocusable(false);
        spy->setSpy(true);
        spy->setDispatchingTimeout(SPY_TIMEOUT);
        mDispatcher->onWindowInfosChanged({{*spy->getInfo(), *mWindow->getInfo()}, {}, 0, 0});
        return spy;
    }
};

// Send a tap and respond, which should not cause an ANR.
TEST_F(InputDispatcherSingleWindowAnr, WhenTouchIsConsumed_NoAnr) {
    tapOnWindow();
    mWindow->consumeMotionDown();
    mWindow->consumeMotionUp();
    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertNotifyAnrWasNotCalled();
}

// Send a regular key and respond, which should not cause an ANR.
TEST_F(InputDispatcherSingleWindowAnr, WhenKeyIsConsumed_NoAnr) {
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyDownNoRepeat(*mDispatcher));
    mWindow->consumeKeyDown(ui::LogicalDisplayId::INVALID);
    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertNotifyAnrWasNotCalled();
}

TEST_F(InputDispatcherSingleWindowAnr, WhenFocusedApplicationChanges_NoAnr) {
    mWindow->setFocusable(false);
    mDispatcher->onWindowInfosChanged({{*mWindow->getInfo()}, {}, 0, 0});
    mWindow->consumeFocusEvent(false);

    InputEventInjectionResult result =
            injectKey(*mDispatcher, AKEY_EVENT_ACTION_DOWN, /*repeatCount=*/0,
                      ui::LogicalDisplayId::DEFAULT, InputEventInjectionSync::NONE,
                      CONSUME_TIMEOUT_EVENT_EXPECTED,
                      /*allowKeyRepeat=*/false);
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, result);
    // Key will not go to window because we have no focused window.
    // The 'no focused window' ANR timer should start instead.

    // Now, the focused application goes away.
    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, nullptr);
    // The key should get dropped and there should be no ANR.

    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertNotifyAnrWasNotCalled();
}

// Send an event to the app and have the app not respond right away.
// When ANR is raised, policy will tell the dispatcher to cancel the events for that window.
// So InputDispatcher will enqueue ACTION_CANCEL event as well.
TEST_F(InputDispatcherSingleWindowAnr, OnPointerDown_BasicAnr) {
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, WINDOW_LOCATION));

    const auto [sequenceNum, _] = mWindow->receiveEvent(); // ACTION_DOWN
    ASSERT_TRUE(sequenceNum);
    const std::chrono::duration timeout = mWindow->getDispatchingTimeout(DISPATCHING_TIMEOUT);
    mFakePolicy->assertNotifyWindowUnresponsiveWasCalled(timeout, mWindow);

    mWindow->finishEvent(*sequenceNum);
    mWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDisplayId(ui::LogicalDisplayId::DEFAULT)));
    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertNotifyWindowResponsiveWasCalled(mWindow->getToken(), mWindow->getPid());
}

// Send a key to the app and have the app not respond right away.
TEST_F(InputDispatcherSingleWindowAnr, OnKeyDown_BasicAnr) {
    // Inject a key, and don't respond - expect that ANR is called.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyDownNoRepeat(*mDispatcher));
    const auto [sequenceNum, _] = mWindow->receiveEvent();
    ASSERT_TRUE(sequenceNum);
    const std::chrono::duration timeout = mWindow->getDispatchingTimeout(DISPATCHING_TIMEOUT);
    mFakePolicy->assertNotifyWindowUnresponsiveWasCalled(timeout, mWindow);
    ASSERT_TRUE(mDispatcher->waitForIdle());
}

// We have a focused application, but no focused window
TEST_F(InputDispatcherSingleWindowAnr, FocusedApplication_NoFocusedWindow) {
    mWindow->setFocusable(false);
    mDispatcher->onWindowInfosChanged({{*mWindow->getInfo()}, {}, 0, 0});
    mWindow->consumeFocusEvent(false);

    // taps on the window work as normal
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, WINDOW_LOCATION));
    ASSERT_NO_FATAL_FAILURE(mWindow->consumeMotionDown());
    mDispatcher->waitForIdle();
    mFakePolicy->assertNotifyAnrWasNotCalled();

    // Once a focused event arrives, we get an ANR for this application
    // We specify the injection timeout to be smaller than the application timeout, to ensure that
    // injection times out (instead of failing).
    const InputEventInjectionResult result =
            injectKey(*mDispatcher, AKEY_EVENT_ACTION_DOWN, /*repeatCount=*/0,
                      ui::LogicalDisplayId::DEFAULT, InputEventInjectionSync::WAIT_FOR_RESULT, 50ms,
                      /*allowKeyRepeat=*/false);
    ASSERT_EQ(InputEventInjectionResult::TIMED_OUT, result);
    const std::chrono::duration timeout = mApplication->getDispatchingTimeout(DISPATCHING_TIMEOUT);
    mFakePolicy->assertNotifyNoFocusedWindowAnrWasCalled(timeout, mApplication);
    ASSERT_TRUE(mDispatcher->waitForIdle());
}

/**
 * Make sure the stale key is dropped before causing an ANR. So even if there's no focused window,
 * there will not be an ANR.
 */
TEST_F(InputDispatcherSingleWindowAnr, StaleKeyEventDoesNotAnr) {
    mWindow->setFocusable(false);
    mDispatcher->onWindowInfosChanged({{*mWindow->getInfo()}, {}, 0, 0});
    mWindow->consumeFocusEvent(false);

    KeyEvent event;
    static constexpr std::chrono::duration STALE_EVENT_TIMEOUT = 1000ms;
    mFakePolicy->setStaleEventTimeout(STALE_EVENT_TIMEOUT);
    const nsecs_t eventTime = systemTime(SYSTEM_TIME_MONOTONIC) -
            std::chrono::nanoseconds(STALE_EVENT_TIMEOUT).count();

    // Define a valid key down event that is stale (too old).
    event.initialize(InputEvent::nextId(), DEVICE_ID, AINPUT_SOURCE_KEYBOARD,
                     ui::LogicalDisplayId::INVALID, INVALID_HMAC, AKEY_EVENT_ACTION_DOWN,
                     /*flags=*/0, AKEYCODE_A, KEY_A, AMETA_NONE, /*repeatCount=*/0, eventTime,
                     eventTime);

    const int32_t policyFlags =
            POLICY_FLAG_FILTERED | POLICY_FLAG_PASS_TO_USER | POLICY_FLAG_DISABLE_KEY_REPEAT;

    InputEventInjectionResult result =
            mDispatcher->injectInputEvent(&event, /*targetUid=*/{},
                                          InputEventInjectionSync::WAIT_FOR_RESULT,
                                          INJECT_EVENT_TIMEOUT, policyFlags);
    ASSERT_EQ(InputEventInjectionResult::FAILED, result)
            << "Injection should fail because the event is stale";

    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertNotifyAnrWasNotCalled();
    mWindow->assertNoEvents();
}

// We have a focused application, but no focused window
// Make sure that we don't notify policy twice about the same ANR.
TEST_F(InputDispatcherSingleWindowAnr, NoFocusedWindow_DoesNotSendDuplicateAnr) {
    const std::chrono::duration appTimeout = 400ms;
    mApplication->setDispatchingTimeout(appTimeout);
    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, mApplication);

    mWindow->setFocusable(false);
    mDispatcher->onWindowInfosChanged({{*mWindow->getInfo()}, {}, 0, 0});
    mWindow->consumeFocusEvent(false);

    // Once a focused event arrives, we get an ANR for this application
    // We specify the injection timeout to be smaller than the application timeout, to ensure that
    // injection times out (instead of failing).
    const std::chrono::duration eventInjectionTimeout = 100ms;
    ASSERT_LT(eventInjectionTimeout, appTimeout);
    const InputEventInjectionResult result =
            injectKey(*mDispatcher, AKEY_EVENT_ACTION_DOWN, /*repeatCount=*/0,
                      ui::LogicalDisplayId::DEFAULT, InputEventInjectionSync::WAIT_FOR_RESULT,
                      eventInjectionTimeout,
                      /*allowKeyRepeat=*/false);
    ASSERT_EQ(InputEventInjectionResult::TIMED_OUT, result)
            << "result=" << ftl::enum_string(result);
    // We already waited for 'eventInjectionTimeout`, because the countdown started when the event
    // was first injected. So now we have (appTimeout - eventInjectionTimeout) left to wait.
    std::chrono::duration remainingWaitTime = appTimeout - eventInjectionTimeout;
    mFakePolicy->assertNotifyNoFocusedWindowAnrWasCalled(remainingWaitTime, mApplication);

    std::this_thread::sleep_for(appTimeout);
    // ANR should not be raised again. It is up to policy to do that if it desires.
    mFakePolicy->assertNotifyAnrWasNotCalled();

    // If we now get a focused window, the ANR should stop, but the policy handles that via
    // 'notifyFocusChanged' callback. This is implemented in the policy so we can't test it here.
    ASSERT_TRUE(mDispatcher->waitForIdle());
}

// We have a focused application, but no focused window
TEST_F(InputDispatcherSingleWindowAnr, NoFocusedWindow_DropsFocusedEvents) {
    mWindow->setFocusable(false);
    mDispatcher->onWindowInfosChanged({{*mWindow->getInfo()}, {}, 0, 0});
    mWindow->consumeFocusEvent(false);

    // Once a focused event arrives, we get an ANR for this application
    ASSERT_NO_FATAL_FAILURE(assertInjectedKeyTimesOut(*mDispatcher));

    const std::chrono::duration timeout = mApplication->getDispatchingTimeout(DISPATCHING_TIMEOUT);
    mFakePolicy->assertNotifyNoFocusedWindowAnrWasCalled(timeout, mApplication);

    // Future focused events get dropped right away
    ASSERT_EQ(InputEventInjectionResult::FAILED, injectKeyDown(*mDispatcher));
    ASSERT_TRUE(mDispatcher->waitForIdle());
    mWindow->assertNoEvents();
}

/**
 * Ensure that the implementation is valid. Since we are using multiset to keep track of the
 * ANR timeouts, we are allowing entries with identical timestamps in the same connection.
 * If we process 1 of the events, but ANR on the second event with the same timestamp,
 * the ANR mechanism should still work.
 *
 * In this test, we are injecting DOWN and UP events with the same timestamps, and acknowledging the
 * DOWN event, while not responding on the second one.
 */
TEST_F(InputDispatcherSingleWindowAnr, Anr_HandlesEventsWithIdenticalTimestamps) {
    nsecs_t currentTime = systemTime(SYSTEM_TIME_MONOTONIC);
    injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                      ui::LogicalDisplayId::DEFAULT, WINDOW_LOCATION,
                      {AMOTION_EVENT_INVALID_CURSOR_POSITION,
                       AMOTION_EVENT_INVALID_CURSOR_POSITION},
                      500ms, InputEventInjectionSync::WAIT_FOR_RESULT, currentTime);

    // Now send ACTION_UP, with identical timestamp
    injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN,
                      ui::LogicalDisplayId::DEFAULT, WINDOW_LOCATION,
                      {AMOTION_EVENT_INVALID_CURSOR_POSITION,
                       AMOTION_EVENT_INVALID_CURSOR_POSITION},
                      500ms, InputEventInjectionSync::WAIT_FOR_RESULT, currentTime);

    // We have now sent down and up. Let's consume first event and then ANR on the second.
    mWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    const std::chrono::duration timeout = mWindow->getDispatchingTimeout(DISPATCHING_TIMEOUT);
    mFakePolicy->assertNotifyWindowUnresponsiveWasCalled(timeout, mWindow);
}

// A spy window can receive an ANR
TEST_F(InputDispatcherSingleWindowAnr, SpyWindowAnr) {
    sp<FakeWindowHandle> spy = addSpyWindow();

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, WINDOW_LOCATION));
    mWindow->consumeMotionDown();

    const auto [sequenceNum, _] = spy->receiveEvent(); // ACTION_DOWN
    ASSERT_TRUE(sequenceNum);
    const std::chrono::duration timeout = spy->getDispatchingTimeout(DISPATCHING_TIMEOUT);
    mFakePolicy->assertNotifyWindowUnresponsiveWasCalled(timeout, spy);

    spy->finishEvent(*sequenceNum);
    spy->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDisplayId(ui::LogicalDisplayId::DEFAULT)));
    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertNotifyWindowResponsiveWasCalled(spy->getToken(), mWindow->getPid());
}

// If an app is not responding to a key event, spy windows should continue to receive
// new motion events
TEST_F(InputDispatcherSingleWindowAnr, SpyWindowReceivesEventsDuringAppAnrOnKey) {
    sp<FakeWindowHandle> spy = addSpyWindow();

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectKeyDown(*mDispatcher, ui::LogicalDisplayId::DEFAULT));
    mWindow->consumeKeyDown(ui::LogicalDisplayId::DEFAULT);
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectKeyUp(*mDispatcher, ui::LogicalDisplayId::DEFAULT));

    // Stuck on the ACTION_UP
    const std::chrono::duration timeout = mWindow->getDispatchingTimeout(DISPATCHING_TIMEOUT);
    mFakePolicy->assertNotifyWindowUnresponsiveWasCalled(timeout, mWindow);

    // New tap will go to the spy window, but not to the window
    tapOnWindow();
    spy->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    spy->consumeMotionUp(ui::LogicalDisplayId::DEFAULT);

    mWindow->consumeKeyUp(ui::LogicalDisplayId::DEFAULT); // still the previous motion
    mDispatcher->waitForIdle();
    mFakePolicy->assertNotifyWindowResponsiveWasCalled(mWindow->getToken(), mWindow->getPid());
    mWindow->assertNoEvents();
    spy->assertNoEvents();
}

// If an app is not responding to a motion event, spy windows should continue to receive
// new motion events
TEST_F(InputDispatcherSingleWindowAnr, SpyWindowReceivesEventsDuringAppAnrOnMotion) {
    sp<FakeWindowHandle> spy = addSpyWindow();

    tapOnWindow();
    spy->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    spy->consumeMotionUp(ui::LogicalDisplayId::DEFAULT);

    mWindow->consumeMotionDown();
    // Stuck on the ACTION_UP
    const std::chrono::duration timeout = mWindow->getDispatchingTimeout(DISPATCHING_TIMEOUT);
    mFakePolicy->assertNotifyWindowUnresponsiveWasCalled(timeout, mWindow);

    // New tap will go to the spy window, but not to the window
    tapOnWindow();
    spy->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    spy->consumeMotionUp(ui::LogicalDisplayId::DEFAULT);

    mWindow->consumeMotionUp(ui::LogicalDisplayId::DEFAULT); // still the previous motion
    mDispatcher->waitForIdle();
    mFakePolicy->assertNotifyWindowResponsiveWasCalled(mWindow->getToken(), mWindow->getPid());
    mWindow->assertNoEvents();
    spy->assertNoEvents();
}

TEST_F(InputDispatcherSingleWindowAnr, UnresponsiveMonitorAnr) {
    mDispatcher->setMonitorDispatchingTimeoutForTest(SPY_TIMEOUT);

    FakeMonitorReceiver monitor =
            FakeMonitorReceiver(*mDispatcher, "M_1", ui::LogicalDisplayId::DEFAULT);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, WINDOW_LOCATION));

    mWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    const std::optional<uint32_t> consumeSeq = monitor.receiveEvent();
    ASSERT_TRUE(consumeSeq);

    mFakePolicy->assertNotifyWindowUnresponsiveWasCalled(SPY_TIMEOUT, monitor.getToken(),
                                                         MONITOR_PID);

    monitor.finishEvent(*consumeSeq);
    monitor.consumeMotionCancel(ui::LogicalDisplayId::DEFAULT);

    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertNotifyWindowResponsiveWasCalled(monitor.getToken(), MONITOR_PID);
}

// If a window is unresponsive, then you get anr. if the window later catches up and starts to
// process events, you don't get an anr. When the window later becomes unresponsive again, you
// get an ANR again.
// 1. tap -> block on ACTION_UP -> receive ANR
// 2. consume all pending events (= queue becomes healthy again)
// 3. tap again -> block on ACTION_UP again -> receive ANR second time
TEST_F(InputDispatcherSingleWindowAnr, SameWindow_CanReceiveAnrTwice) {
    tapOnWindow();

    mWindow->consumeMotionDown();
    // Block on ACTION_UP
    const std::chrono::duration timeout = mWindow->getDispatchingTimeout(DISPATCHING_TIMEOUT);
    mFakePolicy->assertNotifyWindowUnresponsiveWasCalled(timeout, mWindow);
    mWindow->consumeMotionUp(); // Now the connection should be healthy again
    mDispatcher->waitForIdle();
    mFakePolicy->assertNotifyWindowResponsiveWasCalled(mWindow->getToken(), mWindow->getPid());
    mWindow->assertNoEvents();

    tapOnWindow();
    mWindow->consumeMotionDown();
    mFakePolicy->assertNotifyWindowUnresponsiveWasCalled(timeout, mWindow);
    mWindow->consumeMotionUp();

    mDispatcher->waitForIdle();
    mFakePolicy->assertNotifyWindowResponsiveWasCalled(mWindow->getToken(), mWindow->getPid());
    mFakePolicy->assertNotifyAnrWasNotCalled();
    mWindow->assertNoEvents();
}

// If a connection remains unresponsive for a while, make sure policy is only notified once about
// it.
TEST_F(InputDispatcherSingleWindowAnr, Policy_DoesNotGetDuplicateAnr) {
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, WINDOW_LOCATION));

    const std::chrono::duration windowTimeout = mWindow->getDispatchingTimeout(DISPATCHING_TIMEOUT);
    mFakePolicy->assertNotifyWindowUnresponsiveWasCalled(windowTimeout, mWindow);
    std::this_thread::sleep_for(windowTimeout);
    // 'notifyConnectionUnresponsive' should only be called once per connection
    mFakePolicy->assertNotifyAnrWasNotCalled();
    // When the ANR happened, dispatcher should abort the current event stream via ACTION_CANCEL
    mWindow->consumeMotionDown();
    mWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDisplayId(ui::LogicalDisplayId::DEFAULT)));
    mWindow->assertNoEvents();
    mDispatcher->waitForIdle();
    mFakePolicy->assertNotifyWindowResponsiveWasCalled(mWindow->getToken(), mWindow->getPid());
    mFakePolicy->assertNotifyAnrWasNotCalled();
}

/**
 * If a window is processing a motion event, and then a key event comes in, the key event should
 * not get delivered to the focused window until the motion is processed.
 */
TEST_F(InputDispatcherSingleWindowAnr, Key_StaysPendingWhileMotionIsProcessed) {
    // The timeouts in this test are established by relying on the fact that the "key waiting for
    // events timeout" is equal to 500ms.
    ASSERT_EQ(mFakePolicy->getKeyWaitingForEventsTimeout(), 500ms);
    mWindow->setDispatchingTimeout(2s); // Set a long ANR timeout to prevent it from triggering
    mDispatcher->onWindowInfosChanged({{*mWindow->getInfo()}, {}, 0, 0});

    tapOnWindow();
    const auto& [downSequenceNum, downEvent] = mWindow->receiveEvent();
    ASSERT_TRUE(downSequenceNum);
    const auto& [upSequenceNum, upEvent] = mWindow->receiveEvent();
    ASSERT_TRUE(upSequenceNum);

    // Don't finish the events yet, and send a key
    mDispatcher->notifyKey(
            KeyArgsBuilder(AKEY_EVENT_ACTION_DOWN, AINPUT_SOURCE_KEYBOARD)
                    .policyFlags(DEFAULT_POLICY_FLAGS | POLICY_FLAG_DISABLE_KEY_REPEAT)
                    .build());
    // Key will not be sent to the window, yet, because the window is still processing events
    // and the key remains pending, waiting for the touch events to be processed
    // Make sure that `assertNoEvents` doesn't wait too long, because it could cause an ANR.
    mWindow->assertNoEvents(100ms);

    std::this_thread::sleep_for(400ms);
    // if we wait long enough though, dispatcher will give up, and still send the key
    // to the focused window, even though we have not yet finished the motion event
    mWindow->consumeKeyDown(ui::LogicalDisplayId::DEFAULT);
    mWindow->finishEvent(*downSequenceNum);
    mWindow->finishEvent(*upSequenceNum);
}

/**
 * If a window is processing a motion event, and then a key event comes in, the key event should
 * not go to the focused window until the motion is processed.
 * If then a new motion comes in, then the pending key event should be going to the currently
 * focused window right away.
 */
TEST_F(InputDispatcherSingleWindowAnr,
       PendingKey_IsDeliveredWhileMotionIsProcessingAndNewTouchComesIn) {
    // The timeouts in this test are established by relying on the fact that the "key waiting for
    // events timeout" is equal to 500ms.
    ASSERT_EQ(mFakePolicy->getKeyWaitingForEventsTimeout(), 500ms);
    mWindow->setDispatchingTimeout(2s); // Set a long ANR timeout to prevent it from triggering
    mDispatcher->onWindowInfosChanged({{*mWindow->getInfo()}, {}, 0, 0});

    tapOnWindow();
    const auto& [downSequenceNum, _] = mWindow->receiveEvent();
    ASSERT_TRUE(downSequenceNum);
    const auto& [upSequenceNum, upEvent] = mWindow->receiveEvent();
    ASSERT_TRUE(upSequenceNum);
    // Don't finish the events yet, and send a key
    mDispatcher->notifyKey(
            KeyArgsBuilder(AKEY_EVENT_ACTION_DOWN, AINPUT_SOURCE_KEYBOARD)
                    .policyFlags(DEFAULT_POLICY_FLAGS | POLICY_FLAG_DISABLE_KEY_REPEAT)
                    .build());
    // At this point, key is still pending, and should not be sent to the application yet.
    mWindow->assertNoEvents(100ms);

    // Now tap down again. It should cause the pending key to go to the focused window right away.
    tapOnWindow();
    // Now that we tapped, we should receive the key immediately.
    // Since there's still room for slowness, we use 200ms, which is much less than
    // the "key waiting for events' timeout of 500ms minus the already waited 100ms duration.
    std::unique_ptr<InputEvent> keyEvent = mWindow->consume(200ms);
    ASSERT_NE(nullptr, keyEvent);
    ASSERT_EQ(InputEventType::KEY, keyEvent->getType());
    ASSERT_THAT(static_cast<KeyEvent&>(*keyEvent), WithKeyAction(AKEY_EVENT_ACTION_DOWN));
    // it doesn't matter that we haven't ack'd the other events yet. We can finish events in any
    // order.
    mWindow->finishEvent(*downSequenceNum); // first tap's ACTION_DOWN
    mWindow->finishEvent(*upSequenceNum);   // first tap's ACTION_UP
    mWindow->consumeMotionEvent(WithMotionAction(ACTION_DOWN));
    mWindow->consumeMotionEvent(WithMotionAction(ACTION_UP));
    mWindow->assertNoEvents();
}

/**
 * Send an event to the app and have the app not respond right away.
 * When ANR is raised, policy will tell the dispatcher to cancel the events for that window.
 * So InputDispatcher will enqueue ACTION_CANCEL event as well.
 * At some point, the window becomes responsive again.
 * Ensure that subsequent events get dropped, and the next gesture is delivered.
 */
TEST_F(InputDispatcherSingleWindowAnr, TwoGesturesWithAnr) {
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(10).y(10))
                                      .build());

    const auto [sequenceNum, _] = mWindow->receiveEvent(); // ACTION_DOWN
    ASSERT_TRUE(sequenceNum);
    const std::chrono::duration timeout = mWindow->getDispatchingTimeout(DISPATCHING_TIMEOUT);
    mFakePolicy->assertNotifyWindowUnresponsiveWasCalled(timeout, mWindow);

    mWindow->finishEvent(*sequenceNum);
    mWindow->consumeMotionEvent(WithMotionAction(ACTION_CANCEL));
    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertNotifyWindowResponsiveWasCalled(mWindow->getToken(), mWindow->getPid());

    // Now that the window is responsive, let's continue the gesture.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(11).y(11))
                                      .build());

    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(11).y(11))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(3).y(3))
                                      .build());

    mDispatcher->notifyMotion(MotionArgsBuilder(POINTER_1_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(11).y(11))
                                      .pointer(PointerBuilder(1, ToolType::FINGER).x(3).y(3))
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(11).y(11))
                                      .build());
    // We already canceled this pointer, so the window shouldn't get any new events.
    mWindow->assertNoEvents();

    // Start another one.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(15).y(15))
                                      .build());
    mWindow->consumeMotionEvent(WithMotionAction(ACTION_DOWN));
}

// Send an event to the app and have the app not respond right away. Then remove the app window.
// When the window is removed, the dispatcher will cancel the events for that window.
// So InputDispatcher will enqueue ACTION_CANCEL event as well.
TEST_F(InputDispatcherSingleWindowAnr, AnrAfterWindowRemoval) {
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT, {WINDOW_LOCATION}));

    const auto [sequenceNum, _] = mWindow->receiveEvent(); // ACTION_DOWN
    ASSERT_TRUE(sequenceNum);

    // Remove the window, but the input channel should remain alive.
    mDispatcher->onWindowInfosChanged({{}, {}, 0, 0});

    const std::chrono::duration timeout = mWindow->getDispatchingTimeout(DISPATCHING_TIMEOUT);
    // Since the window was removed, Dispatcher does not know the PID associated with the window
    // anymore, so the policy is notified without the PID.
    mFakePolicy->assertNotifyWindowUnresponsiveWasCalled(timeout, mWindow->getToken(),
                                                         /*pid=*/std::nullopt);

    mWindow->finishEvent(*sequenceNum);
    // The cancellation was generated when the window was removed, along with the focus event.
    mWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDisplayId(ui::LogicalDisplayId::DEFAULT)));
    mWindow->consumeFocusEvent(false);
    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertNotifyWindowResponsiveWasCalled(mWindow->getToken(), /*pid=*/std::nullopt);
}

// Send an event to the app and have the app not respond right away. Wait for the policy to be
// notified of the unresponsive window, then remove the app window.
TEST_F(InputDispatcherSingleWindowAnr, AnrFollowedByWindowRemoval) {
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT, {WINDOW_LOCATION}));

    const auto [sequenceNum, _] = mWindow->receiveEvent(); // ACTION_DOWN
    ASSERT_TRUE(sequenceNum);
    const std::chrono::duration timeout = mWindow->getDispatchingTimeout(DISPATCHING_TIMEOUT);
    mFakePolicy->assertNotifyWindowUnresponsiveWasCalled(timeout, mWindow);

    // Remove the window, but the input channel should remain alive.
    mDispatcher->onWindowInfosChanged({{}, {}, 0, 0});

    mWindow->finishEvent(*sequenceNum);
    // The cancellation was generated during the ANR, and the window lost focus when it was removed.
    mWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDisplayId(ui::LogicalDisplayId::DEFAULT)));
    mWindow->consumeFocusEvent(false);
    ASSERT_TRUE(mDispatcher->waitForIdle());
    // Since the window was removed, Dispatcher does not know the PID associated with the window
    // becoming responsive, so the policy is notified without the PID.
    mFakePolicy->assertNotifyWindowResponsiveWasCalled(mWindow->getToken(), /*pid=*/std::nullopt);
}

class InputDispatcherMultiWindowAnr : public InputDispatcherTest {
    virtual void SetUp() override {
        InputDispatcherTest::SetUp();

        mApplication = std::make_shared<FakeApplicationHandle>();
        mApplication->setDispatchingTimeout(100ms);
        mUnfocusedWindow = sp<FakeWindowHandle>::make(mApplication, mDispatcher, "Unfocused",
                                                      ui::LogicalDisplayId::DEFAULT);
        mUnfocusedWindow->setFrame(Rect(0, 0, 30, 30));
        // Adding FLAG_WATCH_OUTSIDE_TOUCH to receive ACTION_OUTSIDE when another window is tapped
        mUnfocusedWindow->setWatchOutsideTouch(true);

        mFocusedWindow = sp<FakeWindowHandle>::make(mApplication, mDispatcher, "Focused",
                                                    ui::LogicalDisplayId::DEFAULT);
        mFocusedWindow->setDispatchingTimeout(100ms);
        mFocusedWindow->setFrame(Rect(50, 50, 100, 100));

        // Set focused application.
        mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, mApplication);
        mFocusedWindow->setFocusable(true);

        // Expect one focus window exist in display.
        mDispatcher->onWindowInfosChanged(
                {{*mUnfocusedWindow->getInfo(), *mFocusedWindow->getInfo()}, {}, 0, 0});
        setFocusedWindow(mFocusedWindow);
        mFocusedWindow->consumeFocusEvent(true);
    }

    virtual void TearDown() override {
        InputDispatcherTest::TearDown();

        mUnfocusedWindow.clear();
        mFocusedWindow.clear();
    }

protected:
    std::shared_ptr<FakeApplicationHandle> mApplication;
    sp<FakeWindowHandle> mUnfocusedWindow;
    sp<FakeWindowHandle> mFocusedWindow;
    static constexpr PointF UNFOCUSED_WINDOW_LOCATION = {20, 20};
    static constexpr PointF FOCUSED_WINDOW_LOCATION = {75, 75};
    static constexpr PointF LOCATION_OUTSIDE_ALL_WINDOWS = {40, 40};

    void tapOnFocusedWindow() { tap(FOCUSED_WINDOW_LOCATION); }

    void tapOnUnfocusedWindow() { tap(UNFOCUSED_WINDOW_LOCATION); }

private:
    void tap(const PointF& location) {
        ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
                  injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                                   ui::LogicalDisplayId::DEFAULT, location));
        ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
                  injectMotionUp(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                                 ui::LogicalDisplayId::DEFAULT, location));
    }
};

// If we have 2 windows that are both unresponsive, the one with the shortest timeout
// should be ANR'd first.
TEST_F(InputDispatcherMultiWindowAnr, TwoWindows_BothUnresponsive) {
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_DOWN,
                                                   AINPUT_SOURCE_TOUCHSCREEN)
                                        .pointer(PointerBuilder(0, ToolType::FINGER)
                                                         .x(FOCUSED_WINDOW_LOCATION.x)
                                                         .y(FOCUSED_WINDOW_LOCATION.y))
                                        .build()));
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_UP,
                                                   AINPUT_SOURCE_TOUCHSCREEN)
                                        .pointer(PointerBuilder(0, ToolType::FINGER)
                                                         .x(FOCUSED_WINDOW_LOCATION.x)
                                                         .y(FOCUSED_WINDOW_LOCATION.y))
                                        .build()));
    mFocusedWindow->consumeMotionDown();
    mFocusedWindow->consumeMotionUp();
    mUnfocusedWindow->consumeMotionOutside(ui::LogicalDisplayId::DEFAULT, /*flags=*/0);
    // We consumed all events, so no ANR
    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertNotifyAnrWasNotCalled();

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_DOWN,
                                                   AINPUT_SOURCE_TOUCHSCREEN)
                                        .pointer(PointerBuilder(0, ToolType::FINGER)
                                                         .x(FOCUSED_WINDOW_LOCATION.x)
                                                         .y(FOCUSED_WINDOW_LOCATION.y))
                                        .build()));
    const auto [unfocusedSequenceNum, _] = mUnfocusedWindow->receiveEvent();
    ASSERT_TRUE(unfocusedSequenceNum);

    const std::chrono::duration timeout =
            mFocusedWindow->getDispatchingTimeout(DISPATCHING_TIMEOUT);
    mFakePolicy->assertNotifyWindowUnresponsiveWasCalled(timeout, mFocusedWindow);

    mUnfocusedWindow->finishEvent(*unfocusedSequenceNum);
    mFocusedWindow->consumeMotionDown();
    // This cancel is generated because the connection was unresponsive
    mFocusedWindow->consumeMotionCancel();
    mFocusedWindow->assertNoEvents();
    mUnfocusedWindow->assertNoEvents();
    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertNotifyWindowResponsiveWasCalled(mFocusedWindow->getToken(),
                                                       mFocusedWindow->getPid());
    mFakePolicy->assertNotifyAnrWasNotCalled();
}

// If we have 2 windows with identical timeouts that are both unresponsive,
// it doesn't matter which order they should have ANR.
// But we should receive ANR for both.
TEST_F(InputDispatcherMultiWindowAnr, TwoWindows_BothUnresponsiveWithSameTimeout) {
    // Set the timeout for unfocused window to match the focused window
    mUnfocusedWindow->setDispatchingTimeout(
            mFocusedWindow->getDispatchingTimeout(DISPATCHING_TIMEOUT));
    mDispatcher->onWindowInfosChanged(
            {{*mUnfocusedWindow->getInfo(), *mFocusedWindow->getInfo()}, {}, 0, 0});

    tapOnFocusedWindow();
    // we should have ACTION_DOWN/ACTION_UP on focused window and ACTION_OUTSIDE on unfocused window
    // We don't know which window will ANR first. But both of them should happen eventually.
    std::array<sp<IBinder>, 2> anrConnectionTokens = {mFakePolicy->getUnresponsiveWindowToken(
                                                              mFocusedWindow->getDispatchingTimeout(
                                                                      DISPATCHING_TIMEOUT)),
                                                      mFakePolicy->getUnresponsiveWindowToken(0ms)};

    ASSERT_THAT(anrConnectionTokens,
                ::testing::UnorderedElementsAre(testing::Eq(mFocusedWindow->getToken()),
                                                testing::Eq(mUnfocusedWindow->getToken())));

    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertNotifyAnrWasNotCalled();

    mFocusedWindow->consumeMotionDown();
    mFocusedWindow->consumeMotionUp();
    mUnfocusedWindow->consumeMotionOutside();

    std::array<sp<IBinder>, 2> responsiveTokens = {mFakePolicy->getResponsiveWindowToken(),
                                                   mFakePolicy->getResponsiveWindowToken()};

    // Both applications should be marked as responsive, in any order
    ASSERT_THAT(responsiveTokens,
                ::testing::UnorderedElementsAre(testing::Eq(mFocusedWindow->getToken()),
                                                testing::Eq(mUnfocusedWindow->getToken())));
    mFakePolicy->assertNotifyAnrWasNotCalled();
}

// If a window is already not responding, the second tap on the same window should be ignored.
// We should also log an error to account for the dropped event (not tested here).
// At the same time, FLAG_WATCH_OUTSIDE_TOUCH targets should not receive any events.
TEST_F(InputDispatcherMultiWindowAnr, DuringAnr_SecondTapIsIgnored) {
    tapOnFocusedWindow();
    mUnfocusedWindow->consumeMotionOutside(ui::LogicalDisplayId::DEFAULT, /*flags=*/0);
    // Receive the events, but don't respond
    const auto [downEventSequenceNum, downEvent] = mFocusedWindow->receiveEvent(); // ACTION_DOWN
    ASSERT_TRUE(downEventSequenceNum);
    const auto [upEventSequenceNum, upEvent] = mFocusedWindow->receiveEvent(); // ACTION_UP
    ASSERT_TRUE(upEventSequenceNum);
    const std::chrono::duration timeout =
            mFocusedWindow->getDispatchingTimeout(DISPATCHING_TIMEOUT);
    mFakePolicy->assertNotifyWindowUnresponsiveWasCalled(timeout, mFocusedWindow);

    // Tap once again
    // We cannot use "tapOnFocusedWindow" because it asserts the injection result to be success
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, FOCUSED_WINDOW_LOCATION));
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              injectMotionUp(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, ui::LogicalDisplayId::DEFAULT,
                             FOCUSED_WINDOW_LOCATION));
    // Unfocused window does not receive ACTION_OUTSIDE because the tapped window is not a
    // valid touch target
    mUnfocusedWindow->assertNoEvents();

    // Consume the first tap
    mFocusedWindow->finishEvent(*downEventSequenceNum);
    mFocusedWindow->finishEvent(*upEventSequenceNum);
    ASSERT_TRUE(mDispatcher->waitForIdle());
    // The second tap did not go to the focused window
    mFocusedWindow->assertNoEvents();
    // Since all events are finished, connection should be deemed healthy again
    mFakePolicy->assertNotifyWindowResponsiveWasCalled(mFocusedWindow->getToken(),
                                                       mFocusedWindow->getPid());
    mFakePolicy->assertNotifyAnrWasNotCalled();
}

// If you tap outside of all windows, there will not be ANR
TEST_F(InputDispatcherMultiWindowAnr, TapOutsideAllWindows_DoesNotAnr) {
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, LOCATION_OUTSIDE_ALL_WINDOWS));
    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertNotifyAnrWasNotCalled();
}

// Since the focused window is paused, tapping on it should not produce any events
TEST_F(InputDispatcherMultiWindowAnr, Window_CanBePaused) {
    mFocusedWindow->setPaused(true);
    mDispatcher->onWindowInfosChanged(
            {{*mUnfocusedWindow->getInfo(), *mFocusedWindow->getInfo()}, {}, 0, 0});

    ASSERT_EQ(InputEventInjectionResult::FAILED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, FOCUSED_WINDOW_LOCATION));

    std::this_thread::sleep_for(mFocusedWindow->getDispatchingTimeout(DISPATCHING_TIMEOUT));
    ASSERT_TRUE(mDispatcher->waitForIdle());
    // Should not ANR because the window is paused, and touches shouldn't go to it
    mFakePolicy->assertNotifyAnrWasNotCalled();

    mFocusedWindow->assertNoEvents();
    mUnfocusedWindow->assertNoEvents();
}

/**
 * If a window is processing a motion event, and then a key event comes in, the key event should
 * not get delivered to the focused window until the motion is processed.
 * If a different window becomes focused at this time, the key should go to that window instead.
 *
 * Warning!!!
 * This test depends on the value of android::inputdispatcher::KEY_WAITING_FOR_MOTION_TIMEOUT
 * and the injection timeout that we specify when injecting the key.
 * We must have the injection timeout (100ms) be smaller than
 *  KEY_WAITING_FOR_MOTION_TIMEOUT (currently 500ms).
 *
 * If that value changes, this test should also change.
 */
TEST_F(InputDispatcherMultiWindowAnr, PendingKey_GoesToNewlyFocusedWindow) {
    // Set a long ANR timeout to prevent it from triggering
    mFocusedWindow->setDispatchingTimeout(2s);
    mDispatcher->onWindowInfosChanged(
            {{*mFocusedWindow->getInfo(), *mUnfocusedWindow->getInfo()}, {}, 0, 0});

    tapOnUnfocusedWindow();
    const auto [downSequenceNum, downEvent] = mUnfocusedWindow->receiveEvent();
    ASSERT_TRUE(downSequenceNum);
    const auto [upSequenceNum, upEvent] = mUnfocusedWindow->receiveEvent();
    ASSERT_TRUE(upSequenceNum);
    // Don't finish the events yet, and send a key
    // Injection will succeed because we will eventually give up and send the key to the focused
    // window even if motions are still being processed.

    InputEventInjectionResult result =
            injectKey(*mDispatcher, AKEY_EVENT_ACTION_DOWN, /*repeatCount=*/0,
                      ui::LogicalDisplayId::DEFAULT, InputEventInjectionSync::NONE,
                      /*injectionTimeout=*/100ms);
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, result);
    // Key will not be sent to the window, yet, because the window is still processing events
    // and the key remains pending, waiting for the touch events to be processed.
    // Make sure `assertNoEvents` doesn't take too long. It uses CONSUME_TIMEOUT_NO_EVENT_EXPECTED
    // under the hood.
    static_assert(CONSUME_TIMEOUT_NO_EVENT_EXPECTED < 100ms);
    mFocusedWindow->assertNoEvents();

    // Switch the focus to the "unfocused" window that we tapped. Expect the key to go there
    mFocusedWindow->setFocusable(false);
    mUnfocusedWindow->setFocusable(true);
    mDispatcher->onWindowInfosChanged(
            {{*mFocusedWindow->getInfo(), *mUnfocusedWindow->getInfo()}, {}, 0, 0});
    setFocusedWindow(mUnfocusedWindow);

    // Focus events should precede the key events
    mUnfocusedWindow->consumeFocusEvent(true);
    mFocusedWindow->consumeFocusEvent(false);

    // Finish the tap events, which should unblock dispatcher
    mUnfocusedWindow->finishEvent(*downSequenceNum);
    mUnfocusedWindow->finishEvent(*upSequenceNum);

    // Now that all queues are cleared and no backlog in the connections, the key event
    // can finally go to the newly focused "mUnfocusedWindow".
    mUnfocusedWindow->consumeKeyDown(ui::LogicalDisplayId::DEFAULT);
    mFocusedWindow->assertNoEvents();
    mUnfocusedWindow->assertNoEvents();
    mFakePolicy->assertNotifyAnrWasNotCalled();
}

// When the touch stream is split across 2 windows, and one of them does not respond,
// then ANR should be raised and the touch should be canceled for the unresponsive window.
// The other window should not be affected by that.
TEST_F(InputDispatcherMultiWindowAnr, SplitTouch_SingleWindowAnr) {
    // Touch Window 1
    mDispatcher->notifyMotion(
            generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {FOCUSED_WINDOW_LOCATION}));
    mUnfocusedWindow->consumeMotionOutside(ui::LogicalDisplayId::DEFAULT, /*flags=*/0);

    // Touch Window 2
    mDispatcher->notifyMotion(
            generateMotionArgs(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT,
                               {FOCUSED_WINDOW_LOCATION, UNFOCUSED_WINDOW_LOCATION}));

    const std::chrono::duration timeout =
            mFocusedWindow->getDispatchingTimeout(DISPATCHING_TIMEOUT);
    mFakePolicy->assertNotifyWindowUnresponsiveWasCalled(timeout, mFocusedWindow);

    mUnfocusedWindow->consumeMotionDown();
    mFocusedWindow->consumeMotionDown();
    // Focused window may or may not receive ACTION_MOVE
    // But it should definitely receive ACTION_CANCEL due to the ANR
    const auto [moveOrCancelSequenceNum, event] = mFocusedWindow->receiveEvent();
    ASSERT_TRUE(moveOrCancelSequenceNum);
    mFocusedWindow->finishEvent(*moveOrCancelSequenceNum);
    ASSERT_NE(nullptr, event);
    ASSERT_EQ(event->getType(), InputEventType::MOTION);
    MotionEvent& motionEvent = static_cast<MotionEvent&>(*event);
    if (motionEvent.getAction() == AMOTION_EVENT_ACTION_MOVE) {
        mFocusedWindow->consumeMotionCancel();
    } else {
        ASSERT_EQ(AMOTION_EVENT_ACTION_CANCEL, motionEvent.getAction());
    }
    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertNotifyWindowResponsiveWasCalled(mFocusedWindow->getToken(),
                                                       mFocusedWindow->getPid());

    mUnfocusedWindow->assertNoEvents();
    mFocusedWindow->assertNoEvents();
    mFakePolicy->assertNotifyAnrWasNotCalled();
}

/**
 * If we have no focused window, and a key comes in, we start the ANR timer.
 * The focused application should add a focused window before the timer runs out to prevent ANR.
 *
 * If the user touches another application during this time, the key should be dropped.
 * Next, if a new focused window comes in, without toggling the focused application,
 * then no ANR should occur.
 *
 * Normally, we would expect the new focused window to be accompanied by 'setFocusedApplication',
 * but in some cases the policy may not update the focused application.
 */
TEST_F(InputDispatcherMultiWindowAnr, FocusedWindowWithoutSetFocusedApplication_NoAnr) {
    std::shared_ptr<FakeApplicationHandle> focusedApplication =
            std::make_shared<FakeApplicationHandle>();
    focusedApplication->setDispatchingTimeout(300ms);
    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, focusedApplication);
    // The application that owns 'mFocusedWindow' and 'mUnfocusedWindow' is not focused.
    mFocusedWindow->setFocusable(false);

    mDispatcher->onWindowInfosChanged(
            {{*mFocusedWindow->getInfo(), *mUnfocusedWindow->getInfo()}, {}, 0, 0});
    mFocusedWindow->consumeFocusEvent(false);

    // Send a key. The ANR timer should start because there is no focused window.
    // 'focusedApplication' will get blamed if this timer completes.
    // Key will not be sent anywhere because we have no focused window. It will remain pending.
    InputEventInjectionResult result =
            injectKey(*mDispatcher, AKEY_EVENT_ACTION_DOWN, /*repeatCount=*/0,
                      ui::LogicalDisplayId::DEFAULT, InputEventInjectionSync::NONE,
                      /*injectionTimeout=*/100ms,
                      /*allowKeyRepeat=*/false);
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, result);

    // Wait until dispatcher starts the "no focused window" timer. If we don't wait here,
    // then the injected touches won't cause the focused event to get dropped.
    // The dispatcher only checks for whether the queue should be pruned upon queueing.
    // If we inject the touch right away and the ANR timer hasn't started, the touch event would
    // simply be added to the queue without 'shouldPruneInboundQueueLocked' returning 'true'.
    // For this test, it means that the key would get delivered to the window once it becomes
    // focused.
    std::this_thread::sleep_for(100ms);

    // Touch unfocused window. This should force the pending key to get dropped.
    mDispatcher->notifyMotion(
            generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {UNFOCUSED_WINDOW_LOCATION}));

    // We do not consume the motion right away, because that would require dispatcher to first
    // process (== drop) the key event, and by that time, ANR will be raised.
    // Set the focused window first.
    mFocusedWindow->setFocusable(true);
    mDispatcher->onWindowInfosChanged(
            {{*mFocusedWindow->getInfo(), *mUnfocusedWindow->getInfo()}, {}, 0, 0});
    setFocusedWindow(mFocusedWindow);
    mFocusedWindow->consumeFocusEvent(true);
    // We do not call "setFocusedApplication" here, even though the newly focused window belongs
    // to another application. This could be a bug / behaviour in the policy.

    mUnfocusedWindow->consumeMotionDown();

    ASSERT_TRUE(mDispatcher->waitForIdle());
    // Should not ANR because we actually have a focused window. It was just added too slowly.
    ASSERT_NO_FATAL_FAILURE(mFakePolicy->assertNotifyAnrWasNotCalled());
}

/**
 * If we are pruning input queue, we should never drop pointer events. Otherwise, we risk having
 * an inconsistent event stream inside the dispatcher. In this test, we make sure that the
 * dispatcher doesn't prune pointer events incorrectly.
 *
 * This test reproduces a crash in InputDispatcher.
 * To reproduce the crash, we need to simulate the conditions for "pruning input queue" to occur.
 *
 * Keep the currently focused application (mApplication), and have no focused window.
 * We set up two additional windows:
 * 1) The navigation bar window. This simulates the system "NavigationBar", which is used in the
 * 3-button navigation mode. This window injects a BACK button when it's touched. 2) The application
 * window. This window is not focusable, but is touchable.
 *
 * We first touch the navigation bar, which causes it to inject a key. Since there's no focused
 * window, the dispatcher doesn't process this key, and all other events inside dispatcher are now
 * blocked. The dispatcher is waiting for 'mApplication' to add a focused window.
 *
 * Now, we touch "Another window". This window is owned by a different application than
 * 'mApplication'. This causes the dispatcher to stop waiting for 'mApplication' to add a focused
 * window. Now, the "pruning input queue" behaviour should kick in, and the dispatcher should start
 * dropping the events from its queue. Ensure that no crash occurs.
 *
 * In this test, we are setting long timeouts to prevent ANRs and events dropped due to being stale.
 * This does not affect the test running time.
 */
TEST_F(InputDispatcherMultiWindowAnr, PruningInputQueueShouldNotDropPointerEvents) {
    std::shared_ptr<FakeApplicationHandle> systemUiApplication =
            std::make_shared<FakeApplicationHandle>();
    systemUiApplication->setDispatchingTimeout(3000ms);
    mFakePolicy->setStaleEventTimeout(3000ms);
    sp<FakeWindowHandle> navigationBar =
            sp<FakeWindowHandle>::make(systemUiApplication, mDispatcher, "NavigationBar",
                                       ui::LogicalDisplayId::DEFAULT);
    navigationBar->setFocusable(false);
    navigationBar->setWatchOutsideTouch(true);
    navigationBar->setFrame(Rect(0, 0, 100, 100));

    mApplication->setDispatchingTimeout(3000ms);
    // 'mApplication' is already focused, but we call it again here to make it explicit.
    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, mApplication);

    std::shared_ptr<FakeApplicationHandle> anotherApplication =
            std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> appWindow =
            sp<FakeWindowHandle>::make(anotherApplication, mDispatcher, "Another window",
                                       ui::LogicalDisplayId::DEFAULT);
    appWindow->setFocusable(false);
    appWindow->setFrame(Rect(100, 100, 200, 200));

    mDispatcher->onWindowInfosChanged(
            {{*navigationBar->getInfo(), *appWindow->getInfo()}, {}, 0, 0});
    // 'mFocusedWindow' is no longer in the dispatcher window list, and therefore loses focus
    mFocusedWindow->consumeFocusEvent(false);

    // Touch down the navigation bar. It consumes the touch and injects a key into the dispatcher
    // in response.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .build());
    navigationBar->consumeMotionEvent(WithMotionAction(ACTION_DOWN));

    // Key will not be sent anywhere because we have no focused window. It will remain pending.
    // Pretend we are injecting KEYCODE_BACK, but it doesn't actually matter what key it is.
    InputEventInjectionResult result =
            injectKey(*mDispatcher, AKEY_EVENT_ACTION_DOWN, /*repeatCount=*/0,
                      ui::LogicalDisplayId::DEFAULT, InputEventInjectionSync::NONE,
                      /*injectionTimeout=*/100ms,
                      /*allowKeyRepeat=*/false);
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, result);

    // Finish the gesture - lift up finger and inject ACTION_UP key event
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(50).y(50))
                                      .build());
    result = injectKey(*mDispatcher, AKEY_EVENT_ACTION_UP, /*repeatCount=*/0,
                       ui::LogicalDisplayId::DEFAULT, InputEventInjectionSync::NONE,
                       /*injectionTimeout=*/100ms,
                       /*allowKeyRepeat=*/false);
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, result);
    // The key that was injected is blocking the dispatcher, so the navigation bar shouldn't be
    // getting any events yet.
    navigationBar->assertNoEvents();

    // Now touch "Another window". This touch is going to a different application than the one we
    // are waiting for (which is 'mApplication').
    // This should cause the dispatcher to drop the pending focus-dispatched events (like the key
    // trying to be injected) and to continue processing the rest of the events in the original
    // order.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                                      .pointer(PointerBuilder(0, ToolType::FINGER).x(150).y(150))
                                      .build());
    navigationBar->consumeMotionEvent(WithMotionAction(ACTION_UP));
    navigationBar->consumeMotionEvent(WithMotionAction(ACTION_OUTSIDE));
    appWindow->consumeMotionEvent(WithMotionAction(ACTION_DOWN));

    appWindow->assertNoEvents();
    navigationBar->assertNoEvents();
}

// These tests ensure we cannot send touch events to a window that's positioned behind a window
// that has feature NO_INPUT_CHANNEL.
// Layout:
//   Top (closest to user)
//       mNoInputWindow (above all windows)
//       mBottomWindow
//   Bottom (furthest from user)
class InputDispatcherMultiWindowOcclusionTests : public InputDispatcherTest {
    virtual void SetUp() override {
        InputDispatcherTest::SetUp();

        mApplication = std::make_shared<FakeApplicationHandle>();
        mNoInputWindow = sp<FakeWindowHandle>::make(mApplication, mDispatcher,
                                                    "Window without input channel",
                                                    ui::LogicalDisplayId::DEFAULT,
                                                    /*createInputChannel=*/false);
        mNoInputWindow->setNoInputChannel(true);
        mNoInputWindow->setFrame(Rect(0, 0, 100, 100));
        // It's perfectly valid for this window to not have an associated input channel

        mBottomWindow = sp<FakeWindowHandle>::make(mApplication, mDispatcher, "Bottom window",
                                                   ui::LogicalDisplayId::DEFAULT);
        mBottomWindow->setFrame(Rect(0, 0, 100, 100));

        mDispatcher->onWindowInfosChanged(
                {{*mNoInputWindow->getInfo(), *mBottomWindow->getInfo()}, {}, 0, 0});
    }

protected:
    std::shared_ptr<FakeApplicationHandle> mApplication;
    sp<FakeWindowHandle> mNoInputWindow;
    sp<FakeWindowHandle> mBottomWindow;
};

TEST_F(InputDispatcherMultiWindowOcclusionTests, NoInputChannelFeature_DropsTouches) {
    PointF touchedPoint = {10, 10};

    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT, {touchedPoint}));

    mNoInputWindow->assertNoEvents();
    // Even though the window 'mNoInputWindow' positioned above 'mBottomWindow' does not have
    // an input channel, it is not marked as FLAG_NOT_TOUCHABLE,
    // and therefore should prevent mBottomWindow from receiving touches
    mBottomWindow->assertNoEvents();
}

/**
 * If a window has feature NO_INPUT_CHANNEL, and somehow (by mistake) still has an input channel,
 * ensure that this window does not receive any touches, and blocks touches to windows underneath.
 */
TEST_F(InputDispatcherMultiWindowOcclusionTests,
       NoInputChannelFeature_DropsTouchesWithValidChannel) {
    mNoInputWindow = sp<FakeWindowHandle>::make(mApplication, mDispatcher,
                                                "Window with input channel and NO_INPUT_CHANNEL",
                                                ui::LogicalDisplayId::DEFAULT);

    mNoInputWindow->setNoInputChannel(true);
    mNoInputWindow->setFrame(Rect(0, 0, 100, 100));
    mDispatcher->onWindowInfosChanged(
            {{*mNoInputWindow->getInfo(), *mBottomWindow->getInfo()}, {}, 0, 0});

    PointF touchedPoint = {10, 10};

    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT, {touchedPoint}));

    mNoInputWindow->assertNoEvents();
    mBottomWindow->assertNoEvents();
}

class InputDispatcherMirrorWindowFocusTests : public InputDispatcherTest {
protected:
    std::shared_ptr<FakeApplicationHandle> mApp;
    sp<FakeWindowHandle> mWindow;
    sp<FakeWindowHandle> mMirror;

    virtual void SetUp() override {
        InputDispatcherTest::SetUp();
        mApp = std::make_shared<FakeApplicationHandle>();
        mWindow = sp<FakeWindowHandle>::make(mApp, mDispatcher, "TestWindow",
                                             ui::LogicalDisplayId::DEFAULT);
        mMirror = mWindow->clone(ui::LogicalDisplayId::DEFAULT);
        mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, mApp);
        mWindow->setFocusable(true);
        mMirror->setFocusable(true);
        mDispatcher->onWindowInfosChanged({{*mWindow->getInfo(), *mMirror->getInfo()}, {}, 0, 0});
    }
};

TEST_F(InputDispatcherMirrorWindowFocusTests, CanGetFocus) {
    // Request focus on a mirrored window
    setFocusedWindow(mMirror);

    // window gets focused
    mWindow->consumeFocusEvent(true);
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyDown(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    mWindow->consumeKeyDown(ui::LogicalDisplayId::INVALID);
}

// A focused & mirrored window remains focused only if the window and its mirror are both
// focusable.
TEST_F(InputDispatcherMirrorWindowFocusTests, FocusedIfAllWindowsFocusable) {
    setFocusedWindow(mMirror);

    // window gets focused because it is above the mirror
    mWindow->consumeFocusEvent(true);
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyDown(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    mWindow->consumeKeyDown(ui::LogicalDisplayId::INVALID);
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyUp(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    mWindow->consumeKeyUp(ui::LogicalDisplayId::INVALID);

    mMirror->setFocusable(false);
    mDispatcher->onWindowInfosChanged({{*mWindow->getInfo(), *mMirror->getInfo()}, {}, 0, 0});

    // window loses focus since one of the windows associated with the token in not focusable
    mWindow->consumeFocusEvent(false);

    ASSERT_EQ(InputEventInjectionResult::TIMED_OUT, injectKeyDown(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::TIMED_OUT";
    mWindow->assertNoEvents();
}

// A focused & mirrored window remains focused until the window and its mirror both become
// invisible.
TEST_F(InputDispatcherMirrorWindowFocusTests, FocusedIfAnyWindowVisible) {
    setFocusedWindow(mMirror);

    // window gets focused
    mWindow->consumeFocusEvent(true);
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyDown(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    mWindow->consumeKeyDown(ui::LogicalDisplayId::INVALID);
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyUp(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    mWindow->consumeKeyUp(ui::LogicalDisplayId::INVALID);

    mMirror->setVisible(false);
    mDispatcher->onWindowInfosChanged({{*mWindow->getInfo(), *mMirror->getInfo()}, {}, 0, 0});

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyDown(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    mWindow->consumeKeyDown(ui::LogicalDisplayId::INVALID);
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyUp(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    mWindow->consumeKeyUp(ui::LogicalDisplayId::INVALID);

    mWindow->setVisible(false);
    mDispatcher->onWindowInfosChanged({{*mWindow->getInfo(), *mMirror->getInfo()}, {}, 0, 0});

    // window loses focus only after all windows associated with the token become invisible.
    mWindow->consumeFocusEvent(false);

    ASSERT_EQ(InputEventInjectionResult::TIMED_OUT, injectKeyDown(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::TIMED_OUT";
    mWindow->assertNoEvents();
}

// A focused & mirrored window remains focused until both windows are removed.
TEST_F(InputDispatcherMirrorWindowFocusTests, FocusedWhileWindowsAlive) {
    setFocusedWindow(mMirror);

    // window gets focused
    mWindow->consumeFocusEvent(true);
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyDown(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    mWindow->consumeKeyDown(ui::LogicalDisplayId::INVALID);
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyUp(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    mWindow->consumeKeyUp(ui::LogicalDisplayId::INVALID);

    // single window is removed but the window token remains focused
    mDispatcher->onWindowInfosChanged({{*mMirror->getInfo()}, {}, 0, 0});

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyDown(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    mMirror->consumeKeyDown(ui::LogicalDisplayId::INVALID);
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyUp(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    mMirror->consumeKeyUp(ui::LogicalDisplayId::INVALID);

    // Both windows are removed
    mDispatcher->onWindowInfosChanged({{}, {}, 0, 0});
    mWindow->consumeFocusEvent(false);

    ASSERT_EQ(InputEventInjectionResult::TIMED_OUT, injectKeyDown(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::TIMED_OUT";
    mWindow->assertNoEvents();
}

// Focus request can be pending until one window becomes visible.
TEST_F(InputDispatcherMirrorWindowFocusTests, DeferFocusWhenInvisible) {
    // Request focus on an invisible mirror.
    mWindow->setVisible(false);
    mMirror->setVisible(false);
    mDispatcher->onWindowInfosChanged({{*mWindow->getInfo(), *mMirror->getInfo()}, {}, 0, 0});
    setFocusedWindow(mMirror);

    // Injected key goes to pending queue.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectKey(*mDispatcher, AKEY_EVENT_ACTION_DOWN, /*repeatCount=*/0,
                        ui::LogicalDisplayId::DEFAULT, InputEventInjectionSync::NONE));

    mMirror->setVisible(true);
    mDispatcher->onWindowInfosChanged({{*mWindow->getInfo(), *mMirror->getInfo()}, {}, 0, 0});

    // window gets focused
    mWindow->consumeFocusEvent(true);
    // window gets the pending key event
    mWindow->consumeKeyDown(ui::LogicalDisplayId::DEFAULT);
}

class InputDispatcherPointerCaptureTests : public InputDispatcherTest {
protected:
    std::shared_ptr<FakeApplicationHandle> mApp;
    sp<FakeWindowHandle> mWindow;
    sp<FakeWindowHandle> mSecondWindow;

    void SetUp() override {
        InputDispatcherTest::SetUp();
        mApp = std::make_shared<FakeApplicationHandle>();
        mWindow = sp<FakeWindowHandle>::make(mApp, mDispatcher, "TestWindow",
                                             ui::LogicalDisplayId::DEFAULT);
        mWindow->setFocusable(true);
        mSecondWindow = sp<FakeWindowHandle>::make(mApp, mDispatcher, "TestWindow2",
                                                   ui::LogicalDisplayId::DEFAULT);
        mSecondWindow->setFocusable(true);

        mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, mApp);
        mDispatcher->onWindowInfosChanged(
                {{*mWindow->getInfo(), *mSecondWindow->getInfo()}, {}, 0, 0});

        setFocusedWindow(mWindow);
        mWindow->consumeFocusEvent(true);
    }

    void notifyPointerCaptureChanged(const PointerCaptureRequest& request) {
        mDispatcher->notifyPointerCaptureChanged(generatePointerCaptureChangedArgs(request));
    }

    PointerCaptureRequest requestAndVerifyPointerCapture(const sp<FakeWindowHandle>& window,
                                                         bool enabled) {
        mDispatcher->requestPointerCapture(window->getToken(), enabled);
        auto request = mFakePolicy->assertSetPointerCaptureCalled(window, enabled);
        notifyPointerCaptureChanged(request);
        window->consumeCaptureEvent(enabled);
        return request;
    }
};

TEST_F(InputDispatcherPointerCaptureTests, EnablePointerCaptureWhenFocused) {
    // Ensure that capture cannot be obtained for unfocused windows.
    mDispatcher->requestPointerCapture(mSecondWindow->getToken(), true);
    mFakePolicy->assertSetPointerCaptureNotCalled();
    mSecondWindow->assertNoEvents();

    // Ensure that capture can be enabled from the focus window.
    requestAndVerifyPointerCapture(mWindow, true);

    // Ensure that capture cannot be disabled from a window that does not have capture.
    mDispatcher->requestPointerCapture(mSecondWindow->getToken(), false);
    mFakePolicy->assertSetPointerCaptureNotCalled();

    // Ensure that capture can be disabled from the window with capture.
    requestAndVerifyPointerCapture(mWindow, false);
}

TEST_F(InputDispatcherPointerCaptureTests, DisablesPointerCaptureAfterWindowLosesFocus) {
    auto request = requestAndVerifyPointerCapture(mWindow, true);

    setFocusedWindow(mSecondWindow);

    // Ensure that the capture disabled event was sent first.
    mWindow->consumeCaptureEvent(false);
    mWindow->consumeFocusEvent(false);
    mSecondWindow->consumeFocusEvent(true);
    mFakePolicy->assertSetPointerCaptureCalled(mWindow, false);

    // Ensure that additional state changes from InputReader are not sent to the window.
    notifyPointerCaptureChanged({});
    notifyPointerCaptureChanged(request);
    notifyPointerCaptureChanged({});
    mWindow->assertNoEvents();
    mSecondWindow->assertNoEvents();
    mFakePolicy->assertSetPointerCaptureNotCalled();
}

TEST_F(InputDispatcherPointerCaptureTests, UnexpectedStateChangeDisablesPointerCapture) {
    auto request = requestAndVerifyPointerCapture(mWindow, true);

    // InputReader unexpectedly disables and enables pointer capture.
    notifyPointerCaptureChanged({});
    notifyPointerCaptureChanged(request);

    // Ensure that Pointer Capture is disabled.
    mFakePolicy->assertSetPointerCaptureCalled(mWindow, false);
    mWindow->consumeCaptureEvent(false);
    mWindow->assertNoEvents();
}

TEST_F(InputDispatcherPointerCaptureTests, OutOfOrderRequests) {
    requestAndVerifyPointerCapture(mWindow, true);

    // The first window loses focus.
    setFocusedWindow(mSecondWindow);
    mFakePolicy->assertSetPointerCaptureCalled(mWindow, false);
    mWindow->consumeCaptureEvent(false);

    // Request Pointer Capture from the second window before the notification from InputReader
    // arrives.
    mDispatcher->requestPointerCapture(mSecondWindow->getToken(), true);
    auto request = mFakePolicy->assertSetPointerCaptureCalled(mSecondWindow, true);

    // InputReader notifies Pointer Capture was disabled (because of the focus change).
    notifyPointerCaptureChanged({});

    // InputReader notifies Pointer Capture was enabled (because of mSecondWindow's request).
    notifyPointerCaptureChanged(request);

    mSecondWindow->consumeFocusEvent(true);
    mSecondWindow->consumeCaptureEvent(true);
}

TEST_F(InputDispatcherPointerCaptureTests, EnableRequestFollowsSequenceNumbers) {
    // App repeatedly enables and disables capture.
    mDispatcher->requestPointerCapture(mWindow->getToken(), true);
    auto firstRequest = mFakePolicy->assertSetPointerCaptureCalled(mWindow, true);
    mDispatcher->requestPointerCapture(mWindow->getToken(), false);
    mFakePolicy->assertSetPointerCaptureCalled(mWindow, false);
    mDispatcher->requestPointerCapture(mWindow->getToken(), true);
    auto secondRequest = mFakePolicy->assertSetPointerCaptureCalled(mWindow, true);

    // InputReader notifies that PointerCapture has been enabled for the first request. Since the
    // first request is now stale, this should do nothing.
    notifyPointerCaptureChanged(firstRequest);
    mWindow->assertNoEvents();

    // InputReader notifies that the second request was enabled.
    notifyPointerCaptureChanged(secondRequest);
    mWindow->consumeCaptureEvent(true);
}

TEST_F(InputDispatcherPointerCaptureTests, RapidToggleRequests) {
    requestAndVerifyPointerCapture(mWindow, true);

    // App toggles pointer capture off and on.
    mDispatcher->requestPointerCapture(mWindow->getToken(), false);
    mFakePolicy->assertSetPointerCaptureCalled(mWindow, false);

    mDispatcher->requestPointerCapture(mWindow->getToken(), true);
    auto enableRequest = mFakePolicy->assertSetPointerCaptureCalled(mWindow, true);

    // InputReader notifies that the latest "enable" request was processed, while skipping over the
    // preceding "disable" request.
    notifyPointerCaptureChanged(enableRequest);

    // Since pointer capture was never disabled during the rapid toggle, the window does not receive
    // any notifications.
    mWindow->assertNoEvents();
}

/**
 * One window. Hover mouse in the window, and then start capture. Make sure that the relative
 * mouse movements don't affect the previous mouse hovering state.
 * When pointer capture is enabled, the incoming events are always ACTION_MOVE (there are no
 * HOVER_MOVE events).
 */
TEST_F(InputDispatcherPointerCaptureTests, MouseHoverAndPointerCapture) {
    // Mouse hover on the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(110))
                                      .build());
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(100).y(110))
                                      .build());

    mWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_ENTER)));
    mWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_HOVER_MOVE)));

    // Start pointer capture
    requestAndVerifyPointerCapture(mWindow, true);

    // Send some relative mouse movements and receive them in the window.
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_MOVE, AINPUT_SOURCE_MOUSE_RELATIVE)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(10).y(11))
                                      .build());
    mWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithCoords(10, 11),
                                      WithSource(AINPUT_SOURCE_MOUSE_RELATIVE)));

    // Stop pointer capture
    requestAndVerifyPointerCapture(mWindow, false);

    // Continue hovering on the window
    mDispatcher->notifyMotion(MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                                      .pointer(PointerBuilder(0, ToolType::MOUSE).x(105).y(115))
                                      .build());
    mWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_HOVER_MOVE), WithSource(AINPUT_SOURCE_MOUSE)));

    mWindow->assertNoEvents();
}

TEST_F(InputDispatcherPointerCaptureTests, MultiDisplayPointerCapture) {
    // The default display is the focused display to begin with.
    requestAndVerifyPointerCapture(mWindow, true);

    // Move the second window to a second display, make it the focused window on that display.
    mSecondWindow->editInfo()->displayId = SECOND_DISPLAY_ID;
    mDispatcher->onWindowInfosChanged({{*mWindow->getInfo(), *mSecondWindow->getInfo()}, {}, 0, 0});
    setFocusedWindow(mSecondWindow);
    mSecondWindow->consumeFocusEvent(true);

    mWindow->assertNoEvents();

    // The second window cannot gain capture because it is not on the focused display.
    mDispatcher->requestPointerCapture(mSecondWindow->getToken(), true);
    mFakePolicy->assertSetPointerCaptureNotCalled();
    mSecondWindow->assertNoEvents();

    // Make the second display the focused display.
    mDispatcher->setFocusedDisplay(SECOND_DISPLAY_ID);
    mFakePolicy->assertFocusedDisplayNotified(SECOND_DISPLAY_ID);

    // This causes the first window to lose pointer capture, and it's unable to request capture.
    mWindow->consumeCaptureEvent(false);
    mFakePolicy->assertSetPointerCaptureCalled(mWindow, false);

    mDispatcher->requestPointerCapture(mWindow->getToken(), true);
    mFakePolicy->assertSetPointerCaptureNotCalled();

    // The second window is now able to gain pointer capture successfully.
    requestAndVerifyPointerCapture(mSecondWindow, true);
}

using InputDispatcherPointerCaptureDeathTest = InputDispatcherPointerCaptureTests;

TEST_F(InputDispatcherPointerCaptureDeathTest,
       NotifyPointerCaptureChangedWithWrongTokenAbortsDispatcher) {
    testing::GTEST_FLAG(death_test_style) = "threadsafe";
    ScopedSilentDeath _silentDeath;

    mDispatcher->requestPointerCapture(mWindow->getToken(), true);
    auto request = mFakePolicy->assertSetPointerCaptureCalled(mWindow, true);

    // Dispatch a pointer changed event with a wrong token.
    request.window = mSecondWindow->getToken();
    ASSERT_DEATH(
            {
                notifyPointerCaptureChanged(request);
                mSecondWindow->consumeCaptureEvent(true);
            },
            "Unexpected requested window for Pointer Capture.");
}

class InputDispatcherUntrustedTouchesTest : public InputDispatcherTest {
protected:
    constexpr static const float MAXIMUM_OBSCURING_OPACITY = 0.8;

    constexpr static const float OPACITY_ABOVE_THRESHOLD = 0.9;
    static_assert(OPACITY_ABOVE_THRESHOLD > MAXIMUM_OBSCURING_OPACITY);

    constexpr static const float OPACITY_BELOW_THRESHOLD = 0.7;
    static_assert(OPACITY_BELOW_THRESHOLD < MAXIMUM_OBSCURING_OPACITY);

    // When combined twice, ie 1 - (1 - 0.5)*(1 - 0.5) = 0.75 < 8, is still below the threshold
    constexpr static const float OPACITY_FAR_BELOW_THRESHOLD = 0.5;
    static_assert(OPACITY_FAR_BELOW_THRESHOLD < MAXIMUM_OBSCURING_OPACITY);
    static_assert(1 - (1 - OPACITY_FAR_BELOW_THRESHOLD) * (1 - OPACITY_FAR_BELOW_THRESHOLD) <
                  MAXIMUM_OBSCURING_OPACITY);

    static constexpr gui::Uid TOUCHED_APP_UID{10001};
    static constexpr gui::Uid APP_B_UID{10002};
    static constexpr gui::Uid APP_C_UID{10003};

    sp<FakeWindowHandle> mTouchWindow;

    virtual void SetUp() override {
        InputDispatcherTest::SetUp();
        mTouchWindow = getWindow(TOUCHED_APP_UID, "Touched");
        mDispatcher->setMaximumObscuringOpacityForTouch(MAXIMUM_OBSCURING_OPACITY);
    }

    virtual void TearDown() override {
        InputDispatcherTest::TearDown();
        mTouchWindow.clear();
    }

    sp<FakeWindowHandle> getOccludingWindow(gui::Uid uid, std::string name, TouchOcclusionMode mode,
                                            float alpha = 1.0f) {
        sp<FakeWindowHandle> window = getWindow(uid, name);
        window->setTouchable(false);
        window->setTouchOcclusionMode(mode);
        window->setAlpha(alpha);
        return window;
    }

    sp<FakeWindowHandle> getWindow(gui::Uid uid, std::string name) {
        std::shared_ptr<FakeApplicationHandle> app = std::make_shared<FakeApplicationHandle>();
        sp<FakeWindowHandle> window =
                sp<FakeWindowHandle>::make(app, mDispatcher, name, ui::LogicalDisplayId::DEFAULT);
        // Generate an arbitrary PID based on the UID
        window->setOwnerInfo(gui::Pid{static_cast<pid_t>(1777 + (uid.val() % 10000))}, uid);
        return window;
    }

    void touch(const std::vector<PointF>& points = {PointF{100, 200}}) {
        mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                     AINPUT_SOURCE_TOUCHSCREEN,
                                                     ui::LogicalDisplayId::DEFAULT, points));
    }
};

TEST_F(InputDispatcherUntrustedTouchesTest, WindowWithBlockUntrustedOcclusionMode_BlocksTouch) {
    const sp<FakeWindowHandle>& w =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::BLOCK_UNTRUSTED);
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->assertNoEvents();
}

TEST_F(InputDispatcherUntrustedTouchesTest,
       WindowWithBlockUntrustedOcclusionModeWithOpacityBelowThreshold_BlocksTouch) {
    const sp<FakeWindowHandle>& w =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::BLOCK_UNTRUSTED, 0.7f);
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->assertNoEvents();
}

TEST_F(InputDispatcherUntrustedTouchesTest,
       WindowWithBlockUntrustedOcclusionMode_DoesNotReceiveTouch) {
    const sp<FakeWindowHandle>& w =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::BLOCK_UNTRUSTED);
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    w->assertNoEvents();
}

TEST_F(InputDispatcherUntrustedTouchesTest, WindowWithAllowOcclusionMode_AllowsTouch) {
    const sp<FakeWindowHandle>& w = getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::ALLOW);
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->consumeAnyMotionDown();
}

TEST_F(InputDispatcherUntrustedTouchesTest, TouchOutsideOccludingWindow_AllowsTouch) {
    const sp<FakeWindowHandle>& w =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::BLOCK_UNTRUSTED);
    w->setFrame(Rect(0, 0, 50, 50));
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch({PointF{100, 100}});

    mTouchWindow->consumeAnyMotionDown();
}

TEST_F(InputDispatcherUntrustedTouchesTest, WindowFromSameUid_AllowsTouch) {
    const sp<FakeWindowHandle>& w =
            getOccludingWindow(TOUCHED_APP_UID, "A", TouchOcclusionMode::BLOCK_UNTRUSTED);
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->consumeAnyMotionDown();
}

TEST_F(InputDispatcherUntrustedTouchesTest, WindowWithZeroOpacity_AllowsTouch) {
    const sp<FakeWindowHandle>& w =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::BLOCK_UNTRUSTED, 0.0f);
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->consumeAnyMotionDown();
}

TEST_F(InputDispatcherUntrustedTouchesTest, WindowWithZeroOpacity_DoesNotReceiveTouch) {
    const sp<FakeWindowHandle>& w =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::BLOCK_UNTRUSTED, 0.0f);
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    w->assertNoEvents();
}

/**
 * This is important to make sure apps can't indirectly learn the position of touches (outside vs
 * inside) while letting them pass-through. Note that even though touch passes through the occluding
 * window, the occluding window will still receive ACTION_OUTSIDE event.
 */
TEST_F(InputDispatcherUntrustedTouchesTest,
       WindowWithZeroOpacityAndWatchOutside_ReceivesOutsideEvent) {
    const sp<FakeWindowHandle>& w =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::BLOCK_UNTRUSTED, 0.0f);
    w->setWatchOutsideTouch(true);
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    w->consumeMotionOutside();
}

TEST_F(InputDispatcherUntrustedTouchesTest, OutsideEvent_HasZeroCoordinates) {
    const sp<FakeWindowHandle>& w =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::BLOCK_UNTRUSTED, 0.0f);
    w->setWatchOutsideTouch(true);
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    w->consumeMotionOutsideWithZeroedCoords();
}

TEST_F(InputDispatcherUntrustedTouchesTest, WindowWithOpacityBelowThreshold_AllowsTouch) {
    const sp<FakeWindowHandle>& w =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::USE_OPACITY,
                               OPACITY_BELOW_THRESHOLD);
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->consumeAnyMotionDown();
}

TEST_F(InputDispatcherUntrustedTouchesTest, WindowWithOpacityAtThreshold_AllowsTouch) {
    const sp<FakeWindowHandle>& w =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::USE_OPACITY,
                               MAXIMUM_OBSCURING_OPACITY);
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->consumeAnyMotionDown();
}

TEST_F(InputDispatcherUntrustedTouchesTest, WindowWithOpacityAboveThreshold_BlocksTouch) {
    const sp<FakeWindowHandle>& w =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::USE_OPACITY,
                               OPACITY_ABOVE_THRESHOLD);
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->assertNoEvents();
}

TEST_F(InputDispatcherUntrustedTouchesTest, WindowsWithCombinedOpacityAboveThreshold_BlocksTouch) {
    // Resulting opacity = 1 - (1 - 0.7)*(1 - 0.7) = .91
    const sp<FakeWindowHandle>& w1 =
            getOccludingWindow(APP_B_UID, "B1", TouchOcclusionMode::USE_OPACITY,
                               OPACITY_BELOW_THRESHOLD);
    const sp<FakeWindowHandle>& w2 =
            getOccludingWindow(APP_B_UID, "B2", TouchOcclusionMode::USE_OPACITY,
                               OPACITY_BELOW_THRESHOLD);
    mDispatcher->onWindowInfosChanged(
            {{*w1->getInfo(), *w2->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->assertNoEvents();
}

TEST_F(InputDispatcherUntrustedTouchesTest, WindowsWithCombinedOpacityBelowThreshold_AllowsTouch) {
    // Resulting opacity = 1 - (1 - 0.5)*(1 - 0.5) = .75
    const sp<FakeWindowHandle>& w1 =
            getOccludingWindow(APP_B_UID, "B1", TouchOcclusionMode::USE_OPACITY,
                               OPACITY_FAR_BELOW_THRESHOLD);
    const sp<FakeWindowHandle>& w2 =
            getOccludingWindow(APP_B_UID, "B2", TouchOcclusionMode::USE_OPACITY,
                               OPACITY_FAR_BELOW_THRESHOLD);
    mDispatcher->onWindowInfosChanged(
            {{*w1->getInfo(), *w2->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->consumeAnyMotionDown();
}

TEST_F(InputDispatcherUntrustedTouchesTest,
       WindowsFromDifferentAppsEachBelowThreshold_AllowsTouch) {
    const sp<FakeWindowHandle>& wB =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::USE_OPACITY,
                               OPACITY_BELOW_THRESHOLD);
    const sp<FakeWindowHandle>& wC =
            getOccludingWindow(APP_C_UID, "C", TouchOcclusionMode::USE_OPACITY,
                               OPACITY_BELOW_THRESHOLD);
    mDispatcher->onWindowInfosChanged(
            {{*wB->getInfo(), *wC->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->consumeAnyMotionDown();
}

TEST_F(InputDispatcherUntrustedTouchesTest, WindowsFromDifferentAppsOneAboveThreshold_BlocksTouch) {
    const sp<FakeWindowHandle>& wB =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::USE_OPACITY,
                               OPACITY_BELOW_THRESHOLD);
    const sp<FakeWindowHandle>& wC =
            getOccludingWindow(APP_C_UID, "C", TouchOcclusionMode::USE_OPACITY,
                               OPACITY_ABOVE_THRESHOLD);
    mDispatcher->onWindowInfosChanged(
            {{*wB->getInfo(), *wC->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->assertNoEvents();
}

TEST_F(InputDispatcherUntrustedTouchesTest,
       WindowWithOpacityAboveThresholdAndSelfWindow_BlocksTouch) {
    const sp<FakeWindowHandle>& wA =
            getOccludingWindow(TOUCHED_APP_UID, "T", TouchOcclusionMode::USE_OPACITY,
                               OPACITY_BELOW_THRESHOLD);
    const sp<FakeWindowHandle>& wB =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::USE_OPACITY,
                               OPACITY_ABOVE_THRESHOLD);
    mDispatcher->onWindowInfosChanged(
            {{*wA->getInfo(), *wB->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->assertNoEvents();
}

TEST_F(InputDispatcherUntrustedTouchesTest,
       WindowWithOpacityBelowThresholdAndSelfWindow_AllowsTouch) {
    const sp<FakeWindowHandle>& wA =
            getOccludingWindow(TOUCHED_APP_UID, "T", TouchOcclusionMode::USE_OPACITY,
                               OPACITY_ABOVE_THRESHOLD);
    const sp<FakeWindowHandle>& wB =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::USE_OPACITY,
                               OPACITY_BELOW_THRESHOLD);
    mDispatcher->onWindowInfosChanged(
            {{*wA->getInfo(), *wB->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->consumeAnyMotionDown();
}

TEST_F(InputDispatcherUntrustedTouchesTest, SelfWindowWithOpacityAboveThreshold_AllowsTouch) {
    const sp<FakeWindowHandle>& w =
            getOccludingWindow(TOUCHED_APP_UID, "T", TouchOcclusionMode::USE_OPACITY,
                               OPACITY_ABOVE_THRESHOLD);
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->consumeAnyMotionDown();
}

TEST_F(InputDispatcherUntrustedTouchesTest, SelfWindowWithBlockUntrustedMode_AllowsTouch) {
    const sp<FakeWindowHandle>& w =
            getOccludingWindow(TOUCHED_APP_UID, "T", TouchOcclusionMode::BLOCK_UNTRUSTED);
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->consumeAnyMotionDown();
}

TEST_F(InputDispatcherUntrustedTouchesTest,
       OpacityThresholdIs0AndWindowAboveThreshold_BlocksTouch) {
    mDispatcher->setMaximumObscuringOpacityForTouch(0.0f);
    const sp<FakeWindowHandle>& w =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::USE_OPACITY, 0.1f);
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->assertNoEvents();
}

TEST_F(InputDispatcherUntrustedTouchesTest, OpacityThresholdIs0AndWindowAtThreshold_AllowsTouch) {
    mDispatcher->setMaximumObscuringOpacityForTouch(0.0f);
    const sp<FakeWindowHandle>& w =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::USE_OPACITY, 0.0f);
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->consumeAnyMotionDown();
}

TEST_F(InputDispatcherUntrustedTouchesTest,
       OpacityThresholdIs1AndWindowBelowThreshold_AllowsTouch) {
    mDispatcher->setMaximumObscuringOpacityForTouch(1.0f);
    const sp<FakeWindowHandle>& w =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::USE_OPACITY,
                               OPACITY_ABOVE_THRESHOLD);
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->consumeAnyMotionDown();
}

TEST_F(InputDispatcherUntrustedTouchesTest,
       WindowWithBlockUntrustedModeAndWindowWithOpacityBelowFromSameApp_BlocksTouch) {
    const sp<FakeWindowHandle>& w1 =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::BLOCK_UNTRUSTED,
                               OPACITY_BELOW_THRESHOLD);
    const sp<FakeWindowHandle>& w2 =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::USE_OPACITY,
                               OPACITY_BELOW_THRESHOLD);
    mDispatcher->onWindowInfosChanged(
            {{*w1->getInfo(), *w2->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->assertNoEvents();
}

/**
 * Window B of BLOCK_UNTRUSTED occlusion mode is enough to block the touch, we're testing that the
 * addition of another window (C) of USE_OPACITY occlusion mode and opacity below the threshold
 * (which alone would result in allowing touches) does not affect the blocking behavior.
 */
TEST_F(InputDispatcherUntrustedTouchesTest,
       WindowWithBlockUntrustedModeAndWindowWithOpacityBelowFromDifferentApps_BlocksTouch) {
    const sp<FakeWindowHandle>& wB =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::BLOCK_UNTRUSTED,
                               OPACITY_BELOW_THRESHOLD);
    const sp<FakeWindowHandle>& wC =
            getOccludingWindow(APP_C_UID, "C", TouchOcclusionMode::USE_OPACITY,
                               OPACITY_BELOW_THRESHOLD);
    mDispatcher->onWindowInfosChanged(
            {{*wB->getInfo(), *wC->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->assertNoEvents();
}

/**
 * This test is testing that a window from a different UID but with same application token doesn't
 * block the touch. Apps can share the application token for close UI collaboration for example.
 */
TEST_F(InputDispatcherUntrustedTouchesTest,
       WindowWithSameApplicationTokenFromDifferentApp_AllowsTouch) {
    const sp<FakeWindowHandle>& w =
            getOccludingWindow(APP_B_UID, "B", TouchOcclusionMode::BLOCK_UNTRUSTED);
    w->setApplicationToken(mTouchWindow->getApplicationToken());
    mDispatcher->onWindowInfosChanged({{*w->getInfo(), *mTouchWindow->getInfo()}, {}, 0, 0});

    touch();

    mTouchWindow->consumeAnyMotionDown();
}

class InputDispatcherDragTests : public InputDispatcherTest {
protected:
    std::shared_ptr<FakeApplicationHandle> mApp;
    sp<FakeWindowHandle> mWindow;
    sp<FakeWindowHandle> mSecondWindow;
    sp<FakeWindowHandle> mDragWindow;
    sp<FakeWindowHandle> mSpyWindow;
    // Mouse would force no-split, set the id as non-zero to verify if drag state could track it.
    static constexpr int32_t MOUSE_POINTER_ID = 1;

    void SetUp() override {
        InputDispatcherTest::SetUp();
        mApp = std::make_shared<FakeApplicationHandle>();
        mWindow = sp<FakeWindowHandle>::make(mApp, mDispatcher, "TestWindow",
                                             ui::LogicalDisplayId::DEFAULT);
        mWindow->setFrame(Rect(0, 0, 100, 100));

        mSecondWindow = sp<FakeWindowHandle>::make(mApp, mDispatcher, "TestWindow2",
                                                   ui::LogicalDisplayId::DEFAULT);
        mSecondWindow->setFrame(Rect(100, 0, 200, 100));

        mSpyWindow = sp<FakeWindowHandle>::make(mApp, mDispatcher, "SpyWindow",
                                                ui::LogicalDisplayId::DEFAULT);
        mSpyWindow->setSpy(true);
        mSpyWindow->setTrustedOverlay(true);
        mSpyWindow->setFrame(Rect(0, 0, 200, 100));

        mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, mApp);
        mDispatcher->onWindowInfosChanged(
                {{*mSpyWindow->getInfo(), *mWindow->getInfo(), *mSecondWindow->getInfo()},
                 {},
                 0,
                 0});
    }

    void injectDown(int fromSource = AINPUT_SOURCE_TOUCHSCREEN) {
        switch (fromSource) {
            case AINPUT_SOURCE_TOUCHSCREEN:
                ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
                          injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                                           ui::LogicalDisplayId::DEFAULT, {50, 50}))
                        << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
                break;
            case AINPUT_SOURCE_STYLUS:
                ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
                          injectMotionEvent(*mDispatcher,
                                            MotionEventBuilder(AMOTION_EVENT_ACTION_DOWN,
                                                               AINPUT_SOURCE_STYLUS)
                                                    .buttonState(
                                                            AMOTION_EVENT_BUTTON_STYLUS_PRIMARY)
                                                    .pointer(PointerBuilder(0, ToolType::STYLUS)
                                                                     .x(50)
                                                                     .y(50))
                                                    .build()));
                break;
            case AINPUT_SOURCE_MOUSE:
                ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
                          injectMotionEvent(*mDispatcher,
                                            MotionEventBuilder(AMOTION_EVENT_ACTION_DOWN,
                                                               AINPUT_SOURCE_MOUSE)
                                                    .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                                    .pointer(PointerBuilder(MOUSE_POINTER_ID,
                                                                            ToolType::MOUSE)
                                                                     .x(50)
                                                                     .y(50))
                                                    .build()));
                break;
            default:
                FAIL() << "Source " << fromSource << " doesn't support drag and drop";
        }

        // Window should receive motion event.
        mWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
        // Spy window should also receive motion event
        mSpyWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    }

    // Start performing drag, we will create a drag window and transfer touch to it.
    // @param sendDown : if true, send a motion down on first window before perform drag and drop.
    // Returns true on success.
    bool startDrag(bool sendDown = true, int fromSource = AINPUT_SOURCE_TOUCHSCREEN) {
        if (sendDown) {
            injectDown(fromSource);
        }

        // The drag window covers the entire display
        mDragWindow = sp<FakeWindowHandle>::make(mApp, mDispatcher, "DragWindow",
                                                 ui::LogicalDisplayId::DEFAULT);
        mDragWindow->setTouchableRegion(Region{{0, 0, 0, 0}});
        mDispatcher->onWindowInfosChanged({{*mDragWindow->getInfo(), *mSpyWindow->getInfo(),
                                            *mWindow->getInfo(), *mSecondWindow->getInfo()},
                                           {},
                                           0,
                                           0});

        // Transfer touch focus to the drag window
        bool transferred =
                mDispatcher->transferTouchGesture(mWindow->getToken(), mDragWindow->getToken(),
                                                  /*isDragDrop=*/true);
        if (transferred) {
            mWindow->consumeMotionCancel();
            mDragWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT,
                                           AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
        }
        return transferred;
    }
};

TEST_F(InputDispatcherDragTests, DragEnterAndDragExit) {
    startDrag();

    // Move on window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {50, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionMove(ui::LogicalDisplayId::DEFAULT,
                                   AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mWindow->consumeDragEvent(false, 50, 50);
    mSecondWindow->assertNoEvents();

    // Move to another window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {150, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionMove(ui::LogicalDisplayId::DEFAULT,
                                   AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mWindow->consumeDragEvent(true, 150, 50);
    mSecondWindow->consumeDragEvent(false, 50, 50);

    // Move back to original window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {50, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionMove(ui::LogicalDisplayId::DEFAULT,
                                   AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mWindow->consumeDragEvent(false, 50, 50);
    mSecondWindow->consumeDragEvent(true, -50, 50);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionUp(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, ui::LogicalDisplayId::DEFAULT,
                             {50, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionUp(ui::LogicalDisplayId::DEFAULT, AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mWindow->assertNoEvents();
    mSecondWindow->assertNoEvents();
}

TEST_F(InputDispatcherDragTests, DragEnterAndPointerDownPilfersPointers) {
    startDrag();

    // No cancel event after drag start
    mSpyWindow->assertNoEvents();

    const MotionEvent secondFingerDownEvent =
            MotionEventBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(50).y(50))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(60).y(60))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerDownEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    // Receives cancel for first pointer after next pointer down
    mSpyWindow->consumeMotionCancel();
    mSpyWindow->consumeMotionDown();

    mSpyWindow->assertNoEvents();
}

TEST_F(InputDispatcherDragTests, DragAndDrop) {
    startDrag();

    // Move on window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {50, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionMove(ui::LogicalDisplayId::DEFAULT,
                                   AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mWindow->consumeDragEvent(false, 50, 50);
    mSecondWindow->assertNoEvents();

    // Move to another window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {150, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionMove(ui::LogicalDisplayId::DEFAULT,
                                   AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mWindow->consumeDragEvent(true, 150, 50);
    mSecondWindow->consumeDragEvent(false, 50, 50);

    // drop to another window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionUp(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, ui::LogicalDisplayId::DEFAULT,
                             {150, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionUp(ui::LogicalDisplayId::DEFAULT, AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mFakePolicy->assertDropTargetEquals(*mDispatcher, mSecondWindow->getToken());
    mWindow->assertNoEvents();
    mSecondWindow->assertNoEvents();
}

TEST_F(InputDispatcherDragTests, DragAndDropNotCancelledIfSomeOtherPointerIsPilfered) {
    startDrag();

    // No cancel event after drag start
    mSpyWindow->assertNoEvents();

    const MotionEvent secondFingerDownEvent =
            MotionEventBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(50).y(50))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(60).y(60))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerDownEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    // Receives cancel for first pointer after next pointer down
    mSpyWindow->consumeMotionEvent(WithMotionAction(ACTION_CANCEL));
    mSpyWindow->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithPointerIds({1})));
    mDragWindow->consumeMotionEvent(WithMotionAction(ACTION_MOVE));

    mSpyWindow->assertNoEvents();

    // Spy window calls pilfer pointers
    EXPECT_EQ(OK, mDispatcher->pilferPointers(mSpyWindow->getToken()));
    mDragWindow->assertNoEvents();

    const MotionEvent firstFingerMoveEvent =
            MotionEventBuilder(ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(60).y(60))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(60).y(60))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, firstFingerMoveEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    // Drag window should still receive the new event
    mDragWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_MOVE), WithFlags(AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE)));
    mDragWindow->assertNoEvents();
}

TEST_F(InputDispatcherDragTests, StylusDragAndDrop) {
    startDrag(true, AINPUT_SOURCE_STYLUS);

    // Move on window and keep button pressed.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                        .buttonState(AMOTION_EVENT_BUTTON_STYLUS_PRIMARY)
                                        .pointer(PointerBuilder(0, ToolType::STYLUS).x(50).y(50))
                                        .build()))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionMove(ui::LogicalDisplayId::DEFAULT,
                                   AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mWindow->consumeDragEvent(false, 50, 50);
    mSecondWindow->assertNoEvents();

    // Move to another window and release button, expect to drop item.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                        .buttonState(0)
                                        .pointer(PointerBuilder(0, ToolType::STYLUS).x(150).y(50))
                                        .build()))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionMove(ui::LogicalDisplayId::DEFAULT,
                                   AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mWindow->assertNoEvents();
    mSecondWindow->assertNoEvents();
    mFakePolicy->assertDropTargetEquals(*mDispatcher, mSecondWindow->getToken());

    // nothing to the window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_STYLUS)
                                        .buttonState(0)
                                        .pointer(PointerBuilder(0, ToolType::STYLUS).x(150).y(50))
                                        .build()))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionUp(ui::LogicalDisplayId::DEFAULT, AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mWindow->assertNoEvents();
    mSecondWindow->assertNoEvents();
}

TEST_F(InputDispatcherDragTests, DragAndDropOnInvalidWindow) {
    startDrag();

    // Set second window invisible.
    mSecondWindow->setVisible(false);
    mDispatcher->onWindowInfosChanged(
            {{*mDragWindow->getInfo(), *mWindow->getInfo(), *mSecondWindow->getInfo()}, {}, 0, 0});

    // Move on window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {50, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionMove(ui::LogicalDisplayId::DEFAULT,
                                   AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mWindow->consumeDragEvent(false, 50, 50);
    mSecondWindow->assertNoEvents();

    // Move to another window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {150, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionMove(ui::LogicalDisplayId::DEFAULT,
                                   AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mWindow->consumeDragEvent(true, 150, 50);
    mSecondWindow->assertNoEvents();

    // drop to another window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionUp(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, ui::LogicalDisplayId::DEFAULT,
                             {150, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionUp(ui::LogicalDisplayId::DEFAULT, AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mFakePolicy->assertDropTargetEquals(*mDispatcher, nullptr);
    mWindow->assertNoEvents();
    mSecondWindow->assertNoEvents();
}

TEST_F(InputDispatcherDragTests, NoDragAndDropWhenMultiFingers) {
    // Ensure window could track pointerIds if it didn't support split touch.
    mWindow->setPreventSplitting(true);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {50, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);

    const MotionEvent secondFingerDownEvent =
            MotionEventBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .displayId(ui::LogicalDisplayId::DEFAULT)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(50).y(50))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(75).y(50))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerDownEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mWindow->consumeMotionPointerDown(/*pointerIndex=*/1);

    // Should not perform drag and drop when window has multi fingers.
    ASSERT_FALSE(startDrag(false));
}

TEST_F(InputDispatcherDragTests, DragAndDropWhenSplitTouch) {
    // First down on second window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {150, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    mSecondWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);

    // Second down on first window.
    const MotionEvent secondFingerDownEvent =
            MotionEventBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .displayId(ui::LogicalDisplayId::DEFAULT)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(150).y(50))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(50).y(50))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerDownEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mWindow->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    mSecondWindow->consumeMotionMove(ui::LogicalDisplayId::DEFAULT);

    // Perform drag and drop from first window.
    ASSERT_TRUE(startDrag(false));

    // Move on window.
    const MotionEvent secondFingerMoveEvent =
            MotionEventBuilder(AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(150).y(50))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(50).y(50))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerMoveEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT));
    mDragWindow->consumeMotionMove(ui::LogicalDisplayId::DEFAULT,
                                   AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mWindow->consumeDragEvent(false, 50, 50);
    mSecondWindow->consumeMotionMove();

    // Release the drag pointer should perform drop.
    const MotionEvent secondFingerUpEvent =
            MotionEventBuilder(POINTER_1_UP, AINPUT_SOURCE_TOUCHSCREEN)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(150).y(50))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(50).y(50))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerUpEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT));
    mDragWindow->consumeMotionUp(ui::LogicalDisplayId::DEFAULT, AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mFakePolicy->assertDropTargetEquals(*mDispatcher, mWindow->getToken());
    mWindow->assertNoEvents();
    mSecondWindow->consumeMotionMove();
}

TEST_F(InputDispatcherDragTests, DragAndDropWhenMultiDisplays) {
    startDrag();

    // Update window of second display.
    sp<FakeWindowHandle> windowInSecondary =
            sp<FakeWindowHandle>::make(mApp, mDispatcher, "D_2", SECOND_DISPLAY_ID);
    mDispatcher->onWindowInfosChanged(
            {{*mDragWindow->getInfo(), *mSpyWindow->getInfo(), *mWindow->getInfo(),
              *mSecondWindow->getInfo(), *windowInSecondary->getInfo()},
             {},
             0,
             0});

    // Let second display has a touch state.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_DOWN,
                                                   AINPUT_SOURCE_TOUCHSCREEN)
                                        .displayId(SECOND_DISPLAY_ID)
                                        .pointer(PointerBuilder(0, ToolType::FINGER).x(100).y(100))
                                        .build()));
    windowInSecondary->consumeMotionDown(SECOND_DISPLAY_ID, /*expectedFlag=*/0);
    // Update window again.
    mDispatcher->onWindowInfosChanged(
            {{*mDragWindow->getInfo(), *mSpyWindow->getInfo(), *mWindow->getInfo(),
              *mSecondWindow->getInfo(), *windowInSecondary->getInfo()},
             {},
             0,
             0});

    // Move on window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {50, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionMove(ui::LogicalDisplayId::DEFAULT,
                                   AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mWindow->consumeDragEvent(false, 50, 50);
    mSecondWindow->assertNoEvents();

    // Move to another window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT, {150, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionMove(ui::LogicalDisplayId::DEFAULT,
                                   AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mWindow->consumeDragEvent(true, 150, 50);
    mSecondWindow->consumeDragEvent(false, 50, 50);

    // drop to another window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionUp(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, ui::LogicalDisplayId::DEFAULT,
                             {150, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionUp(ui::LogicalDisplayId::DEFAULT, AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mFakePolicy->assertDropTargetEquals(*mDispatcher, mSecondWindow->getToken());
    mWindow->assertNoEvents();
    mSecondWindow->assertNoEvents();
}

TEST_F(InputDispatcherDragTests, MouseDragAndDrop) {
    startDrag(true, AINPUT_SOURCE_MOUSE);
    // Move on window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_MOUSE)
                                        .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                        .pointer(PointerBuilder(MOUSE_POINTER_ID, ToolType::MOUSE)
                                                         .x(50)
                                                         .y(50))
                                        .build()))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionMove(ui::LogicalDisplayId::DEFAULT,
                                   AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mWindow->consumeDragEvent(false, 50, 50);
    mSecondWindow->assertNoEvents();

    // Move to another window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_MOUSE)
                                        .buttonState(AMOTION_EVENT_BUTTON_PRIMARY)
                                        .pointer(PointerBuilder(MOUSE_POINTER_ID, ToolType::MOUSE)
                                                         .x(150)
                                                         .y(50))
                                        .build()))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionMove(ui::LogicalDisplayId::DEFAULT,
                                   AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mWindow->consumeDragEvent(true, 150, 50);
    mSecondWindow->consumeDragEvent(false, 50, 50);

    // drop to another window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher,
                                MotionEventBuilder(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_MOUSE)
                                        .buttonState(0)
                                        .pointer(PointerBuilder(MOUSE_POINTER_ID, ToolType::MOUSE)
                                                         .x(150)
                                                         .y(50))
                                        .build()))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    mDragWindow->consumeMotionUp(ui::LogicalDisplayId::DEFAULT, AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE);
    mFakePolicy->assertDropTargetEquals(*mDispatcher, mSecondWindow->getToken());
    mWindow->assertNoEvents();
    mSecondWindow->assertNoEvents();
}

/**
 * Start drag and drop with a pointer whose id is not 0, cancel the current touch, and ensure drag
 * and drop is also canceled. Then inject a simple gesture, and ensure dispatcher does not crash.
 */
TEST_F(InputDispatcherDragTests, DragAndDropFinishedWhenCancelCurrentTouch) {
    // Down on second window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {150, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    ASSERT_NO_FATAL_FAILURE(mSecondWindow->consumeMotionDown());
    ASSERT_NO_FATAL_FAILURE(mSpyWindow->consumeMotionDown());

    // Down on first window
    const MotionEvent secondFingerDownEvent =
            MotionEventBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .displayId(ui::LogicalDisplayId::DEFAULT)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(150).y(50))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(50).y(50))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerDownEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    ASSERT_NO_FATAL_FAILURE(mWindow->consumeMotionDown());
    ASSERT_NO_FATAL_FAILURE(mSecondWindow->consumeMotionMove());
    ASSERT_NO_FATAL_FAILURE(mSpyWindow->consumeMotionPointerDown(1));

    // Start drag on first window
    ASSERT_TRUE(startDrag(/*sendDown=*/false, AINPUT_SOURCE_TOUCHSCREEN));

    // Trigger cancel
    mDispatcher->cancelCurrentTouch();
    ASSERT_NO_FATAL_FAILURE(mSecondWindow->consumeMotionCancel());
    ASSERT_NO_FATAL_FAILURE(mDragWindow->consumeMotionCancel(ui::LogicalDisplayId::DEFAULT,
                                                             AMOTION_EVENT_FLAG_NO_FOCUS_CHANGE));
    ASSERT_NO_FATAL_FAILURE(mSpyWindow->consumeMotionCancel());

    ASSERT_TRUE(mDispatcher->waitForIdle());
    // The D&D finished with nullptr
    mFakePolicy->assertDropTargetEquals(*mDispatcher, nullptr);

    // Remove drag window
    mDispatcher->onWindowInfosChanged({{*mWindow->getInfo(), *mSecondWindow->getInfo()}, {}, 0, 0});

    // Inject a simple gesture, ensure dispatcher not crashed
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, PointF{50, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    ASSERT_NO_FATAL_FAILURE(mWindow->consumeMotionDown());

    const MotionEvent moveEvent =
            MotionEventBuilder(AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                    .displayId(ui::LogicalDisplayId::DEFAULT)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(50).y(50))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, moveEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    ASSERT_NO_FATAL_FAILURE(mWindow->consumeMotionMove());

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionUp(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, ui::LogicalDisplayId::DEFAULT,
                             {50, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    ASSERT_NO_FATAL_FAILURE(mWindow->consumeMotionUp());
}

TEST_F(InputDispatcherDragTests, NoDragAndDropWithHoveringPointer) {
    // Start hovering over the window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE,
                                ui::LogicalDisplayId::DEFAULT, {50, 50}));

    ASSERT_NO_FATAL_FAILURE(mWindow->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER)));
    ASSERT_NO_FATAL_FAILURE(mSpyWindow->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER)));

    ASSERT_FALSE(startDrag(/*sendDown=*/false))
            << "Drag and drop should not work with a hovering pointer";
}

class InputDispatcherDropInputFeatureTest : public InputDispatcherTest {};

TEST_F(InputDispatcherDropInputFeatureTest, WindowDropsInput) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Test window",
                                       ui::LogicalDisplayId::DEFAULT);
    window->setDropInput(true);
    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);
    window->setFocusable(true);
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);
    window->consumeFocusEvent(/*hasFocus=*/true, /*inTouchMode=*/true);

    // With the flag set, window should not get any input
    mDispatcher->notifyKey(generateKeyArgs(AKEY_EVENT_ACTION_DOWN, ui::LogicalDisplayId::DEFAULT));
    window->assertNoEvents();

    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));
    mDispatcher->waitForIdle();
    window->assertNoEvents();

    // With the flag cleared, the window should get input
    window->setDropInput(false);
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    mDispatcher->notifyKey(generateKeyArgs(AKEY_EVENT_ACTION_UP, ui::LogicalDisplayId::DEFAULT));
    window->consumeKeyUp(ui::LogicalDisplayId::DEFAULT);

    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));
    window->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    window->assertNoEvents();
}

TEST_F(InputDispatcherDropInputFeatureTest, ObscuredWindowDropsInput) {
    std::shared_ptr<FakeApplicationHandle> obscuringApplication =
            std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> obscuringWindow =
            sp<FakeWindowHandle>::make(obscuringApplication, mDispatcher, "obscuringWindow",
                                       ui::LogicalDisplayId::DEFAULT);
    obscuringWindow->setFrame(Rect(0, 0, 50, 50));
    obscuringWindow->setOwnerInfo(gui::Pid{111}, gui::Uid{111});
    obscuringWindow->setTouchable(false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Test window",
                                       ui::LogicalDisplayId::DEFAULT);
    window->setDropInputIfObscured(true);
    window->setOwnerInfo(gui::Pid{222}, gui::Uid{222});
    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);
    window->setFocusable(true);
    mDispatcher->onWindowInfosChanged(
            {{*obscuringWindow->getInfo(), *window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);
    window->consumeFocusEvent(/*hasFocus=*/true, /*inTouchMode=*/true);

    // With the flag set, window should not get any input
    mDispatcher->notifyKey(generateKeyArgs(AKEY_EVENT_ACTION_DOWN, ui::LogicalDisplayId::DEFAULT));
    window->assertNoEvents();

    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));
    window->assertNoEvents();

    // With the flag cleared, the window should get input
    window->setDropInputIfObscured(false);
    mDispatcher->onWindowInfosChanged(
            {{*obscuringWindow->getInfo(), *window->getInfo()}, {}, 0, 0});

    mDispatcher->notifyKey(generateKeyArgs(AKEY_EVENT_ACTION_UP, ui::LogicalDisplayId::DEFAULT));
    window->consumeKeyUp(ui::LogicalDisplayId::DEFAULT);

    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));
    window->consumeMotionDown(ui::LogicalDisplayId::DEFAULT,
                              AMOTION_EVENT_FLAG_WINDOW_IS_PARTIALLY_OBSCURED);
    window->assertNoEvents();
}

TEST_F(InputDispatcherDropInputFeatureTest, UnobscuredWindowGetsInput) {
    std::shared_ptr<FakeApplicationHandle> obscuringApplication =
            std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> obscuringWindow =
            sp<FakeWindowHandle>::make(obscuringApplication, mDispatcher, "obscuringWindow",
                                       ui::LogicalDisplayId::DEFAULT);
    obscuringWindow->setFrame(Rect(0, 0, 50, 50));
    obscuringWindow->setOwnerInfo(gui::Pid{111}, gui::Uid{111});
    obscuringWindow->setTouchable(false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();
    sp<FakeWindowHandle> window =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Test window",
                                       ui::LogicalDisplayId::DEFAULT);
    window->setDropInputIfObscured(true);
    window->setOwnerInfo(gui::Pid{222}, gui::Uid{222});
    mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);
    window->setFocusable(true);
    mDispatcher->onWindowInfosChanged(
            {{*obscuringWindow->getInfo(), *window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);
    window->consumeFocusEvent(/*hasFocus=*/true, /*inTouchMode=*/true);

    // With the flag set, window should not get any input
    mDispatcher->notifyKey(generateKeyArgs(AKEY_EVENT_ACTION_DOWN, ui::LogicalDisplayId::DEFAULT));
    window->assertNoEvents();

    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));
    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));
    window->assertNoEvents();

    // When the window is no longer obscured because it went on top, it should get input
    mDispatcher->onWindowInfosChanged(
            {{*window->getInfo(), *obscuringWindow->getInfo()}, {}, 0, 0});

    mDispatcher->notifyKey(generateKeyArgs(AKEY_EVENT_ACTION_UP, ui::LogicalDisplayId::DEFAULT));
    window->consumeKeyUp(ui::LogicalDisplayId::DEFAULT);

    mDispatcher->notifyMotion(generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
                                                 AINPUT_SOURCE_TOUCHSCREEN,
                                                 ui::LogicalDisplayId::DEFAULT));
    window->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    window->assertNoEvents();
}

class InputDispatcherTouchModeChangedTests : public InputDispatcherTest {
protected:
    std::shared_ptr<FakeApplicationHandle> mApp;
    std::shared_ptr<FakeApplicationHandle> mSecondaryApp;
    sp<FakeWindowHandle> mWindow;
    sp<FakeWindowHandle> mSecondWindow;
    sp<FakeWindowHandle> mThirdWindow;

    void SetUp() override {
        InputDispatcherTest::SetUp();

        mApp = std::make_shared<FakeApplicationHandle>();
        mSecondaryApp = std::make_shared<FakeApplicationHandle>();
        mWindow = sp<FakeWindowHandle>::make(mApp, mDispatcher, "TestWindow",
                                             ui::LogicalDisplayId::DEFAULT);
        mWindow->setFocusable(true);
        setFocusedWindow(mWindow);
        mSecondWindow = sp<FakeWindowHandle>::make(mApp, mDispatcher, "TestWindow2",
                                                   ui::LogicalDisplayId::DEFAULT);
        mSecondWindow->setFocusable(true);
        mThirdWindow =
                sp<FakeWindowHandle>::make(mSecondaryApp, mDispatcher,
                                           "TestWindow3_SecondaryDisplay", SECOND_DISPLAY_ID);
        mThirdWindow->setFocusable(true);

        mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, mApp);
        mDispatcher->onWindowInfosChanged(
                {{*mWindow->getInfo(), *mSecondWindow->getInfo(), *mThirdWindow->getInfo()},
                 {},
                 0,
                 0});
        mThirdWindow->setOwnerInfo(SECONDARY_WINDOW_PID, SECONDARY_WINDOW_UID);
        mWindow->consumeFocusEvent(true);

        // Set main display initial touch mode to InputDispatcher::kDefaultInTouchMode.
        if (mDispatcher->setInTouchMode(InputDispatcher::kDefaultInTouchMode, WINDOW_PID,
                                        WINDOW_UID, /*hasPermission=*/true,
                                        ui::LogicalDisplayId::DEFAULT)) {
            mWindow->consumeTouchModeEvent(InputDispatcher::kDefaultInTouchMode);
            mSecondWindow->consumeTouchModeEvent(InputDispatcher::kDefaultInTouchMode);
            mThirdWindow->assertNoEvents();
        }

        // Set secondary display initial touch mode to InputDispatcher::kDefaultInTouchMode.
        if (mDispatcher->setInTouchMode(InputDispatcher::kDefaultInTouchMode, SECONDARY_WINDOW_PID,
                                        SECONDARY_WINDOW_UID, /*hasPermission=*/true,
                                        SECOND_DISPLAY_ID)) {
            mWindow->assertNoEvents();
            mSecondWindow->assertNoEvents();
            mThirdWindow->consumeTouchModeEvent(InputDispatcher::kDefaultInTouchMode);
        }
    }

    void changeAndVerifyTouchModeInMainDisplayOnly(bool inTouchMode, gui::Pid pid, gui::Uid uid,
                                                   bool hasPermission) {
        ASSERT_TRUE(mDispatcher->setInTouchMode(inTouchMode, pid, uid, hasPermission,
                                                ui::LogicalDisplayId::DEFAULT));
        mWindow->consumeTouchModeEvent(inTouchMode);
        mSecondWindow->consumeTouchModeEvent(inTouchMode);
        mThirdWindow->assertNoEvents();
    }
};

TEST_F(InputDispatcherTouchModeChangedTests, FocusedWindowCanChangeTouchMode) {
    const WindowInfo& windowInfo = *mWindow->getInfo();
    changeAndVerifyTouchModeInMainDisplayOnly(!InputDispatcher::kDefaultInTouchMode,
                                              windowInfo.ownerPid, windowInfo.ownerUid,
                                              /* hasPermission=*/false);
}

TEST_F(InputDispatcherTouchModeChangedTests, NonFocusedWindowOwnerCannotChangeTouchMode) {
    const WindowInfo& windowInfo = *mWindow->getInfo();
    gui::Pid ownerPid = windowInfo.ownerPid;
    gui::Uid ownerUid = windowInfo.ownerUid;
    mWindow->setOwnerInfo(gui::Pid::INVALID, gui::Uid::INVALID);
    ASSERT_FALSE(mDispatcher->setInTouchMode(InputDispatcher::kDefaultInTouchMode, ownerPid,
                                             ownerUid, /*hasPermission=*/false,
                                             ui::LogicalDisplayId::DEFAULT));
    mWindow->assertNoEvents();
    mSecondWindow->assertNoEvents();
}

TEST_F(InputDispatcherTouchModeChangedTests, NonWindowOwnerMayChangeTouchModeOnPermissionGranted) {
    const WindowInfo& windowInfo = *mWindow->getInfo();
    gui::Pid ownerPid = windowInfo.ownerPid;
    gui::Uid ownerUid = windowInfo.ownerUid;
    mWindow->setOwnerInfo(gui::Pid::INVALID, gui::Uid::INVALID);
    changeAndVerifyTouchModeInMainDisplayOnly(!InputDispatcher::kDefaultInTouchMode, ownerPid,
                                              ownerUid, /*hasPermission=*/true);
}

TEST_F(InputDispatcherTouchModeChangedTests, EventIsNotGeneratedIfNotChangingTouchMode) {
    const WindowInfo& windowInfo = *mWindow->getInfo();
    ASSERT_FALSE(mDispatcher->setInTouchMode(InputDispatcher::kDefaultInTouchMode,
                                             windowInfo.ownerPid, windowInfo.ownerUid,
                                             /*hasPermission=*/true,
                                             ui::LogicalDisplayId::DEFAULT));
    mWindow->assertNoEvents();
    mSecondWindow->assertNoEvents();
}

TEST_F(InputDispatcherTouchModeChangedTests, ChangeTouchOnSecondaryDisplayOnly) {
    const WindowInfo& windowInfo = *mThirdWindow->getInfo();
    ASSERT_TRUE(mDispatcher->setInTouchMode(!InputDispatcher::kDefaultInTouchMode,
                                            windowInfo.ownerPid, windowInfo.ownerUid,
                                            /*hasPermission=*/true, SECOND_DISPLAY_ID));
    mWindow->assertNoEvents();
    mSecondWindow->assertNoEvents();
    mThirdWindow->consumeTouchModeEvent(!InputDispatcher::kDefaultInTouchMode);
}

TEST_F(InputDispatcherTouchModeChangedTests, CanChangeTouchModeWhenOwningLastInteractedWindow) {
    // Interact with the window first.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectKeyDown(*mDispatcher, ui::LogicalDisplayId::DEFAULT))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    mWindow->consumeKeyDown(ui::LogicalDisplayId::DEFAULT);

    // Then remove focus.
    mWindow->setFocusable(false);
    mDispatcher->onWindowInfosChanged({{*mWindow->getInfo()}, {}, 0, 0});

    // Assert that caller can switch touch mode by owning one of the last interacted window.
    const WindowInfo& windowInfo = *mWindow->getInfo();
    ASSERT_TRUE(mDispatcher->setInTouchMode(!InputDispatcher::kDefaultInTouchMode,
                                            windowInfo.ownerPid, windowInfo.ownerUid,
                                            /*hasPermission=*/false,
                                            ui::LogicalDisplayId::DEFAULT));
}

class InputDispatcherSpyWindowTest : public InputDispatcherTest {
public:
    sp<FakeWindowHandle> createSpy() {
        std::shared_ptr<FakeApplicationHandle> application =
                std::make_shared<FakeApplicationHandle>();
        std::string name = "Fake Spy ";
        name += std::to_string(mSpyCount++);
        sp<FakeWindowHandle> spy =
                sp<FakeWindowHandle>::make(application, mDispatcher, name.c_str(),
                                           ui::LogicalDisplayId::DEFAULT);
        spy->setSpy(true);
        spy->setTrustedOverlay(true);
        return spy;
    }

    sp<FakeWindowHandle> createForeground() {
        std::shared_ptr<FakeApplicationHandle> application =
                std::make_shared<FakeApplicationHandle>();
        sp<FakeWindowHandle> window =
                sp<FakeWindowHandle>::make(application, mDispatcher, "Fake Window",
                                           ui::LogicalDisplayId::DEFAULT);
        window->setFocusable(true);
        return window;
    }

private:
    int mSpyCount{0};
};

using InputDispatcherSpyWindowDeathTest = InputDispatcherSpyWindowTest;
/**
 * Adding a spy window that is not a trusted overlay causes Dispatcher to abort.
 */
TEST_F(InputDispatcherSpyWindowDeathTest, UntrustedSpy_AbortsDispatcher) {
    testing::GTEST_FLAG(death_test_style) = "threadsafe";
    ScopedSilentDeath _silentDeath;

    auto spy = createSpy();
    spy->setTrustedOverlay(false);
    ASSERT_DEATH(mDispatcher->onWindowInfosChanged({{*spy->getInfo()}, {}, 0, 0}),
                 ".* not a trusted overlay");
}

/**
 * Input injection into a display with a spy window but no foreground windows should succeed.
 */
TEST_F(InputDispatcherSpyWindowTest, NoForegroundWindow) {
    auto spy = createSpy();
    mDispatcher->onWindowInfosChanged({{*spy->getInfo()}, {}, 0, 0});

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    spy->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
}

/**
 * Verify the order in which different input windows receive events. The touched foreground window
 * (if there is one) should always receive the event first. When there are multiple spy windows, the
 * spy windows will receive the event according to their Z-order, where the top-most spy window will
 * receive events before ones belows it.
 *
 * Here, we set up a scenario with four windows in the following Z order from the top:
 *    spy1, spy2, window, spy3.
 * We then inject an event and verify that the foreground "window" receives it first, followed by
 * "spy1" and "spy2". The "spy3" does not receive the event because it is underneath the foreground
 * window.
 */
TEST_F(InputDispatcherSpyWindowTest, ReceivesInputInOrder) {
    auto window = createForeground();
    auto spy1 = createSpy();
    auto spy2 = createSpy();
    auto spy3 = createSpy();
    mDispatcher->onWindowInfosChanged(
            {{*spy1->getInfo(), *spy2->getInfo(), *window->getInfo(), *spy3->getInfo()}, {}, 0, 0});
    const std::vector<sp<FakeWindowHandle>> channels{spy1, spy2, window, spy3};
    const size_t numChannels = channels.size();

    base::unique_fd epollFd(epoll_create1(EPOLL_CLOEXEC));
    if (!epollFd.ok()) {
        FAIL() << "Failed to create epoll fd";
    }

    for (size_t i = 0; i < numChannels; i++) {
        struct epoll_event event = {.events = EPOLLIN, .data.u64 = i};
        if (epoll_ctl(epollFd.get(), EPOLL_CTL_ADD, channels[i]->getChannelFd(), &event) < 0) {
            FAIL() << "Failed to add fd to epoll";
        }
    }

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    std::vector<size_t> eventOrder;
    std::vector<struct epoll_event> events(numChannels);
    for (;;) {
        const int nFds = epoll_wait(epollFd.get(), events.data(), static_cast<int>(numChannels),
                                    (100ms).count());
        if (nFds < 0) {
            FAIL() << "Failed to call epoll_wait";
        }
        if (nFds == 0) {
            break; // epoll_wait timed out
        }
        for (int i = 0; i < nFds; i++) {
            ASSERT_EQ(static_cast<uint32_t>(EPOLLIN), events[i].events);
            eventOrder.push_back(static_cast<size_t>(events[i].data.u64));
            channels[i]->consumeMotionDown();
        }
    }

    // Verify the order in which the events were received.
    EXPECT_EQ(3u, eventOrder.size());
    EXPECT_EQ(2u, eventOrder[0]); // index 2: window
    EXPECT_EQ(0u, eventOrder[1]); // index 0: spy1
    EXPECT_EQ(1u, eventOrder[2]); // index 1: spy2
}

/**
 * A spy window using the NOT_TOUCHABLE flag does not receive events.
 */
TEST_F(InputDispatcherSpyWindowTest, NotTouchable) {
    auto window = createForeground();
    auto spy = createSpy();
    spy->setTouchable(false);
    mDispatcher->onWindowInfosChanged({{*spy->getInfo(), *window->getInfo()}, {}, 0, 0});

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    window->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    spy->assertNoEvents();
}

/**
 * A spy window will only receive gestures that originate within its touchable region. Gestures that
 * have their ACTION_DOWN outside of the touchable region of the spy window will not be dispatched
 * to the window.
 */
TEST_F(InputDispatcherSpyWindowTest, TouchableRegion) {
    auto window = createForeground();
    auto spy = createSpy();
    spy->setTouchableRegion(Region{{0, 0, 20, 20}});
    mDispatcher->onWindowInfosChanged({{*spy->getInfo(), *window->getInfo()}, {}, 0, 0});

    // Inject an event outside the spy window's touchable region.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    window->consumeMotionDown();
    spy->assertNoEvents();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionUp(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                             ui::LogicalDisplayId::DEFAULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    window->consumeMotionUp();
    spy->assertNoEvents();

    // Inject an event inside the spy window's touchable region.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {5, 10}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    window->consumeMotionDown();
    spy->consumeMotionDown();
}

/**
 * A spy window can listen for touches outside its touchable region using the WATCH_OUTSIDE_TOUCHES
 * flag, but it will get zero-ed out coordinates if the foreground has a different owner.
 */
TEST_F(InputDispatcherSpyWindowTest, WatchOutsideTouches) {
    auto window = createForeground();
    window->setOwnerInfo(gui::Pid{12}, gui::Uid{34});
    auto spy = createSpy();
    spy->setWatchOutsideTouch(true);
    spy->setOwnerInfo(gui::Pid{56}, gui::Uid{78});
    spy->setFrame(Rect{0, 0, 20, 20});
    mDispatcher->onWindowInfosChanged({{*spy->getInfo(), *window->getInfo()}, {}, 0, 0});

    // Inject an event outside the spy window's frame and touchable region.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {100, 200}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    window->consumeMotionDown();
    spy->consumeMotionOutsideWithZeroedCoords();
}

/**
 * Even when a spy window spans over multiple foreground windows, the spy should receive all
 * pointers that are down within its bounds.
 */
TEST_F(InputDispatcherSpyWindowTest, ReceivesMultiplePointers) {
    auto windowLeft = createForeground();
    windowLeft->setFrame({0, 0, 100, 200});
    auto windowRight = createForeground();
    windowRight->setFrame({100, 0, 200, 200});
    auto spy = createSpy();
    spy->setFrame({0, 0, 200, 200});
    mDispatcher->onWindowInfosChanged(
            {{*spy->getInfo(), *windowLeft->getInfo(), *windowRight->getInfo()}, {}, 0, 0});

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {50, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    windowLeft->consumeMotionDown();
    spy->consumeMotionDown();

    const MotionEvent secondFingerDownEvent =
            MotionEventBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(50).y(50))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(150).y(50))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerDownEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    windowRight->consumeMotionDown();
    spy->consumeMotionPointerDown(/*pointerIndex=*/1);
}

/**
 * When the first pointer lands outside the spy window and the second pointer lands inside it, the
 * the spy should receive the second pointer with ACTION_DOWN.
 */
TEST_F(InputDispatcherSpyWindowTest, ReceivesSecondPointerAsDown) {
    auto window = createForeground();
    window->setFrame({0, 0, 200, 200});
    auto spyRight = createSpy();
    spyRight->setFrame({100, 0, 200, 200});
    mDispatcher->onWindowInfosChanged({{*spyRight->getInfo(), *window->getInfo()}, {}, 0, 0});

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {50, 50}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    window->consumeMotionDown();
    spyRight->assertNoEvents();

    const MotionEvent secondFingerDownEvent =
            MotionEventBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(50).y(50))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(150).y(50))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerDownEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    window->consumeMotionPointerDown(/*pointerIndex=*/1);
    spyRight->consumeMotionDown();
}

/**
 * The spy window should not be able to affect whether or not touches are split. Only the foreground
 * windows should be allowed to control split touch.
 */
TEST_F(InputDispatcherSpyWindowTest, SplitIfNoForegroundWindowTouched) {
    // This spy window prevents touch splitting. However, we still expect to split touches
    // because a foreground window has not disabled splitting.
    auto spy = createSpy();
    spy->setPreventSplitting(true);

    auto window = createForeground();
    window->setFrame(Rect(0, 0, 100, 100));

    mDispatcher->onWindowInfosChanged({{*spy->getInfo(), *window->getInfo()}, {}, 0, 0});

    // First finger down, no window touched.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {100, 200}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    spy->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    window->assertNoEvents();

    // Second finger down on window, the window should receive touch down.
    const MotionEvent secondFingerDownEvent =
            MotionEventBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .displayId(ui::LogicalDisplayId::DEFAULT)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(100).y(200))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(50).y(50))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerDownEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    window->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    spy->consumeMotionPointerDown(/*pointerIndex=*/1);
}

/**
 * A spy window will usually be implemented as an un-focusable window. Verify that these windows
 * do not receive key events.
 */
TEST_F(InputDispatcherSpyWindowTest, UnfocusableSpyDoesNotReceiveKeyEvents) {
    auto spy = createSpy();
    spy->setFocusable(false);

    auto window = createForeground();
    mDispatcher->onWindowInfosChanged({{*spy->getInfo(), *window->getInfo()}, {}, 0, 0});
    setFocusedWindow(window);
    window->consumeFocusEvent(true);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyDown(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    window->consumeKeyDown(ui::LogicalDisplayId::INVALID);

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyUp(*mDispatcher))
            << "Inject key event should return InputEventInjectionResult::SUCCEEDED";
    window->consumeKeyUp(ui::LogicalDisplayId::INVALID);

    spy->assertNoEvents();
}

using InputDispatcherPilferPointersTest = InputDispatcherSpyWindowTest;

/**
 * A spy window can pilfer pointers. When this happens, touch gestures used by the spy window that
 * are currently sent to any other windows - including other spy windows - will also be cancelled.
 */
TEST_F(InputDispatcherPilferPointersTest, PilferPointers) {
    auto window = createForeground();
    auto spy1 = createSpy();
    auto spy2 = createSpy();
    mDispatcher->onWindowInfosChanged(
            {{*spy1->getInfo(), *spy2->getInfo(), *window->getInfo()}, {}, 0, 0});

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    window->consumeMotionDown();
    spy1->consumeMotionDown();
    spy2->consumeMotionDown();

    // Pilfer pointers from the second spy window.
    EXPECT_EQ(OK, mDispatcher->pilferPointers(spy2->getToken()));
    spy2->assertNoEvents();
    spy1->consumeMotionCancel();
    window->consumeMotionCancel();

    // The rest of the gesture should only be sent to the second spy window.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    spy2->consumeMotionMove();
    spy1->assertNoEvents();
    window->assertNoEvents();
}

/**
 * A spy window can pilfer pointers for a gesture even after the foreground window has been removed
 * in the middle of the gesture.
 */
TEST_F(InputDispatcherPilferPointersTest, CanPilferAfterWindowIsRemovedMidStream) {
    auto window = createForeground();
    auto spy = createSpy();
    mDispatcher->onWindowInfosChanged({{*spy->getInfo(), *window->getInfo()}, {}, 0, 0});

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    window->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);
    spy->consumeMotionDown(ui::LogicalDisplayId::DEFAULT);

    window->releaseChannel();

    EXPECT_EQ(OK, mDispatcher->pilferPointers(spy->getToken()));

    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionUp(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                             ui::LogicalDisplayId::DEFAULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    spy->consumeMotionUp(ui::LogicalDisplayId::DEFAULT);
}

/**
 * After a spy window pilfers pointers, new pointers that go down in its bounds should be sent to
 * the spy, but not to any other windows.
 */
TEST_F(InputDispatcherPilferPointersTest, ContinuesToReceiveGestureAfterPilfer) {
    auto spy = createSpy();
    auto window = createForeground();

    mDispatcher->onWindowInfosChanged({{*spy->getInfo(), *window->getInfo()}, {}, 0, 0});

    // First finger down on the window and the spy.
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {100, 200}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    spy->consumeMotionDown();
    window->consumeMotionDown();

    // Spy window pilfers the pointers.
    EXPECT_EQ(OK, mDispatcher->pilferPointers(spy->getToken()));
    window->consumeMotionCancel();

    // Second finger down on the window and spy, but the window should not receive the pointer down.
    const MotionEvent secondFingerDownEvent =
            MotionEventBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .displayId(ui::LogicalDisplayId::DEFAULT)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(100).y(200))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(50).y(50))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerDownEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";

    spy->consumeMotionPointerDown(/*pointerIndex=*/1);

    // Third finger goes down outside all windows, so injection should fail.
    const MotionEvent thirdFingerDownEvent =
            MotionEventBuilder(POINTER_2_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .displayId(ui::LogicalDisplayId::DEFAULT)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(100).y(200))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(50).y(50))
                    .pointer(PointerBuilder(/*id=*/2, ToolType::FINGER).x(-5).y(-5))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::FAILED,
              injectMotionEvent(*mDispatcher, thirdFingerDownEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::FAILED";

    spy->assertNoEvents();
    window->assertNoEvents();
}

/**
 * After a spy window pilfers pointers, only the pointers used by the spy should be canceled
 */
TEST_F(InputDispatcherPilferPointersTest, PartiallyPilferRequiredPointers) {
    auto spy = createSpy();
    spy->setFrame(Rect(0, 0, 100, 100));
    auto window = createForeground();
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*spy->getInfo(), *window->getInfo()}, {}, 0, 0});

    // First finger down on the window only
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {150, 150}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    window->consumeMotionDown();

    // Second finger down on the spy and window
    const MotionEvent secondFingerDownEvent =
            MotionEventBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .displayId(ui::LogicalDisplayId::DEFAULT)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(150).y(150))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(10).y(10))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerDownEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    spy->consumeMotionDown();
    window->consumeMotionPointerDown(1);

    // Third finger down on the spy and window
    const MotionEvent thirdFingerDownEvent =
            MotionEventBuilder(POINTER_2_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .displayId(ui::LogicalDisplayId::DEFAULT)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(150).y(150))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(10).y(10))
                    .pointer(PointerBuilder(/*id=*/2, ToolType::FINGER).x(50).y(50))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, thirdFingerDownEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    spy->consumeMotionPointerDown(1);
    window->consumeMotionPointerDown(2);

    // Spy window pilfers the pointers.
    EXPECT_EQ(OK, mDispatcher->pilferPointers(spy->getToken()));
    window->consumeMotionPointerUp(/*pointerIdx=*/2,
                                   AllOf(WithDisplayId(ui::LogicalDisplayId::DEFAULT),
                                         WithFlags(AMOTION_EVENT_FLAG_CANCELED),
                                         WithPointerCount(3)));
    window->consumeMotionPointerUp(/*pointerIdx=*/1,
                                   AllOf(WithDisplayId(ui::LogicalDisplayId::DEFAULT),
                                         WithFlags(AMOTION_EVENT_FLAG_CANCELED),
                                         WithPointerCount(2)));

    spy->assertNoEvents();
    window->assertNoEvents();
}

/**
 * After a spy window pilfers pointers, all pilfered pointers that have already been dispatched to
 * other windows should be canceled. If this results in the cancellation of all pointers for some
 * window, then that window should receive ACTION_CANCEL.
 */
TEST_F(InputDispatcherPilferPointersTest, PilferAllRequiredPointers) {
    auto spy = createSpy();
    spy->setFrame(Rect(0, 0, 100, 100));
    auto window = createForeground();
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*spy->getInfo(), *window->getInfo()}, {}, 0, 0});

    // First finger down on both spy and window
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {10, 10}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    window->consumeMotionDown();
    spy->consumeMotionDown();

    // Second finger down on the spy and window
    const MotionEvent secondFingerDownEvent =
            MotionEventBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .displayId(ui::LogicalDisplayId::DEFAULT)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(10).y(10))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(50).y(50))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerDownEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    spy->consumeMotionPointerDown(1);
    window->consumeMotionPointerDown(1);

    // Spy window pilfers the pointers.
    EXPECT_EQ(OK, mDispatcher->pilferPointers(spy->getToken()));
    window->consumeMotionCancel();

    spy->assertNoEvents();
    window->assertNoEvents();
}

/**
 * After a spy window pilfers pointers, new pointers that are not touching the spy window can still
 * be sent to other windows
 */
TEST_F(InputDispatcherPilferPointersTest, CanReceivePointersAfterPilfer) {
    auto spy = createSpy();
    spy->setFrame(Rect(0, 0, 100, 100));
    auto window = createForeground();
    window->setFrame(Rect(0, 0, 200, 200));

    mDispatcher->onWindowInfosChanged({{*spy->getInfo(), *window->getInfo()}, {}, 0, 0});

    // First finger down on both window and spy
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionDown(*mDispatcher, AINPUT_SOURCE_TOUCHSCREEN,
                               ui::LogicalDisplayId::DEFAULT, {10, 10}))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    window->consumeMotionDown();
    spy->consumeMotionDown();

    // Spy window pilfers the pointers.
    EXPECT_EQ(OK, mDispatcher->pilferPointers(spy->getToken()));
    window->consumeMotionCancel();

    // Second finger down on the window only
    const MotionEvent secondFingerDownEvent =
            MotionEventBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .displayId(ui::LogicalDisplayId::DEFAULT)
                    .eventTime(systemTime(SYSTEM_TIME_MONOTONIC))
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(10).y(10))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(150).y(150))
                    .build();
    ASSERT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, secondFingerDownEvent, INJECT_EVENT_TIMEOUT,
                                InputEventInjectionSync::WAIT_FOR_RESULT))
            << "Inject motion event should return InputEventInjectionResult::SUCCEEDED";
    window->consumeMotionDown();
    window->assertNoEvents();

    // TODO(b/232530217): do not send the unnecessary MOVE event and delete the next line
    spy->consumeMotionMove();
    spy->assertNoEvents();
}

/**
 * A window on the left and a window on the right. Also, a spy window that's above all of the
 * windows, and spanning both left and right windows.
 * Send simultaneous motion streams from two different devices, one to the left window, and another
 * to the right window.
 * Pilfer from spy window.
 * Check that the pilfering only affects the pointers that are actually being received by the spy.
 */
TEST_F(InputDispatcherPilferPointersTest, MultiDevicePilfer_legacy) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    sp<FakeWindowHandle> spy = createSpy();
    spy->setFrame(Rect(0, 0, 200, 200));
    sp<FakeWindowHandle> leftWindow = createForeground();
    leftWindow->setFrame(Rect(0, 0, 100, 100));

    sp<FakeWindowHandle> rightWindow = createForeground();
    rightWindow->setFrame(Rect(100, 0, 200, 100));

    constexpr int32_t stylusDeviceId = 1;
    constexpr int32_t touchDeviceId = 2;

    mDispatcher->onWindowInfosChanged(
            {{*spy->getInfo(), *leftWindow->getInfo(), *rightWindow->getInfo()}, {}, 0, 0});

    // Stylus down on left window and spy
    mDispatcher->notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(50).y(50))
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId)));
    spy->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId)));

    // Finger down on right window and spy - but spy already has stylus
    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .deviceId(touchDeviceId)
                    .pointer(PointerBuilder(0, ToolType::FINGER).x(150).y(50))
                    .build());
    rightWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));
    spy->assertNoEvents();

    // Act: pilfer from spy. Spy is currently receiving touch events.
    EXPECT_EQ(OK, mDispatcher->pilferPointers(spy->getToken()));
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(stylusDeviceId)));
    rightWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(touchDeviceId)));

    // Continue movements from both stylus and touch. Touch will be delivered to spy, but not stylus
    mDispatcher->notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(51).y(52))
                                      .build());
    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                    .deviceId(touchDeviceId)
                    .pointer(PointerBuilder(0, ToolType::FINGER).x(151).y(52))
                    .build());
    spy->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId)));

    spy->assertNoEvents();
    leftWindow->assertNoEvents();
    rightWindow->assertNoEvents();
}

/**
 * A window on the left and a window on the right. Also, a spy window that's above all of the
 * windows, and spanning both left and right windows.
 * Send simultaneous motion streams from two different devices, one to the left window, and another
 * to the right window.
 * Pilfer from spy window.
 * Check that the pilfering affects all of the pointers that are actually being received by the spy.
 * The spy should receive both the touch and the stylus events after pilfer.
 */
TEST_F(InputDispatcherPilferPointersTest, MultiDevicePilfer) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    sp<FakeWindowHandle> spy = createSpy();
    spy->setFrame(Rect(0, 0, 200, 200));
    sp<FakeWindowHandle> leftWindow = createForeground();
    leftWindow->setFrame(Rect(0, 0, 100, 100));

    sp<FakeWindowHandle> rightWindow = createForeground();
    rightWindow->setFrame(Rect(100, 0, 200, 100));

    constexpr int32_t stylusDeviceId = 1;
    constexpr int32_t touchDeviceId = 2;

    mDispatcher->onWindowInfosChanged(
            {{*spy->getInfo(), *leftWindow->getInfo(), *rightWindow->getInfo()}, {}, 0, 0});

    // Stylus down on left window and spy
    mDispatcher->notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(50).y(50))
                                      .build());
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId)));
    spy->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(stylusDeviceId)));

    // Finger down on right window and spy
    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .deviceId(touchDeviceId)
                    .pointer(PointerBuilder(0, ToolType::FINGER).x(150).y(50))
                    .build());
    rightWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));
    spy->consumeMotionEvent(AllOf(WithMotionAction(ACTION_DOWN), WithDeviceId(touchDeviceId)));

    // Act: pilfer from spy. Spy is currently receiving touch events.
    EXPECT_EQ(OK, mDispatcher->pilferPointers(spy->getToken()));
    leftWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(stylusDeviceId)));
    rightWindow->consumeMotionEvent(
            AllOf(WithMotionAction(ACTION_CANCEL), WithDeviceId(touchDeviceId)));

    // Continue movements from both stylus and touch. Touch and stylus will be delivered to spy
    // Instead of sending the two MOVE events for each input device together, and then receiving
    // them both, process them one at at time. InputConsumer is always in the batching mode, which
    // means that the two MOVE events will be initially put into a batch. Once the events are
    // batched, the 'consume' call may result in any of the MOVE events to be sent first (depending
    // on the implementation of InputConsumer), which would mean that the order of the received
    // events could be different depending on whether there are 1 or 2 events pending in the
    // InputChannel at the time the test calls 'consume'. To make assertions simpler here, and to
    // avoid this confusing behaviour, send and receive each MOVE event separately.
    mDispatcher->notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_STYLUS)
                                      .deviceId(stylusDeviceId)
                                      .pointer(PointerBuilder(0, ToolType::STYLUS).x(51).y(52))
                                      .build());
    spy->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(stylusDeviceId)));
    mDispatcher->notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN)
                    .deviceId(touchDeviceId)
                    .pointer(PointerBuilder(0, ToolType::FINGER).x(151).y(52))
                    .build());
    spy->consumeMotionEvent(AllOf(WithMotionAction(ACTION_MOVE), WithDeviceId(touchDeviceId)));

    spy->assertNoEvents();
    leftWindow->assertNoEvents();
    rightWindow->assertNoEvents();
}

TEST_F(InputDispatcherPilferPointersTest, NoPilferingWithHoveringPointers) {
    auto window = createForeground();
    auto spy = createSpy();
    mDispatcher->onWindowInfosChanged({{*spy->getInfo(), *window->getInfo()}, {}, 0, 0});

    mDispatcher->notifyMotion(
            MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                    .deviceId(1)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::MOUSE).x(100).y(200))
                    .build());
    window->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));
    spy->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));

    // Pilfer pointers from the spy window should fail.
    EXPECT_NE(OK, mDispatcher->pilferPointers(spy->getToken()));
    spy->assertNoEvents();
    window->assertNoEvents();
}

class InputDispatcherStylusInterceptorTest : public InputDispatcherTest {
public:
    std::pair<sp<FakeWindowHandle>, sp<FakeWindowHandle>> setupStylusOverlayScenario() {
        std::shared_ptr<FakeApplicationHandle> overlayApplication =
                std::make_shared<FakeApplicationHandle>();
        sp<FakeWindowHandle> overlay = sp<FakeWindowHandle>::make(overlayApplication, mDispatcher,
                                                                  "Stylus interceptor window",
                                                                  ui::LogicalDisplayId::DEFAULT);
        overlay->setFocusable(false);
        overlay->setOwnerInfo(gui::Pid{111}, gui::Uid{111});
        overlay->setTouchable(false);
        overlay->setInterceptsStylus(true);
        overlay->setTrustedOverlay(true);

        std::shared_ptr<FakeApplicationHandle> application =
                std::make_shared<FakeApplicationHandle>();
        sp<FakeWindowHandle> window =
                sp<FakeWindowHandle>::make(application, mDispatcher, "Application window",
                                           ui::LogicalDisplayId::DEFAULT);
        window->setFocusable(true);
        window->setOwnerInfo(gui::Pid{222}, gui::Uid{222});

        mDispatcher->setFocusedApplication(ui::LogicalDisplayId::DEFAULT, application);
        mDispatcher->onWindowInfosChanged({{*overlay->getInfo(), *window->getInfo()}, {}, 0, 0});
        setFocusedWindow(window);
        window->consumeFocusEvent(/*hasFocus=*/true, /*inTouchMode=*/true);
        return {std::move(overlay), std::move(window)};
    }

    void sendFingerEvent(int32_t action) {
        mDispatcher->notifyMotion(
                generateMotionArgs(action, AINPUT_SOURCE_TOUCHSCREEN | AINPUT_SOURCE_STYLUS,
                                   ui::LogicalDisplayId::DEFAULT, {PointF{20, 20}}));
    }

    void sendStylusEvent(int32_t action) {
        NotifyMotionArgs motionArgs =
                generateMotionArgs(action, AINPUT_SOURCE_TOUCHSCREEN | AINPUT_SOURCE_STYLUS,
                                   ui::LogicalDisplayId::DEFAULT, {PointF{30, 40}});
        motionArgs.pointerProperties[0].toolType = ToolType::STYLUS;
        mDispatcher->notifyMotion(motionArgs);
    }
};

using InputDispatcherStylusInterceptorDeathTest = InputDispatcherStylusInterceptorTest;

TEST_F(InputDispatcherStylusInterceptorDeathTest, UntrustedOverlay_AbortsDispatcher) {
    testing::GTEST_FLAG(death_test_style) = "threadsafe";
    ScopedSilentDeath _silentDeath;

    auto [overlay, window] = setupStylusOverlayScenario();
    overlay->setTrustedOverlay(false);
    // Configuring an untrusted overlay as a stylus interceptor should cause Dispatcher to abort.
    ASSERT_DEATH(mDispatcher->onWindowInfosChanged(
                         {{*overlay->getInfo(), *window->getInfo()}, {}, 0, 0}),
                 ".* not a trusted overlay");
}

TEST_F(InputDispatcherStylusInterceptorTest, ConsmesOnlyStylusEvents) {
    auto [overlay, window] = setupStylusOverlayScenario();
    mDispatcher->onWindowInfosChanged({{*overlay->getInfo(), *window->getInfo()}, {}, 0, 0});

    sendStylusEvent(AMOTION_EVENT_ACTION_DOWN);
    overlay->consumeMotionDown();
    sendStylusEvent(AMOTION_EVENT_ACTION_UP);
    overlay->consumeMotionUp();

    sendFingerEvent(AMOTION_EVENT_ACTION_DOWN);
    window->consumeMotionDown();
    sendFingerEvent(AMOTION_EVENT_ACTION_UP);
    window->consumeMotionUp();

    overlay->assertNoEvents();
    window->assertNoEvents();
}

TEST_F(InputDispatcherStylusInterceptorTest, SpyWindowStylusInterceptor) {
    auto [overlay, window] = setupStylusOverlayScenario();
    overlay->setSpy(true);
    mDispatcher->onWindowInfosChanged({{*overlay->getInfo(), *window->getInfo()}, {}, 0, 0});

    sendStylusEvent(AMOTION_EVENT_ACTION_DOWN);
    overlay->consumeMotionDown();
    window->consumeMotionDown();
    sendStylusEvent(AMOTION_EVENT_ACTION_UP);
    overlay->consumeMotionUp();
    window->consumeMotionUp();

    sendFingerEvent(AMOTION_EVENT_ACTION_DOWN);
    window->consumeMotionDown();
    sendFingerEvent(AMOTION_EVENT_ACTION_UP);
    window->consumeMotionUp();

    overlay->assertNoEvents();
    window->assertNoEvents();
}

/**
 * Set up a scenario to test the behavior used by the stylus handwriting detection feature.
 * The scenario is as follows:
 *   - The stylus interceptor overlay is configured as a spy window.
 *   - The stylus interceptor spy receives the start of a new stylus gesture.
 *   - It pilfers pointers and then configures itself to no longer be a spy.
 *   - The stylus interceptor continues to receive the rest of the gesture.
 */
TEST_F(InputDispatcherStylusInterceptorTest, StylusHandwritingScenario) {
    auto [overlay, window] = setupStylusOverlayScenario();
    overlay->setSpy(true);
    mDispatcher->onWindowInfosChanged({{*overlay->getInfo(), *window->getInfo()}, {}, 0, 0});

    sendStylusEvent(AMOTION_EVENT_ACTION_DOWN);
    overlay->consumeMotionDown();
    window->consumeMotionDown();

    // The interceptor pilfers the pointers.
    EXPECT_EQ(OK, mDispatcher->pilferPointers(overlay->getToken()));
    window->consumeMotionCancel();

    // The interceptor configures itself so that it is no longer a spy.
    overlay->setSpy(false);
    mDispatcher->onWindowInfosChanged({{*overlay->getInfo(), *window->getInfo()}, {}, 0, 0});

    // It continues to receive the rest of the stylus gesture.
    sendStylusEvent(AMOTION_EVENT_ACTION_MOVE);
    overlay->consumeMotionMove();
    sendStylusEvent(AMOTION_EVENT_ACTION_UP);
    overlay->consumeMotionUp();

    window->assertNoEvents();
}

struct User {
    gui::Pid mPid;
    gui::Uid mUid;
    uint32_t mPolicyFlags{DEFAULT_POLICY_FLAGS};
    std::unique_ptr<InputDispatcher>& mDispatcher;

    User(std::unique_ptr<InputDispatcher>& dispatcher, gui::Pid pid, gui::Uid uid)
          : mPid(pid), mUid(uid), mDispatcher(dispatcher) {}

    InputEventInjectionResult injectTargetedMotion(int32_t action) const {
        return injectMotionEvent(*mDispatcher, action, AINPUT_SOURCE_TOUCHSCREEN,
                                 ui::LogicalDisplayId::DEFAULT, {100, 200},
                                 {AMOTION_EVENT_INVALID_CURSOR_POSITION,
                                  AMOTION_EVENT_INVALID_CURSOR_POSITION},
                                 INJECT_EVENT_TIMEOUT, InputEventInjectionSync::WAIT_FOR_RESULT,
                                 systemTime(SYSTEM_TIME_MONOTONIC), {mUid}, mPolicyFlags);
    }

    InputEventInjectionResult injectTargetedKey(int32_t action) const {
        return inputdispatcher::injectKey(*mDispatcher, action, /*repeatCount=*/0,
                                          ui::LogicalDisplayId::INVALID,
                                          InputEventInjectionSync::WAIT_FOR_RESULT,
                                          INJECT_EVENT_TIMEOUT, /*allowKeyRepeat=*/false, {mUid},
                                          mPolicyFlags);
    }

    sp<FakeWindowHandle> createWindow(const char* name) const {
        std::shared_ptr<FakeApplicationHandle> overlayApplication =
                std::make_shared<FakeApplicationHandle>();
        sp<FakeWindowHandle> window =
                sp<FakeWindowHandle>::make(overlayApplication, mDispatcher, name,
                                           ui::LogicalDisplayId::DEFAULT);
        window->setOwnerInfo(mPid, mUid);
        return window;
    }
};

using InputDispatcherTargetedInjectionTest = InputDispatcherTest;

TEST_F(InputDispatcherTargetedInjectionTest, CanInjectIntoOwnedWindow) {
    auto owner = User(mDispatcher, gui::Pid{10}, gui::Uid{11});
    auto window = owner.createWindow("Owned window");
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    EXPECT_EQ(InputEventInjectionResult::SUCCEEDED,
              owner.injectTargetedMotion(AMOTION_EVENT_ACTION_DOWN));
    window->consumeMotionDown();

    setFocusedWindow(window);
    window->consumeFocusEvent(true);

    EXPECT_EQ(InputEventInjectionResult::SUCCEEDED,
              owner.injectTargetedKey(AKEY_EVENT_ACTION_DOWN));
    window->consumeKeyDown(ui::LogicalDisplayId::INVALID);
}

TEST_F(InputDispatcherTargetedInjectionTest, CannotInjectIntoUnownedWindow) {
    auto owner = User(mDispatcher, gui::Pid{10}, gui::Uid{11});
    auto window = owner.createWindow("Owned window");
    mDispatcher->onWindowInfosChanged({{*window->getInfo()}, {}, 0, 0});

    auto rando = User(mDispatcher, gui::Pid{20}, gui::Uid{21});
    EXPECT_EQ(InputEventInjectionResult::TARGET_MISMATCH,
              rando.injectTargetedMotion(AMOTION_EVENT_ACTION_DOWN));

    setFocusedWindow(window);
    window->consumeFocusEvent(true);

    EXPECT_EQ(InputEventInjectionResult::TARGET_MISMATCH,
              rando.injectTargetedKey(AKEY_EVENT_ACTION_DOWN));
    window->assertNoEvents();
}

TEST_F(InputDispatcherTargetedInjectionTest, CanInjectIntoOwnedSpyWindow) {
    auto owner = User(mDispatcher, gui::Pid{10}, gui::Uid{11});
    auto window = owner.createWindow("Owned window");
    auto spy = owner.createWindow("Owned spy");
    spy->setSpy(true);
    spy->setTrustedOverlay(true);
    mDispatcher->onWindowInfosChanged({{*spy->getInfo(), *window->getInfo()}, {}, 0, 0});

    EXPECT_EQ(InputEventInjectionResult::SUCCEEDED,
              owner.injectTargetedMotion(AMOTION_EVENT_ACTION_DOWN));
    spy->consumeMotionDown();
    window->consumeMotionDown();
}

TEST_F(InputDispatcherTargetedInjectionTest, CannotInjectIntoUnownedSpyWindow) {
    auto owner = User(mDispatcher, gui::Pid{10}, gui::Uid{11});
    auto window = owner.createWindow("Owned window");

    auto rando = User(mDispatcher, gui::Pid{20}, gui::Uid{21});
    auto randosSpy = rando.createWindow("Rando's spy");
    randosSpy->setSpy(true);
    randosSpy->setTrustedOverlay(true);
    mDispatcher->onWindowInfosChanged({{*randosSpy->getInfo(), *window->getInfo()}, {}, 0, 0});

    // The event is targeted at owner's window, so injection should succeed, but the spy should
    // not receive the event.
    EXPECT_EQ(InputEventInjectionResult::SUCCEEDED,
              owner.injectTargetedMotion(AMOTION_EVENT_ACTION_DOWN));
    randosSpy->assertNoEvents();
    window->consumeMotionDown();
}

TEST_F(InputDispatcherTargetedInjectionTest, CanInjectIntoAnyWindowWhenNotTargeting) {
    auto owner = User(mDispatcher, gui::Pid{10}, gui::Uid{11});
    auto window = owner.createWindow("Owned window");

    auto rando = User(mDispatcher, gui::Pid{20}, gui::Uid{21});
    auto randosSpy = rando.createWindow("Rando's spy");
    randosSpy->setSpy(true);
    randosSpy->setTrustedOverlay(true);
    mDispatcher->onWindowInfosChanged({{*randosSpy->getInfo(), *window->getInfo()}, {}, 0, 0});

    // A user that has injection permission can inject into any window.
    EXPECT_EQ(InputEventInjectionResult::SUCCEEDED,
              injectMotionEvent(*mDispatcher, AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                                ui::LogicalDisplayId::DEFAULT));
    randosSpy->consumeMotionDown();
    window->consumeMotionDown();

    setFocusedWindow(randosSpy);
    randosSpy->consumeFocusEvent(true);

    EXPECT_EQ(InputEventInjectionResult::SUCCEEDED, injectKeyDown(*mDispatcher));
    randosSpy->consumeKeyDown(ui::LogicalDisplayId::INVALID);
    window->assertNoEvents();
}

TEST_F(InputDispatcherTargetedInjectionTest, CannotGenerateActionOutsideToOtherUids) {
    auto owner = User(mDispatcher, gui::Pid{10}, gui::Uid{11});
    auto window = owner.createWindow("Owned window");

    auto rando = User(mDispatcher, gui::Pid{20}, gui::Uid{21});
    auto randosWindow = rando.createWindow("Rando's window");
    randosWindow->setFrame(Rect{-10, -10, -5, -5});
    randosWindow->setWatchOutsideTouch(true);
    mDispatcher->onWindowInfosChanged({{*randosWindow->getInfo(), *window->getInfo()}, {}, 0, 0});

    // Do not allow generation of ACTION_OUTSIDE events into windows owned by different uids.
    EXPECT_EQ(InputEventInjectionResult::SUCCEEDED,
              owner.injectTargetedMotion(AMOTION_EVENT_ACTION_DOWN));
    window->consumeMotionDown();
    randosWindow->assertNoEvents();
}

using InputDispatcherPointerInWindowTest = InputDispatcherTest;

TEST_F(InputDispatcherPointerInWindowTest, PointerInWindowWhenHovering) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> left = sp<FakeWindowHandle>::make(application, mDispatcher, "Left Window",
                                                           ui::LogicalDisplayId::DEFAULT);
    left->setFrame(Rect(0, 0, 100, 100));
    sp<FakeWindowHandle> right =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Right Window",
                                       ui::LogicalDisplayId::DEFAULT);
    right->setFrame(Rect(100, 0, 200, 100));
    sp<FakeWindowHandle> spy = sp<FakeWindowHandle>::make(application, mDispatcher, "Spy Window",
                                                          ui::LogicalDisplayId::DEFAULT);
    spy->setFrame(Rect(0, 0, 200, 100));
    spy->setTrustedOverlay(true);
    spy->setSpy(true);

    mDispatcher->onWindowInfosChanged(
            {{*spy->getInfo(), *left->getInfo(), *right->getInfo()}, {}, 0, 0});

    // Hover into the left window.
    mDispatcher->notifyMotion(
            MotionArgsBuilder(ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::STYLUS).x(50).y(50))
                    .build());

    left->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));
    spy->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));

    ASSERT_TRUE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               DEVICE_ID,
                                               /*pointerId=*/0));
    ASSERT_TRUE(mDispatcher->isPointerInWindow(spy->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               DEVICE_ID,
                                               /*pointerId=*/0));
    ASSERT_FALSE(mDispatcher->isPointerInWindow(right->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/0));

    // Hover move to the right window.
    mDispatcher->notifyMotion(
            MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_STYLUS)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::STYLUS).x(150).y(50))
                    .build());

    left->consumeMotionEvent(WithMotionAction(ACTION_HOVER_EXIT));
    right->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));
    spy->consumeMotionEvent(WithMotionAction(ACTION_HOVER_MOVE));

    ASSERT_FALSE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/0));
    ASSERT_TRUE(mDispatcher->isPointerInWindow(spy->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               DEVICE_ID,
                                               /*pointerId=*/0));
    ASSERT_TRUE(mDispatcher->isPointerInWindow(right->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               DEVICE_ID,
                                               /*pointerId=*/0));

    // Stop hovering.
    mDispatcher->notifyMotion(
            MotionArgsBuilder(ACTION_HOVER_EXIT, AINPUT_SOURCE_STYLUS)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::STYLUS).x(150).y(50))
                    .build());

    right->consumeMotionEvent(WithMotionAction(ACTION_HOVER_EXIT));
    spy->consumeMotionEvent(WithMotionAction(ACTION_HOVER_EXIT));

    ASSERT_FALSE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/0));
    ASSERT_FALSE(mDispatcher->isPointerInWindow(spy->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/0));
    ASSERT_FALSE(mDispatcher->isPointerInWindow(right->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/0));
}

TEST_F(InputDispatcherPointerInWindowTest, PointerInWindowWithSplitTouch) {
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> left = sp<FakeWindowHandle>::make(application, mDispatcher, "Left Window",
                                                           ui::LogicalDisplayId::DEFAULT);
    left->setFrame(Rect(0, 0, 100, 100));
    sp<FakeWindowHandle> right =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Right Window",
                                       ui::LogicalDisplayId::DEFAULT);
    right->setFrame(Rect(100, 0, 200, 100));
    sp<FakeWindowHandle> spy = sp<FakeWindowHandle>::make(application, mDispatcher, "Spy Window",
                                                          ui::LogicalDisplayId::DEFAULT);
    spy->setFrame(Rect(0, 0, 200, 100));
    spy->setTrustedOverlay(true);
    spy->setSpy(true);

    mDispatcher->onWindowInfosChanged(
            {{*spy->getInfo(), *left->getInfo(), *right->getInfo()}, {}, 0, 0});

    // First pointer down on left window.
    mDispatcher->notifyMotion(
            MotionArgsBuilder(ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(50).y(50))
                    .build());

    left->consumeMotionDown();
    spy->consumeMotionDown();

    ASSERT_TRUE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               DEVICE_ID,
                                               /*pointerId=*/0));
    ASSERT_TRUE(mDispatcher->isPointerInWindow(spy->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               DEVICE_ID,
                                               /*pointerId=*/0));
    ASSERT_FALSE(mDispatcher->isPointerInWindow(right->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/0));

    // Second pointer down on right window.
    mDispatcher->notifyMotion(
            MotionArgsBuilder(POINTER_1_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(50).y(50))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(150).y(50))
                    .build());

    left->consumeMotionMove();
    right->consumeMotionDown();
    spy->consumeMotionEvent(WithMotionAction(POINTER_1_DOWN));

    ASSERT_TRUE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               DEVICE_ID,
                                               /*pointerId=*/0));
    ASSERT_TRUE(mDispatcher->isPointerInWindow(spy->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               DEVICE_ID,
                                               /*pointerId=*/0));
    ASSERT_FALSE(mDispatcher->isPointerInWindow(right->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/0));
    ASSERT_FALSE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/1));
    ASSERT_TRUE(mDispatcher->isPointerInWindow(spy->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               DEVICE_ID,
                                               /*pointerId=*/1));
    ASSERT_TRUE(mDispatcher->isPointerInWindow(right->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               DEVICE_ID,
                                               /*pointerId=*/1));

    // Second pointer up.
    mDispatcher->notifyMotion(
            MotionArgsBuilder(POINTER_1_UP, AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(50).y(50))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(150).y(50))
                    .build());

    left->consumeMotionMove();
    right->consumeMotionUp();
    spy->consumeMotionEvent(WithMotionAction(POINTER_1_UP));

    ASSERT_TRUE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               DEVICE_ID,
                                               /*pointerId=*/0));
    ASSERT_TRUE(mDispatcher->isPointerInWindow(spy->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               DEVICE_ID,
                                               /*pointerId=*/0));
    ASSERT_FALSE(mDispatcher->isPointerInWindow(right->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/0));
    ASSERT_FALSE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/1));
    ASSERT_FALSE(mDispatcher->isPointerInWindow(spy->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/1));
    ASSERT_FALSE(mDispatcher->isPointerInWindow(right->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/1));

    // First pointer up.
    mDispatcher->notifyMotion(
            MotionArgsBuilder(ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(50).y(50))
                    .build());

    left->consumeMotionUp();
    spy->consumeMotionUp();

    ASSERT_FALSE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/0));
    ASSERT_FALSE(mDispatcher->isPointerInWindow(spy->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/0));
    ASSERT_FALSE(mDispatcher->isPointerInWindow(right->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/0));
}

TEST_F(InputDispatcherPointerInWindowTest, MultipleDevicesControllingOneMouse_legacy) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, false);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> left = sp<FakeWindowHandle>::make(application, mDispatcher, "Left Window",
                                                           ui::LogicalDisplayId::DEFAULT);
    left->setFrame(Rect(0, 0, 100, 100));
    sp<FakeWindowHandle> right =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Right Window",
                                       ui::LogicalDisplayId::DEFAULT);
    right->setFrame(Rect(100, 0, 200, 100));

    mDispatcher->onWindowInfosChanged({{*left->getInfo(), *right->getInfo()}, {}, 0, 0});

    ASSERT_FALSE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/0));
    ASSERT_FALSE(mDispatcher->isPointerInWindow(right->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/0));

    // Hover move into the window.
    mDispatcher->notifyMotion(
            MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::MOUSE).x(50).y(50))
                    .rawXCursorPosition(50)
                    .rawYCursorPosition(50)
                    .deviceId(DEVICE_ID)
                    .build());

    left->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));

    ASSERT_TRUE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               DEVICE_ID,
                                               /*pointerId=*/0));

    // Move the mouse with another device. This cancels the hovering pointer from the first device.
    mDispatcher->notifyMotion(
            MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::MOUSE).x(51).y(50))
                    .rawXCursorPosition(51)
                    .rawYCursorPosition(50)
                    .deviceId(SECOND_DEVICE_ID)
                    .build());

    left->consumeMotionEvent(WithMotionAction(ACTION_HOVER_EXIT));
    left->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));

    // TODO(b/313689709): InputDispatcher's touch state is not updated, even though the window gets
    // a HOVER_EXIT from the first device.
    ASSERT_TRUE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               DEVICE_ID,
                                               /*pointerId=*/0));
    ASSERT_TRUE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               SECOND_DEVICE_ID,
                                               /*pointerId=*/0));

    // Move the mouse outside the window. Document the current behavior, where the window does not
    // receive HOVER_EXIT even though the mouse left the window.
    mDispatcher->notifyMotion(
            MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::MOUSE).x(150).y(50))
                    .rawXCursorPosition(150)
                    .rawYCursorPosition(50)
                    .deviceId(SECOND_DEVICE_ID)
                    .build());

    left->consumeMotionEvent(WithMotionAction(ACTION_HOVER_EXIT));
    right->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));
    ASSERT_TRUE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               DEVICE_ID,
                                               /*pointerId=*/0));
    ASSERT_FALSE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                SECOND_DEVICE_ID,
                                                /*pointerId=*/0));
}

/**
 * TODO(b/313689709) - correctly support multiple mouse devices, because they should be controlling
 * the same cursor, and therefore have a shared motion event stream.
 */
TEST_F(InputDispatcherPointerInWindowTest, MultipleDevicesControllingOneMouse) {
    SCOPED_FLAG_OVERRIDE(enable_multi_device_same_window_stream, true);
    std::shared_ptr<FakeApplicationHandle> application = std::make_shared<FakeApplicationHandle>();

    sp<FakeWindowHandle> left = sp<FakeWindowHandle>::make(application, mDispatcher, "Left Window",
                                                           ui::LogicalDisplayId::DEFAULT);
    left->setFrame(Rect(0, 0, 100, 100));
    sp<FakeWindowHandle> right =
            sp<FakeWindowHandle>::make(application, mDispatcher, "Right Window",
                                       ui::LogicalDisplayId::DEFAULT);
    right->setFrame(Rect(100, 0, 200, 100));

    mDispatcher->onWindowInfosChanged({{*left->getInfo(), *right->getInfo()}, {}, 0, 0});

    ASSERT_FALSE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/0));
    ASSERT_FALSE(mDispatcher->isPointerInWindow(right->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                DEVICE_ID,
                                                /*pointerId=*/0));

    // Hover move into the window.
    mDispatcher->notifyMotion(
            MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::MOUSE).x(50).y(50))
                    .rawXCursorPosition(50)
                    .rawYCursorPosition(50)
                    .deviceId(DEVICE_ID)
                    .build());

    left->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));

    ASSERT_TRUE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               DEVICE_ID,
                                               /*pointerId=*/0));

    // Move the mouse with another device
    mDispatcher->notifyMotion(
            MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::MOUSE).x(51).y(50))
                    .rawXCursorPosition(51)
                    .rawYCursorPosition(50)
                    .deviceId(SECOND_DEVICE_ID)
                    .build());
    left->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));

    // TODO(b/313689709): InputDispatcher's touch state is not updated, even though the window gets
    // a HOVER_EXIT from the first device.
    ASSERT_TRUE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               DEVICE_ID,
                                               /*pointerId=*/0));
    ASSERT_TRUE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               SECOND_DEVICE_ID,
                                               /*pointerId=*/0));

    // Move the mouse outside the window. Document the current behavior, where the window does not
    // receive HOVER_EXIT even though the mouse left the window.
    mDispatcher->notifyMotion(
            MotionArgsBuilder(ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::MOUSE).x(150).y(50))
                    .rawXCursorPosition(150)
                    .rawYCursorPosition(50)
                    .deviceId(SECOND_DEVICE_ID)
                    .build());

    right->consumeMotionEvent(WithMotionAction(ACTION_HOVER_ENTER));
    ASSERT_TRUE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                               DEVICE_ID,
                                               /*pointerId=*/0));
    ASSERT_FALSE(mDispatcher->isPointerInWindow(left->getToken(), ui::LogicalDisplayId::DEFAULT,
                                                SECOND_DEVICE_ID,
                                                /*pointerId=*/0));
}

TEST_F(InputDispatcherTest, FocusedDisplayChangeIsNotified) {
    mDispatcher->setFocusedDisplay(SECOND_DISPLAY_ID);
    mFakePolicy->assertFocusedDisplayNotified(SECOND_DISPLAY_ID);
}

} // namespace android::inputdispatcher

/*
 * Copyright 2023 The Android Open Source Project
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

#include "../PointerChoreographer.h"
#include <com_android_input_flags.h>
#include <flag_macros.h>
#include <gtest/gtest.h>
#include <deque>
#include <vector>

#include "FakePointerController.h"
#include "InterfaceMocks.h"
#include "NotifyArgsBuilders.h"
#include "TestEventMatchers.h"
#include "TestInputListener.h"

namespace android {

namespace input_flags = com::android::input::flags;

using ControllerType = PointerControllerInterface::ControllerType;
using testing::AllOf;

namespace {

// Helpers to std::visit with lambdas.
template <typename... V>
struct Visitor : V... {
    using V::operator()...;
};
template <typename... V>
Visitor(V...) -> Visitor<V...>;

constexpr int32_t DEVICE_ID = 3;
constexpr int32_t SECOND_DEVICE_ID = DEVICE_ID + 1;
constexpr int32_t THIRD_DEVICE_ID = SECOND_DEVICE_ID + 1;
constexpr ui::LogicalDisplayId DISPLAY_ID = ui::LogicalDisplayId{5};
constexpr ui::LogicalDisplayId ANOTHER_DISPLAY_ID = ui::LogicalDisplayId{10};
constexpr int32_t DISPLAY_WIDTH = 480;
constexpr int32_t DISPLAY_HEIGHT = 800;
constexpr auto DRAWING_TABLET_SOURCE = AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_STYLUS;

const auto MOUSE_POINTER = PointerBuilder(/*id=*/0, ToolType::MOUSE)
                                   .axis(AMOTION_EVENT_AXIS_RELATIVE_X, 10)
                                   .axis(AMOTION_EVENT_AXIS_RELATIVE_Y, 20);
const auto FIRST_TOUCH_POINTER = PointerBuilder(/*id=*/0, ToolType::FINGER).x(100).y(200);
const auto SECOND_TOUCH_POINTER = PointerBuilder(/*id=*/1, ToolType::FINGER).x(200).y(300);
const auto STYLUS_POINTER = PointerBuilder(/*id=*/0, ToolType::STYLUS).x(100).y(200);
const auto TOUCHPAD_POINTER = PointerBuilder(/*id=*/0, ToolType::FINGER)
                                      .axis(AMOTION_EVENT_AXIS_RELATIVE_X, 10)
                                      .axis(AMOTION_EVENT_AXIS_RELATIVE_Y, 20);

static InputDeviceInfo generateTestDeviceInfo(int32_t deviceId, uint32_t source,
                                              ui::LogicalDisplayId associatedDisplayId) {
    InputDeviceIdentifier identifier;

    auto info = InputDeviceInfo();
    info.initialize(deviceId, /*generation=*/1, /*controllerNumber=*/1, identifier, "alias",
                    /*isExternal=*/false, /*hasMic=*/false, associatedDisplayId);
    info.addSource(source);
    return info;
}

static std::vector<DisplayViewport> createViewports(std::vector<ui::LogicalDisplayId> displayIds) {
    std::vector<DisplayViewport> viewports;
    for (auto displayId : displayIds) {
        DisplayViewport viewport;
        viewport.displayId = displayId;
        viewport.logicalRight = DISPLAY_WIDTH;
        viewport.logicalBottom = DISPLAY_HEIGHT;
        viewports.push_back(viewport);
    }
    return viewports;
}

} // namespace

// --- PointerChoreographerTest ---

class TestPointerChoreographer : public PointerChoreographer {
public:
    TestPointerChoreographer(InputListenerInterface& inputListener,
                             PointerChoreographerPolicyInterface& policy,
                             sp<gui::WindowInfosListener>& windowInfoListener,
                             const std::vector<gui::WindowInfo>& mInitialWindowInfos);
};

TestPointerChoreographer::TestPointerChoreographer(
        InputListenerInterface& inputListener, PointerChoreographerPolicyInterface& policy,
        sp<gui::WindowInfosListener>& windowInfoListener,
        const std::vector<gui::WindowInfo>& mInitialWindowInfos)
      : PointerChoreographer(
                inputListener, policy,
                [&windowInfoListener,
                 &mInitialWindowInfos](const sp<android::gui::WindowInfosListener>& listener) {
                    windowInfoListener = listener;
                    return mInitialWindowInfos;
                },
                [&windowInfoListener](const sp<android::gui::WindowInfosListener>& listener) {
                    windowInfoListener = nullptr;
                }) {}

class PointerChoreographerTest : public testing::Test {
protected:
    TestInputListener mTestListener;
    sp<gui::WindowInfosListener> mRegisteredWindowInfoListener;
    std::vector<gui::WindowInfo> mInjectedInitialWindowInfos;
    testing::NiceMock<MockPointerChoreographerPolicyInterface> mMockPolicy;
    TestPointerChoreographer mChoreographer{mTestListener, mMockPolicy,
                                            mRegisteredWindowInfoListener,
                                            mInjectedInitialWindowInfos};

    void SetUp() override {
        // flag overrides
        input_flags::hide_pointer_indicators_for_secure_windows(true);

        ON_CALL(mMockPolicy, createPointerController).WillByDefault([this](ControllerType type) {
            std::shared_ptr<FakePointerController> pc = std::make_shared<FakePointerController>();
            EXPECT_FALSE(pc->isPointerShown());
            mCreatedControllers.emplace_back(type, pc);
            return pc;
        });

        ON_CALL(mMockPolicy, notifyPointerDisplayIdChanged)
                .WillByDefault([this](ui::LogicalDisplayId displayId, const FloatPoint& position) {
                    mPointerDisplayIdNotified = displayId;
                });
    }

    std::shared_ptr<FakePointerController> assertPointerControllerCreated(
            ControllerType expectedType) {
        EXPECT_FALSE(mCreatedControllers.empty()) << "No PointerController was created";
        auto [type, controller] = std::move(mCreatedControllers.front());
        EXPECT_EQ(expectedType, type);
        mCreatedControllers.pop_front();
        return controller;
    }

    void assertPointerControllerNotCreated() { ASSERT_TRUE(mCreatedControllers.empty()); }

    void assertPointerControllerRemoved(const std::shared_ptr<FakePointerController>& pc) {
        // Ensure that the code under test is not holding onto this PointerController.
        // While the policy initially creates the PointerControllers, the PointerChoreographer is
        // expected to manage their lifecycles. Although we may not want to strictly enforce how
        // the object is managed, in this case, we need to have a way of ensuring that the
        // corresponding graphical resources have been released by the PointerController, and the
        // simplest way of checking for that is to just make sure that the PointerControllers
        // themselves are released by Choreographer when no longer in use. This check is ensuring
        // that the reference retained by the test is the last one.
        ASSERT_EQ(1, pc.use_count()) << "Expected PointerChoreographer to release all references "
                                        "to this PointerController";
    }

    void assertPointerControllerNotRemoved(const std::shared_ptr<FakePointerController>& pc) {
        // See assertPointerControllerRemoved above.
        ASSERT_GT(pc.use_count(), 1) << "Expected PointerChoreographer to hold at least one "
                                        "reference to this PointerController";
    }

    void assertPointerDisplayIdNotified(ui::LogicalDisplayId displayId) {
        ASSERT_EQ(displayId, mPointerDisplayIdNotified);
        mPointerDisplayIdNotified.reset();
    }

    void assertPointerDisplayIdNotNotified() { ASSERT_EQ(std::nullopt, mPointerDisplayIdNotified); }

    void assertWindowInfosListenerRegistered() {
        ASSERT_NE(nullptr, mRegisteredWindowInfoListener)
                << "WindowInfosListener was not registered";
    }

    void assertWindowInfosListenerNotRegistered() {
        ASSERT_EQ(nullptr, mRegisteredWindowInfoListener)
                << "WindowInfosListener was not unregistered";
    }

private:
    std::deque<std::pair<ControllerType, std::shared_ptr<FakePointerController>>>
            mCreatedControllers;
    std::optional<ui::LogicalDisplayId> mPointerDisplayIdNotified;
};

TEST_F(PointerChoreographerTest, ForwardsArgsToInnerListener) {
    const std::vector<NotifyArgs>
            allArgs{NotifyInputDevicesChangedArgs{},
                    NotifyConfigurationChangedArgs{},
                    KeyArgsBuilder(AKEY_EVENT_ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).build(),
                    MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                            .pointer(FIRST_TOUCH_POINTER)
                            .build(),
                    NotifySensorArgs{},
                    NotifySwitchArgs{},
                    NotifyDeviceResetArgs{},
                    NotifyPointerCaptureChangedArgs{},
                    NotifyVibratorStateArgs{}};

    for (auto notifyArgs : allArgs) {
        mChoreographer.notify(notifyArgs);
        EXPECT_NO_FATAL_FAILURE(
                std::visit(Visitor{
                                   [&](const NotifyInputDevicesChangedArgs& args) {
                                       mTestListener.assertNotifyInputDevicesChangedWasCalled();
                                   },
                                   [&](const NotifyConfigurationChangedArgs& args) {
                                       mTestListener.assertNotifyConfigurationChangedWasCalled();
                                   },
                                   [&](const NotifyKeyArgs& args) {
                                       mTestListener.assertNotifyKeyWasCalled();
                                   },
                                   [&](const NotifyMotionArgs& args) {
                                       mTestListener.assertNotifyMotionWasCalled();
                                   },
                                   [&](const NotifySensorArgs& args) {
                                       mTestListener.assertNotifySensorWasCalled();
                                   },
                                   [&](const NotifySwitchArgs& args) {
                                       mTestListener.assertNotifySwitchWasCalled();
                                   },
                                   [&](const NotifyDeviceResetArgs& args) {
                                       mTestListener.assertNotifyDeviceResetWasCalled();
                                   },
                                   [&](const NotifyPointerCaptureChangedArgs& args) {
                                       mTestListener.assertNotifyCaptureWasCalled();
                                   },
                                   [&](const NotifyVibratorStateArgs& args) {
                                       mTestListener.assertNotifyVibratorStateWasCalled();
                                   },
                           },
                           notifyArgs));
    }
}

TEST_F(PointerChoreographerTest, WhenMouseIsAddedCreatesPointerController) {
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    assertPointerControllerCreated(ControllerType::MOUSE);
}

TEST_F(PointerChoreographerTest, WhenMouseIsRemovedRemovesPointerController) {
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);

    // Remove the mouse.
    mChoreographer.notifyInputDevicesChanged({/*id=*/1, {}});
    assertPointerControllerRemoved(pc);
}

TEST_F(PointerChoreographerTest, WhenKeyboardIsAddedDoesNotCreatePointerController) {
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_KEYBOARD,
                                     ui::LogicalDisplayId::INVALID)}});
    assertPointerControllerNotCreated();
}

TEST_F(PointerChoreographerTest, SetsViewportForAssociatedMouse) {
    // Just adding a viewport or device should create a PointerController.
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE, DISPLAY_ID)}});

    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    pc->assertViewportSet(DISPLAY_ID);
    ASSERT_TRUE(pc->isPointerShown());
}

TEST_F(PointerChoreographerTest, WhenViewportSetLaterSetsViewportForAssociatedMouse) {
    // Without viewport information, PointerController will be created but viewport won't be set.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE, DISPLAY_ID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    pc->assertViewportNotSet();

    // After Choreographer gets viewport, PointerController should also have viewport.
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    pc->assertViewportSet(DISPLAY_ID);
}

TEST_F(PointerChoreographerTest, SetsDefaultMouseViewportForPointerController) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);

    // For a mouse event without a target display, default viewport should be set for
    // the PointerController.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    pc->assertViewportSet(DISPLAY_ID);
    ASSERT_TRUE(pc->isPointerShown());
}

TEST_F(PointerChoreographerTest,
       WhenDefaultMouseDisplayChangesSetsDefaultMouseViewportForPointerController) {
    // Set one display as a default mouse display and emit mouse event to create PointerController.
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID, ANOTHER_DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    auto firstDisplayPc = assertPointerControllerCreated(ControllerType::MOUSE);
    firstDisplayPc->assertViewportSet(DISPLAY_ID);
    ASSERT_TRUE(firstDisplayPc->isPointerShown());

    // Change default mouse display. Existing PointerController should be removed and a new one
    // should be created.
    mChoreographer.setDefaultMouseDisplayId(ANOTHER_DISPLAY_ID);
    assertPointerControllerRemoved(firstDisplayPc);

    auto secondDisplayPc = assertPointerControllerCreated(ControllerType::MOUSE);
    secondDisplayPc->assertViewportSet(ANOTHER_DISPLAY_ID);
    ASSERT_TRUE(secondDisplayPc->isPointerShown());
}

TEST_F(PointerChoreographerTest, CallsNotifyPointerDisplayIdChanged) {
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    assertPointerControllerCreated(ControllerType::MOUSE);

    assertPointerDisplayIdNotified(DISPLAY_ID);
}

TEST_F(PointerChoreographerTest, WhenViewportIsSetLaterCallsNotifyPointerDisplayIdChanged) {
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    assertPointerControllerCreated(ControllerType::MOUSE);
    assertPointerDisplayIdNotNotified();

    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    assertPointerDisplayIdNotified(DISPLAY_ID);
}

TEST_F(PointerChoreographerTest, WhenMouseIsRemovedCallsNotifyPointerDisplayIdChanged) {
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    assertPointerDisplayIdNotified(DISPLAY_ID);

    mChoreographer.notifyInputDevicesChanged({/*id=*/1, {}});
    assertPointerDisplayIdNotified(ui::LogicalDisplayId::INVALID);
    assertPointerControllerRemoved(pc);
}

TEST_F(PointerChoreographerTest, WhenDefaultMouseDisplayChangesCallsNotifyPointerDisplayIdChanged) {
    // Add two viewports.
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID, ANOTHER_DISPLAY_ID}));

    // Set one viewport as a default mouse display ID.
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    auto firstDisplayPc = assertPointerControllerCreated(ControllerType::MOUSE);
    assertPointerDisplayIdNotified(DISPLAY_ID);

    // Set another viewport as a default mouse display ID. The mouse is moved to the other display.
    mChoreographer.setDefaultMouseDisplayId(ANOTHER_DISPLAY_ID);
    assertPointerControllerRemoved(firstDisplayPc);

    assertPointerControllerCreated(ControllerType::MOUSE);
    assertPointerDisplayIdNotified(ANOTHER_DISPLAY_ID);
}

TEST_F(PointerChoreographerTest, MouseMovesPointerAndReturnsNewArgs) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_EQ(DISPLAY_ID, pc->getDisplayId());

    // Set initial position of the PointerController.
    pc->setPosition(100, 200);

    // Make NotifyMotionArgs and notify Choreographer.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                    .pointer(MOUSE_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(ui::LogicalDisplayId::INVALID)
                    .build());

    // Check that the PointerController updated the position and the pointer is shown.
    pc->assertPosition(110, 220);
    ASSERT_TRUE(pc->isPointerShown());

    // Check that x-y coordinates, displayId and cursor position are correctly updated.
    mTestListener.assertNotifyMotionWasCalled(
            AllOf(WithCoords(110, 220), WithDisplayId(DISPLAY_ID), WithCursorPosition(110, 220)));
}

TEST_F(PointerChoreographerTest, AbsoluteMouseMovesPointerAndReturnsNewArgs) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_EQ(DISPLAY_ID, pc->getDisplayId());

    // Set initial position of the PointerController.
    pc->setPosition(100, 200);
    const auto absoluteMousePointer = PointerBuilder(/*id=*/0, ToolType::MOUSE)
                                              .axis(AMOTION_EVENT_AXIS_X, 110)
                                              .axis(AMOTION_EVENT_AXIS_Y, 220);

    // Make NotifyMotionArgs and notify Choreographer.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                    .pointer(absoluteMousePointer)
                    .deviceId(DEVICE_ID)
                    .displayId(ui::LogicalDisplayId::INVALID)
                    .build());

    // Check that the PointerController updated the position and the pointer is shown.
    pc->assertPosition(110, 220);
    ASSERT_TRUE(pc->isPointerShown());

    // Check that x-y coordinates, displayId and cursor position are correctly updated.
    mTestListener.assertNotifyMotionWasCalled(
            AllOf(WithCoords(110, 220), WithRelativeMotion(10, 20), WithDisplayId(DISPLAY_ID),
                  WithCursorPosition(110, 220)));
}

TEST_F(PointerChoreographerTest,
       AssociatedMouseMovesPointerOnAssociatedDisplayAndDoesNotMovePointerOnDefaultDisplay) {
    // Add two displays and set one to default.
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID, ANOTHER_DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);

    // Add two devices, one unassociated and the other associated with non-default mouse display.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE, ui::LogicalDisplayId::INVALID),
              generateTestDeviceInfo(SECOND_DEVICE_ID, AINPUT_SOURCE_MOUSE, ANOTHER_DISPLAY_ID)}});
    auto unassociatedMousePc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_EQ(DISPLAY_ID, unassociatedMousePc->getDisplayId());
    auto associatedMousePc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_EQ(ANOTHER_DISPLAY_ID, associatedMousePc->getDisplayId());

    // Set initial position for PointerControllers.
    unassociatedMousePc->setPosition(100, 200);
    associatedMousePc->setPosition(300, 400);

    // Make NotifyMotionArgs from the associated mouse and notify Choreographer.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                    .pointer(MOUSE_POINTER)
                    .deviceId(SECOND_DEVICE_ID)
                    .displayId(ANOTHER_DISPLAY_ID)
                    .build());

    // Check the status of the PointerControllers.
    unassociatedMousePc->assertPosition(100, 200);
    ASSERT_EQ(DISPLAY_ID, unassociatedMousePc->getDisplayId());
    associatedMousePc->assertPosition(310, 420);
    ASSERT_EQ(ANOTHER_DISPLAY_ID, associatedMousePc->getDisplayId());
    ASSERT_TRUE(associatedMousePc->isPointerShown());

    // Check that x-y coordinates, displayId and cursor position are correctly updated.
    mTestListener.assertNotifyMotionWasCalled(
            AllOf(WithCoords(310, 420), WithDeviceId(SECOND_DEVICE_ID),
                  WithDisplayId(ANOTHER_DISPLAY_ID), WithCursorPosition(310, 420)));
}

TEST_F(PointerChoreographerTest, DoesNotMovePointerForMouseRelativeSource) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_EQ(DISPLAY_ID, pc->getDisplayId());

    // Set initial position of the PointerController.
    pc->setPosition(100, 200);

    // Assume that pointer capture is enabled.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/1,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE_RELATIVE,
                                     ui::LogicalDisplayId::INVALID)}});
    mChoreographer.notifyPointerCaptureChanged(
            NotifyPointerCaptureChangedArgs(/*id=*/2, systemTime(SYSTEM_TIME_MONOTONIC),
                                            PointerCaptureRequest(/*window=*/sp<BBinder>::make(),
                                                                  /*seq=*/0)));

    // Notify motion as if pointer capture is enabled.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_MOUSE_RELATIVE)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::MOUSE)
                                     .x(10)
                                     .y(20)
                                     .axis(AMOTION_EVENT_AXIS_RELATIVE_X, 10)
                                     .axis(AMOTION_EVENT_AXIS_RELATIVE_Y, 20))
                    .deviceId(DEVICE_ID)
                    .displayId(ui::LogicalDisplayId::INVALID)
                    .build());

    // Check that there's no update on the PointerController.
    pc->assertPosition(100, 200);
    ASSERT_FALSE(pc->isPointerShown());

    // Check x-y coordinates, displayId and cursor position are not changed.
    mTestListener.assertNotifyMotionWasCalled(
            AllOf(WithCoords(10, 20), WithRelativeMotion(10, 20),
                  WithDisplayId(ui::LogicalDisplayId::INVALID),
                  WithCursorPosition(AMOTION_EVENT_INVALID_CURSOR_POSITION,
                                     AMOTION_EVENT_INVALID_CURSOR_POSITION)));
}

TEST_F(PointerChoreographerTest, WhenPointerCaptureEnabledHidesPointer) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_EQ(DISPLAY_ID, pc->getDisplayId());
    ASSERT_TRUE(pc->isPointerShown());

    // Enable pointer capture and check if the PointerController hid the pointer.
    mChoreographer.notifyPointerCaptureChanged(
            NotifyPointerCaptureChangedArgs(/*id=*/1, systemTime(SYSTEM_TIME_MONOTONIC),
                                            PointerCaptureRequest(/*window=*/sp<BBinder>::make(),
                                                                  /*seq=*/0)));
    ASSERT_FALSE(pc->isPointerShown());
}

TEST_F(PointerChoreographerTest, MultipleMiceConnectionAndRemoval) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);

    // A mouse is connected, and the pointer is shown.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});

    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_TRUE(pc->isPointerShown());

    pc->fade(PointerControllerInterface::Transition::IMMEDIATE);

    // Add a second mouse is added, the pointer is shown again.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE, ui::LogicalDisplayId::INVALID),
              generateTestDeviceInfo(SECOND_DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    ASSERT_TRUE(pc->isPointerShown());

    // One of the mice is removed, and it does not cause the mouse pointer to fade, because
    // we have one more mouse connected.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(SECOND_DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    assertPointerControllerNotRemoved(pc);
    ASSERT_TRUE(pc->isPointerShown());

    // The final mouse is removed. The pointer is removed.
    mChoreographer.notifyInputDevicesChanged({/*id=*/0, {}});
    assertPointerControllerRemoved(pc);
}

TEST_F(PointerChoreographerTest, UnrelatedChangeDoesNotUnfadePointer) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});

    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_TRUE(pc->isPointerShown());

    pc->fade(PointerControllerInterface::Transition::IMMEDIATE);

    // Adding a touchscreen device does not unfade the mouse pointer.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE, ui::LogicalDisplayId::INVALID),
              generateTestDeviceInfo(SECOND_DEVICE_ID,
                                     AINPUT_SOURCE_TOUCHSCREEN | AINPUT_SOURCE_STYLUS,
                                     DISPLAY_ID)}});

    ASSERT_FALSE(pc->isPointerShown());

    // Show touches setting change does not unfade the mouse pointer.
    mChoreographer.setShowTouchesEnabled(true);

    ASSERT_FALSE(pc->isPointerShown());
}

TEST_F(PointerChoreographerTest, DisabledMouseConnected) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    InputDeviceInfo mouseDeviceInfo =
            generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE, ui::LogicalDisplayId::INVALID);
    // Disable this mouse device.
    mouseDeviceInfo.setEnabled(false);
    mChoreographer.notifyInputDevicesChanged({/*id=*/0, {mouseDeviceInfo}});

    // Disabled mouse device should not create PointerController
    assertPointerControllerNotCreated();
}

TEST_F(PointerChoreographerTest, MouseDeviceDisableLater) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    InputDeviceInfo mouseDeviceInfo =
            generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE, ui::LogicalDisplayId::INVALID);

    mChoreographer.notifyInputDevicesChanged({/*id=*/0, {mouseDeviceInfo}});

    auto pc = assertPointerControllerCreated(PointerControllerInterface::ControllerType::MOUSE);
    ASSERT_TRUE(pc->isPointerShown());

    // Now we disable this mouse device
    mouseDeviceInfo.setEnabled(false);
    mChoreographer.notifyInputDevicesChanged({/*id=*/0, {mouseDeviceInfo}});

    // Because the mouse device disabled, the PointerController should be removed.
    assertPointerControllerRemoved(pc);
}

TEST_F(PointerChoreographerTest, MultipleEnabledAndDisabledMiceConnectionAndRemoval) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    InputDeviceInfo disabledMouseDeviceInfo =
            generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE, ui::LogicalDisplayId::INVALID);
    disabledMouseDeviceInfo.setEnabled(false);

    InputDeviceInfo enabledMouseDeviceInfo =
            generateTestDeviceInfo(SECOND_DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                   ui::LogicalDisplayId::INVALID);

    InputDeviceInfo anotherEnabledMouseDeviceInfo =
            generateTestDeviceInfo(THIRD_DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                   ui::LogicalDisplayId::INVALID);

    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {disabledMouseDeviceInfo, enabledMouseDeviceInfo, anotherEnabledMouseDeviceInfo}});

    // Mouse should show, because we have two enabled mice device.
    auto pc = assertPointerControllerCreated(PointerControllerInterface::ControllerType::MOUSE);
    ASSERT_TRUE(pc->isPointerShown());

    // Now we remove one of enabled mice device.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {disabledMouseDeviceInfo, enabledMouseDeviceInfo}});

    // Because we still have an enabled mouse device, pointer should still show.
    ASSERT_TRUE(pc->isPointerShown());

    // We finally remove last enabled mouse device.
    mChoreographer.notifyInputDevicesChanged({/*id=*/0, {disabledMouseDeviceInfo}});

    // The PointerController should be removed, because there is no enabled mouse device.
    assertPointerControllerRemoved(pc);
}

TEST_F(PointerChoreographerTest, WhenShowTouchesEnabledAndDisabledDoesNotCreatePointerController) {
    // Disable show touches and add a touch device.
    mChoreographer.setShowTouchesEnabled(false);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_TOUCHSCREEN, DISPLAY_ID)}});
    assertPointerControllerNotCreated();

    // Enable show touches. PointerController still should not be created.
    mChoreographer.setShowTouchesEnabled(true);
    assertPointerControllerNotCreated();
}

TEST_F(PointerChoreographerTest, WhenTouchEventOccursCreatesPointerController) {
    // Add a touch device and enable show touches.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_TOUCHSCREEN, DISPLAY_ID)}});
    mChoreographer.setShowTouchesEnabled(true);

    // Emit touch event. Now PointerController should be created.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(FIRST_TOUCH_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    assertPointerControllerCreated(ControllerType::TOUCH);
}

TEST_F(PointerChoreographerTest,
       WhenShowTouchesDisabledAndTouchEventOccursDoesNotCreatePointerController) {
    // Add a touch device and disable show touches.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_TOUCHSCREEN, DISPLAY_ID)}});
    mChoreographer.setShowTouchesEnabled(false);
    assertPointerControllerNotCreated();

    // Emit touch event. Still, PointerController should not be created.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(FIRST_TOUCH_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    assertPointerControllerNotCreated();
}

TEST_F(PointerChoreographerTest, WhenTouchDeviceIsRemovedRemovesPointerController) {
    // Make sure the PointerController is created.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_TOUCHSCREEN, DISPLAY_ID)}});
    mChoreographer.setShowTouchesEnabled(true);
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(FIRST_TOUCH_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    auto pc = assertPointerControllerCreated(ControllerType::TOUCH);

    // Remove the device.
    mChoreographer.notifyInputDevicesChanged({/*id=*/1, {}});
    assertPointerControllerRemoved(pc);
}

TEST_F(PointerChoreographerTest, WhenShowTouchesDisabledRemovesPointerController) {
    // Make sure the PointerController is created.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_TOUCHSCREEN, DISPLAY_ID)}});
    mChoreographer.setShowTouchesEnabled(true);
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(FIRST_TOUCH_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    auto pc = assertPointerControllerCreated(ControllerType::TOUCH);

    // Disable show touches.
    mChoreographer.setShowTouchesEnabled(false);
    assertPointerControllerRemoved(pc);
}

TEST_F(PointerChoreographerTest, TouchSetsSpots) {
    mChoreographer.setShowTouchesEnabled(true);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_TOUCHSCREEN, DISPLAY_ID)}});

    // Emit first pointer down.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(FIRST_TOUCH_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    auto pc = assertPointerControllerCreated(ControllerType::TOUCH);
    pc->assertSpotCount(DISPLAY_ID, 1);

    // Emit second pointer down.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_POINTER_DOWN |
                                      (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT),
                              AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(FIRST_TOUCH_POINTER)
                    .pointer(SECOND_TOUCH_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    pc->assertSpotCount(DISPLAY_ID, 2);

    // Emit second pointer up.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_POINTER_UP |
                                      (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT),
                              AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(FIRST_TOUCH_POINTER)
                    .pointer(SECOND_TOUCH_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    pc->assertSpotCount(DISPLAY_ID, 1);

    // Emit first pointer up.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(FIRST_TOUCH_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    pc->assertSpotCount(DISPLAY_ID, 0);
}

TEST_F(PointerChoreographerTest, TouchSetsSpotsForStylusEvent) {
    mChoreographer.setShowTouchesEnabled(true);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_TOUCHSCREEN | AINPUT_SOURCE_STYLUS,
                                     DISPLAY_ID)}});

    // Emit down event with stylus properties.
    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN,
                                                  AINPUT_SOURCE_TOUCHSCREEN | AINPUT_SOURCE_STYLUS)
                                        .pointer(STYLUS_POINTER)
                                        .deviceId(DEVICE_ID)
                                        .displayId(DISPLAY_ID)
                                        .build());
    auto pc = assertPointerControllerCreated(ControllerType::TOUCH);
    pc->assertSpotCount(DISPLAY_ID, 1);
}

TEST_F(PointerChoreographerTest, TouchSetsSpotsForTwoDisplays) {
    mChoreographer.setShowTouchesEnabled(true);
    // Add two touch devices associated to different displays.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_TOUCHSCREEN, DISPLAY_ID),
              generateTestDeviceInfo(SECOND_DEVICE_ID, AINPUT_SOURCE_TOUCHSCREEN,
                                     ANOTHER_DISPLAY_ID)}});

    // Emit touch event with first device.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(FIRST_TOUCH_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    auto firstDisplayPc = assertPointerControllerCreated(ControllerType::TOUCH);
    firstDisplayPc->assertSpotCount(DISPLAY_ID, 1);

    // Emit touch events with second device.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(FIRST_TOUCH_POINTER)
                    .deviceId(SECOND_DEVICE_ID)
                    .displayId(ANOTHER_DISPLAY_ID)
                    .build());
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_POINTER_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(FIRST_TOUCH_POINTER)
                    .pointer(SECOND_TOUCH_POINTER)
                    .deviceId(SECOND_DEVICE_ID)
                    .displayId(ANOTHER_DISPLAY_ID)
                    .build());

    // There should be another PointerController created.
    auto secondDisplayPc = assertPointerControllerCreated(ControllerType::TOUCH);

    // Check if the spots are set for the second device.
    secondDisplayPc->assertSpotCount(ANOTHER_DISPLAY_ID, 2);

    // Check if there's no change on the spot of the first device.
    firstDisplayPc->assertSpotCount(DISPLAY_ID, 1);
}

TEST_F(PointerChoreographerTest, WhenTouchDeviceIsResetClearsSpots) {
    // Make sure the PointerController is created and there is a spot.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_TOUCHSCREEN, DISPLAY_ID)}});
    mChoreographer.setShowTouchesEnabled(true);
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(FIRST_TOUCH_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    auto pc = assertPointerControllerCreated(ControllerType::TOUCH);
    pc->assertSpotCount(DISPLAY_ID, 1);

    // Reset the device and ensure the touch pointer controller was removed.
    mChoreographer.notifyDeviceReset(NotifyDeviceResetArgs(/*id=*/1, /*eventTime=*/0, DEVICE_ID));
    assertPointerControllerRemoved(pc);
}

using StylusFixtureParam =
        std::tuple</*name*/ std::string_view, /*source*/ uint32_t, ControllerType>;

class StylusTestFixture : public PointerChoreographerTest,
                          public ::testing::WithParamInterface<StylusFixtureParam> {};

INSTANTIATE_TEST_SUITE_P(PointerChoreographerTest, StylusTestFixture,
                         ::testing::Values(std::make_tuple("DirectStylus", AINPUT_SOURCE_STYLUS,
                                                           ControllerType::STYLUS),
                                           std::make_tuple("DrawingTablet", DRAWING_TABLET_SOURCE,
                                                           ControllerType::MOUSE)),
                         [](const testing::TestParamInfo<StylusFixtureParam>& p) {
                             return std::string{std::get<0>(p.param)};
                         });

TEST_P(StylusTestFixture, WhenStylusPointerIconEnabledAndDisabledDoesNotCreatePointerController) {
    const auto& [name, source, controllerType] = GetParam();

    // Disable stylus pointer icon and add a stylus device.
    mChoreographer.setStylusPointerIconEnabled(false);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, source, DISPLAY_ID)}});
    assertPointerControllerNotCreated();

    // Enable stylus pointer icon. PointerController still should not be created.
    mChoreographer.setStylusPointerIconEnabled(true);
    assertPointerControllerNotCreated();
}

TEST_P(StylusTestFixture, WhenStylusHoverEventOccursCreatesPointerController) {
    const auto& [name, source, controllerType] = GetParam();

    // Add a stylus device and enable stylus pointer icon.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, source, DISPLAY_ID)}});
    mChoreographer.setStylusPointerIconEnabled(true);
    assertPointerControllerNotCreated();

    // Emit hover event. Now PointerController should be created.
    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, source)
                                        .pointer(STYLUS_POINTER)
                                        .deviceId(DEVICE_ID)
                                        .displayId(DISPLAY_ID)
                                        .build());
    assertPointerControllerCreated(controllerType);
}

TEST_F(PointerChoreographerTest, StylusHoverEventWhenStylusPointerIconDisabled) {
    // Add a stylus device and disable stylus pointer icon.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_STYLUS, DISPLAY_ID)}});
    mChoreographer.setStylusPointerIconEnabled(false);
    assertPointerControllerNotCreated();

    // Emit hover event. Still, PointerController should not be created.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                    .pointer(STYLUS_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    assertPointerControllerNotCreated();
}

TEST_F(PointerChoreographerTest, DrawingTabletHoverEventWhenStylusPointerIconDisabled) {
    // Add a drawing tablet and disable stylus pointer icon.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, DRAWING_TABLET_SOURCE, DISPLAY_ID)}});
    mChoreographer.setStylusPointerIconEnabled(false);
    assertPointerControllerNotCreated();

    // Emit hover event. Drawing tablets are not affected by "stylus pointer icon" setting.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, DRAWING_TABLET_SOURCE)
                    .pointer(STYLUS_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    assertPointerControllerCreated(ControllerType::MOUSE);
}

TEST_P(StylusTestFixture, WhenStylusDeviceIsRemovedRemovesPointerController) {
    const auto& [name, source, controllerType] = GetParam();

    // Make sure the PointerController is created.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, source, DISPLAY_ID)}});
    mChoreographer.setStylusPointerIconEnabled(true);
    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, source)
                                        .pointer(STYLUS_POINTER)
                                        .deviceId(DEVICE_ID)
                                        .displayId(DISPLAY_ID)
                                        .build());
    auto pc = assertPointerControllerCreated(controllerType);

    // Remove the device.
    mChoreographer.notifyInputDevicesChanged({/*id=*/1, {}});
    assertPointerControllerRemoved(pc);
}

TEST_F(PointerChoreographerTest, StylusPointerIconDisabledRemovesPointerController) {
    // Make sure the PointerController is created.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_STYLUS, DISPLAY_ID)}});
    mChoreographer.setStylusPointerIconEnabled(true);
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, AINPUT_SOURCE_STYLUS)
                    .pointer(STYLUS_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    auto pc = assertPointerControllerCreated(ControllerType::STYLUS);

    // Disable stylus pointer icon.
    mChoreographer.setStylusPointerIconEnabled(false);
    assertPointerControllerRemoved(pc);
}

TEST_F(PointerChoreographerTest,
       StylusPointerIconDisabledDoesNotRemoveDrawingTabletPointerController) {
    // Make sure the PointerController is created.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, DRAWING_TABLET_SOURCE, DISPLAY_ID)}});
    mChoreographer.setStylusPointerIconEnabled(true);
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, DRAWING_TABLET_SOURCE)
                    .pointer(STYLUS_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);

    // Disable stylus pointer icon. This should not affect drawing tablets.
    mChoreographer.setStylusPointerIconEnabled(false);
    assertPointerControllerNotRemoved(pc);
}

TEST_P(StylusTestFixture, SetsViewportForStylusPointerController) {
    const auto& [name, source, controllerType] = GetParam();

    // Set viewport.
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));

    // Make sure the PointerController is created.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, source, DISPLAY_ID)}});
    mChoreographer.setStylusPointerIconEnabled(true);
    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, source)
                                        .pointer(STYLUS_POINTER)
                                        .deviceId(DEVICE_ID)
                                        .displayId(DISPLAY_ID)
                                        .build());
    auto pc = assertPointerControllerCreated(controllerType);

    // Check that viewport is set for the PointerController.
    pc->assertViewportSet(DISPLAY_ID);
}

TEST_P(StylusTestFixture, WhenViewportIsSetLaterSetsViewportForStylusPointerController) {
    const auto& [name, source, controllerType] = GetParam();

    // Make sure the PointerController is created.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, source, DISPLAY_ID)}});
    mChoreographer.setStylusPointerIconEnabled(true);
    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, source)
                                        .pointer(STYLUS_POINTER)
                                        .deviceId(DEVICE_ID)
                                        .displayId(DISPLAY_ID)
                                        .build());
    auto pc = assertPointerControllerCreated(controllerType);

    // Check that viewport is unset.
    pc->assertViewportNotSet();

    // Set viewport.
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));

    // Check that the viewport is set for the PointerController.
    pc->assertViewportSet(DISPLAY_ID);
}

TEST_P(StylusTestFixture, WhenViewportDoesNotMatchDoesNotSetViewportForStylusPointerController) {
    const auto& [name, source, controllerType] = GetParam();

    // Make sure the PointerController is created.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, source, DISPLAY_ID)}});
    mChoreographer.setStylusPointerIconEnabled(true);
    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, source)
                                        .pointer(STYLUS_POINTER)
                                        .deviceId(DEVICE_ID)
                                        .displayId(DISPLAY_ID)
                                        .build());
    auto pc = assertPointerControllerCreated(controllerType);

    // Check that viewport is unset.
    pc->assertViewportNotSet();

    // Set viewport which does not match the associated display of the stylus.
    mChoreographer.setDisplayViewports(createViewports({ANOTHER_DISPLAY_ID}));

    // Check that viewport is still unset.
    pc->assertViewportNotSet();
}

TEST_P(StylusTestFixture, StylusHoverManipulatesPointer) {
    const auto& [name, source, controllerType] = GetParam();

    mChoreographer.setStylusPointerIconEnabled(true);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, source, DISPLAY_ID)}});
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));

    // Emit hover enter event. This is for creating PointerController.
    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, source)
                                        .pointer(STYLUS_POINTER)
                                        .deviceId(DEVICE_ID)
                                        .displayId(DISPLAY_ID)
                                        .build());
    auto pc = assertPointerControllerCreated(controllerType);

    // Emit hover move event. After bounds are set, PointerController will update the position.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE, source)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::STYLUS).x(150).y(250))
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    pc->assertPosition(150, 250);
    ASSERT_TRUE(pc->isPointerShown());

    // Emit hover exit event.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_EXIT, source)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::STYLUS).x(150).y(250))
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    // Check that the pointer is gone.
    ASSERT_FALSE(pc->isPointerShown());
}

TEST_P(StylusTestFixture, StylusHoverManipulatesPointerForTwoDisplays) {
    const auto& [name, source, controllerType] = GetParam();

    mChoreographer.setStylusPointerIconEnabled(true);
    // Add two stylus devices associated to different displays.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, source, DISPLAY_ID),
              generateTestDeviceInfo(SECOND_DEVICE_ID, source, ANOTHER_DISPLAY_ID)}});
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID, ANOTHER_DISPLAY_ID}));

    // Emit hover event with first device. This is for creating PointerController.
    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, source)
                                        .pointer(STYLUS_POINTER)
                                        .deviceId(DEVICE_ID)
                                        .displayId(DISPLAY_ID)
                                        .build());
    auto firstDisplayPc = assertPointerControllerCreated(controllerType);

    // Emit hover event with second device. This is for creating PointerController.
    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, source)
                                        .pointer(STYLUS_POINTER)
                                        .deviceId(SECOND_DEVICE_ID)
                                        .displayId(ANOTHER_DISPLAY_ID)
                                        .build());

    // There should be another PointerController created.
    auto secondDisplayPc = assertPointerControllerCreated(controllerType);

    // Emit hover event with first device.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE, source)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::STYLUS).x(150).y(250))
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());

    // Check the pointer of the first device.
    firstDisplayPc->assertPosition(150, 250);
    ASSERT_TRUE(firstDisplayPc->isPointerShown());

    // Emit hover event with second device.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE, source)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::STYLUS).x(250).y(350))
                    .deviceId(SECOND_DEVICE_ID)
                    .displayId(ANOTHER_DISPLAY_ID)
                    .build());

    // Check the pointer of the second device.
    secondDisplayPc->assertPosition(250, 350);
    ASSERT_TRUE(secondDisplayPc->isPointerShown());

    // Check that there's no change on the pointer of the first device.
    firstDisplayPc->assertPosition(150, 250);
    ASSERT_TRUE(firstDisplayPc->isPointerShown());
}

TEST_P(StylusTestFixture, WhenStylusDeviceIsResetRemovesPointer) {
    const auto& [name, source, controllerType] = GetParam();

    // Make sure the PointerController is created and there is a pointer.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, source, DISPLAY_ID)}});
    mChoreographer.setStylusPointerIconEnabled(true);
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, source)
                                        .pointer(STYLUS_POINTER)
                                        .deviceId(DEVICE_ID)
                                        .displayId(DISPLAY_ID)
                                        .build());
    auto pc = assertPointerControllerCreated(controllerType);
    ASSERT_TRUE(pc->isPointerShown());

    // Reset the device and see the pointer controller was removed.
    mChoreographer.notifyDeviceReset(NotifyDeviceResetArgs(/*id=*/1, /*eventTime=*/0, DEVICE_ID));
    assertPointerControllerRemoved(pc);
}

TEST_F(PointerChoreographerTest, WhenTouchpadIsAddedCreatesPointerController) {
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_TOUCHPAD,
                                     ui::LogicalDisplayId::INVALID)}});
    assertPointerControllerCreated(ControllerType::MOUSE);
}

TEST_F(PointerChoreographerTest, WhenTouchpadIsRemovedRemovesPointerController) {
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_TOUCHPAD,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);

    // Remove the touchpad.
    mChoreographer.notifyInputDevicesChanged({/*id=*/1, {}});
    assertPointerControllerRemoved(pc);
}

TEST_F(PointerChoreographerTest, SetsViewportForAssociatedTouchpad) {
    // Just adding a viewport or device should not create a PointerController.
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_TOUCHPAD,
                                     DISPLAY_ID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    pc->assertViewportSet(DISPLAY_ID);
}

TEST_F(PointerChoreographerTest, WhenViewportSetLaterSetsViewportForAssociatedTouchpad) {
    // Without viewport information, PointerController will be created by a touchpad event
    // but viewport won't be set.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_TOUCHPAD,
                                     DISPLAY_ID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    pc->assertViewportNotSet();

    // After Choreographer gets viewport, PointerController should also have viewport.
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    pc->assertViewportSet(DISPLAY_ID);
}

TEST_F(PointerChoreographerTest, SetsDefaultTouchpadViewportForPointerController) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);

    // For a touchpad event without a target display, default viewport should be set for
    // the PointerController.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    pc->assertViewportSet(DISPLAY_ID);
}

TEST_F(PointerChoreographerTest,
       WhenDefaultTouchpadDisplayChangesSetsDefaultTouchpadViewportForPointerController) {
    // Set one display as a default touchpad display and create PointerController.
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID, ANOTHER_DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_TOUCHPAD,
                                     ui::LogicalDisplayId::INVALID)}});
    auto firstDisplayPc = assertPointerControllerCreated(ControllerType::MOUSE);
    firstDisplayPc->assertViewportSet(DISPLAY_ID);

    // Change default mouse display. Existing PointerController should be removed.
    mChoreographer.setDefaultMouseDisplayId(ANOTHER_DISPLAY_ID);
    assertPointerControllerRemoved(firstDisplayPc);

    auto secondDisplayPc = assertPointerControllerCreated(ControllerType::MOUSE);
    secondDisplayPc->assertViewportSet(ANOTHER_DISPLAY_ID);
}

TEST_F(PointerChoreographerTest, TouchpadCallsNotifyPointerDisplayIdChanged) {
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_TOUCHPAD,
                                     ui::LogicalDisplayId::INVALID)}});
    assertPointerControllerCreated(ControllerType::MOUSE);

    assertPointerDisplayIdNotified(DISPLAY_ID);
}

TEST_F(PointerChoreographerTest, WhenViewportIsSetLaterTouchpadCallsNotifyPointerDisplayIdChanged) {
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_TOUCHPAD,
                                     ui::LogicalDisplayId::INVALID)}});
    assertPointerControllerCreated(ControllerType::MOUSE);
    assertPointerDisplayIdNotNotified();

    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    assertPointerDisplayIdNotified(DISPLAY_ID);
}

TEST_F(PointerChoreographerTest, WhenTouchpadIsRemovedCallsNotifyPointerDisplayIdChanged) {
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_TOUCHPAD,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    assertPointerDisplayIdNotified(DISPLAY_ID);

    mChoreographer.notifyInputDevicesChanged({/*id=*/1, {}});
    assertPointerDisplayIdNotified(ui::LogicalDisplayId::INVALID);
    assertPointerControllerRemoved(pc);
}

TEST_F(PointerChoreographerTest,
       WhenDefaultMouseDisplayChangesTouchpadCallsNotifyPointerDisplayIdChanged) {
    // Add two viewports.
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID, ANOTHER_DISPLAY_ID}));

    // Set one viewport as a default mouse display ID.
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_TOUCHPAD,
                                     ui::LogicalDisplayId::INVALID)}});
    auto firstDisplayPc = assertPointerControllerCreated(ControllerType::MOUSE);
    assertPointerDisplayIdNotified(DISPLAY_ID);

    // Set another viewport as a default mouse display ID. ui::LogicalDisplayId::INVALID will be
    // notified before a touchpad event.
    mChoreographer.setDefaultMouseDisplayId(ANOTHER_DISPLAY_ID);
    assertPointerControllerRemoved(firstDisplayPc);

    assertPointerControllerCreated(ControllerType::MOUSE);
    assertPointerDisplayIdNotified(ANOTHER_DISPLAY_ID);
}

TEST_F(PointerChoreographerTest, TouchpadMovesPointerAndReturnsNewArgs) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_TOUCHPAD,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_EQ(DISPLAY_ID, pc->getDisplayId());

    // Set initial position of the PointerController.
    pc->setPosition(100, 200);

    // Make NotifyMotionArgs and notify Choreographer.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                    .pointer(TOUCHPAD_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(ui::LogicalDisplayId::INVALID)
                    .build());

    // Check that the PointerController updated the position and the pointer is shown.
    pc->assertPosition(110, 220);
    ASSERT_TRUE(pc->isPointerShown());

    // Check that x-y coordinates, displayId and cursor position are correctly updated.
    mTestListener.assertNotifyMotionWasCalled(
            AllOf(WithCoords(110, 220), WithDisplayId(DISPLAY_ID), WithCursorPosition(110, 220)));
}

TEST_F(PointerChoreographerTest, TouchpadAddsPointerPositionToTheCoords) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_TOUCHPAD,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_EQ(DISPLAY_ID, pc->getDisplayId());

    // Set initial position of the PointerController.
    pc->setPosition(100, 200);

    // Notify motion with fake fingers, as if it is multi-finger swipe.
    // Check if the position of the PointerController is added to the fake finger coords.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_MOUSE)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(-100).y(0))
                    .classification(MotionClassification::MULTI_FINGER_SWIPE)
                    .deviceId(DEVICE_ID)
                    .displayId(ui::LogicalDisplayId::INVALID)
                    .build());
    mTestListener.assertNotifyMotionWasCalled(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_DOWN),
                  WithMotionClassification(MotionClassification::MULTI_FINGER_SWIPE),
                  WithCoords(0, 200), WithCursorPosition(100, 200)));
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_POINTER_DOWN |
                                      (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT),
                              AINPUT_SOURCE_MOUSE)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(-100).y(0))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(0).y(0))
                    .classification(MotionClassification::MULTI_FINGER_SWIPE)
                    .deviceId(DEVICE_ID)
                    .displayId(ui::LogicalDisplayId::INVALID)
                    .build());
    mTestListener.assertNotifyMotionWasCalled(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_POINTER_DOWN |
                                   (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT)),
                  WithMotionClassification(MotionClassification::MULTI_FINGER_SWIPE),
                  WithPointerCoords(0, 0, 200), WithPointerCoords(1, 100, 200),
                  WithCursorPosition(100, 200)));
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_POINTER_DOWN |
                                      (2 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT),
                              AINPUT_SOURCE_MOUSE)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(-100).y(0))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(0).y(0))
                    .pointer(PointerBuilder(/*id=*/2, ToolType::FINGER).x(100).y(0))
                    .classification(MotionClassification::MULTI_FINGER_SWIPE)
                    .deviceId(DEVICE_ID)
                    .displayId(ui::LogicalDisplayId::INVALID)
                    .build());
    mTestListener.assertNotifyMotionWasCalled(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_POINTER_DOWN |
                                   (2 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT)),
                  WithMotionClassification(MotionClassification::MULTI_FINGER_SWIPE),
                  WithPointerCoords(0, 0, 200), WithPointerCoords(1, 100, 200),
                  WithPointerCoords(2, 200, 200), WithCursorPosition(100, 200)));
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_MOUSE)
                    .pointer(PointerBuilder(/*id=*/0, ToolType::FINGER).x(-90).y(10))
                    .pointer(PointerBuilder(/*id=*/1, ToolType::FINGER).x(10).y(10))
                    .pointer(PointerBuilder(/*id=*/2, ToolType::FINGER).x(110).y(10))
                    .classification(MotionClassification::MULTI_FINGER_SWIPE)
                    .deviceId(DEVICE_ID)
                    .displayId(ui::LogicalDisplayId::INVALID)
                    .build());
    mTestListener.assertNotifyMotionWasCalled(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                  WithMotionClassification(MotionClassification::MULTI_FINGER_SWIPE),
                  WithPointerCoords(0, 10, 210), WithPointerCoords(1, 110, 210),
                  WithPointerCoords(2, 210, 210), WithCursorPosition(100, 200)));
}

TEST_F(PointerChoreographerTest,
       AssociatedTouchpadMovesPointerOnAssociatedDisplayAndDoesNotMovePointerOnDefaultDisplay) {
    // Add two displays and set one to default.
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID, ANOTHER_DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);

    // Add two devices, one unassociated and the other associated with non-default mouse display.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_TOUCHPAD,
                                     ui::LogicalDisplayId::INVALID),
              generateTestDeviceInfo(SECOND_DEVICE_ID, AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_TOUCHPAD,
                                     ANOTHER_DISPLAY_ID)}});
    auto unassociatedMousePc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_EQ(DISPLAY_ID, unassociatedMousePc->getDisplayId());
    auto associatedMousePc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_EQ(ANOTHER_DISPLAY_ID, associatedMousePc->getDisplayId());

    // Set initial positions for PointerControllers.
    unassociatedMousePc->setPosition(100, 200);
    associatedMousePc->setPosition(300, 400);

    // Make NotifyMotionArgs from the associated mouse and notify Choreographer.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                    .pointer(TOUCHPAD_POINTER)
                    .deviceId(SECOND_DEVICE_ID)
                    .displayId(ANOTHER_DISPLAY_ID)
                    .build());

    // Check the status of the PointerControllers.
    unassociatedMousePc->assertPosition(100, 200);
    ASSERT_EQ(DISPLAY_ID, unassociatedMousePc->getDisplayId());
    associatedMousePc->assertPosition(310, 420);
    ASSERT_EQ(ANOTHER_DISPLAY_ID, associatedMousePc->getDisplayId());
    ASSERT_TRUE(associatedMousePc->isPointerShown());

    // Check that x-y coordinates, displayId and cursor position are correctly updated.
    mTestListener.assertNotifyMotionWasCalled(
            AllOf(WithCoords(310, 420), WithDeviceId(SECOND_DEVICE_ID),
                  WithDisplayId(ANOTHER_DISPLAY_ID), WithCursorPosition(310, 420)));
}

TEST_F(PointerChoreographerTest, DoesNotMovePointerForTouchpadSource) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_TOUCHPAD,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_EQ(DISPLAY_ID, pc->getDisplayId());

    // Set initial position of the PointerController.
    pc->setPosition(200, 300);

    // Assume that pointer capture is enabled.
    mChoreographer.notifyPointerCaptureChanged(
            NotifyPointerCaptureChangedArgs(/*id=*/1, systemTime(SYSTEM_TIME_MONOTONIC),
                                            PointerCaptureRequest(/*window=*/sp<BBinder>::make(),
                                                                  /*seq=*/0)));

    // Notify motion as if pointer capture is enabled.
    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHPAD)
                                        .pointer(FIRST_TOUCH_POINTER)
                                        .deviceId(DEVICE_ID)
                                        .displayId(ui::LogicalDisplayId::INVALID)
                                        .build());

    // Check that there's no update on the PointerController.
    pc->assertPosition(200, 300);
    ASSERT_FALSE(pc->isPointerShown());

    // Check x-y coordinates, displayId and cursor position are not changed.
    mTestListener.assertNotifyMotionWasCalled(
            AllOf(WithCoords(100, 200), WithDisplayId(ui::LogicalDisplayId::INVALID),
                  WithCursorPosition(AMOTION_EVENT_INVALID_CURSOR_POSITION,
                                     AMOTION_EVENT_INVALID_CURSOR_POSITION)));
}

TEST_F(PointerChoreographerTest, WhenPointerCaptureEnabledTouchpadHidesPointer) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_TOUCHPAD,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_EQ(DISPLAY_ID, pc->getDisplayId());
    ASSERT_TRUE(pc->isPointerShown());

    // Enable pointer capture and check if the PointerController hid the pointer.
    mChoreographer.notifyPointerCaptureChanged(
            NotifyPointerCaptureChangedArgs(/*id=*/1, systemTime(SYSTEM_TIME_MONOTONIC),
                                            PointerCaptureRequest(/*window=*/sp<BBinder>::make(),
                                                                  /*seq=*/0)));
    ASSERT_FALSE(pc->isPointerShown());
}

TEST_F(PointerChoreographerTest, SetsPointerIconForMouse) {
    // Make sure there is a PointerController.
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    pc->assertPointerIconNotSet();

    // Set pointer icon for the device.
    ASSERT_TRUE(mChoreographer.setPointerIcon(PointerIconStyle::TYPE_TEXT, DISPLAY_ID, DEVICE_ID));
    pc->assertPointerIconSet(PointerIconStyle::TYPE_TEXT);
}

TEST_F(PointerChoreographerTest, DoesNotSetMousePointerIconForWrongDisplayId) {
    // Make sure there is a PointerController.
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    pc->assertPointerIconNotSet();

    // Set pointer icon for wrong display id. This should be ignored.
    ASSERT_FALSE(mChoreographer.setPointerIcon(PointerIconStyle::TYPE_TEXT, ANOTHER_DISPLAY_ID,
                                               SECOND_DEVICE_ID));
    pc->assertPointerIconNotSet();
}

TEST_F(PointerChoreographerTest, DoesNotSetPointerIconForWrongDeviceId) {
    // Make sure there is a PointerController.
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    pc->assertPointerIconNotSet();

    // Set pointer icon for wrong device id. This should be ignored.
    ASSERT_FALSE(mChoreographer.setPointerIcon(PointerIconStyle::TYPE_TEXT, DISPLAY_ID,
                                               SECOND_DEVICE_ID));
    pc->assertPointerIconNotSet();
}

TEST_F(PointerChoreographerTest, SetsCustomPointerIconForMouse) {
    // Make sure there is a PointerController.
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    pc->assertCustomPointerIconNotSet();

    // Set custom pointer icon for the device.
    ASSERT_TRUE(mChoreographer.setPointerIcon(std::make_unique<SpriteIcon>(
                                                      PointerIconStyle::TYPE_CUSTOM),
                                              DISPLAY_ID, DEVICE_ID));
    pc->assertCustomPointerIconSet(PointerIconStyle::TYPE_CUSTOM);

    // Set custom pointer icon for wrong device id. This should be ignored.
    ASSERT_FALSE(mChoreographer.setPointerIcon(std::make_unique<SpriteIcon>(
                                                       PointerIconStyle::TYPE_CUSTOM),
                                               DISPLAY_ID, SECOND_DEVICE_ID));
    pc->assertCustomPointerIconNotSet();
}

TEST_F(PointerChoreographerTest, SetsPointerIconForMouseOnTwoDisplays) {
    // Make sure there are two PointerControllers on different displays.
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID, ANOTHER_DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE, ui::LogicalDisplayId::INVALID),
              generateTestDeviceInfo(SECOND_DEVICE_ID, AINPUT_SOURCE_MOUSE, ANOTHER_DISPLAY_ID)}});
    auto firstMousePc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_EQ(DISPLAY_ID, firstMousePc->getDisplayId());
    auto secondMousePc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_EQ(ANOTHER_DISPLAY_ID, secondMousePc->getDisplayId());

    // Set pointer icon for one mouse.
    ASSERT_TRUE(mChoreographer.setPointerIcon(PointerIconStyle::TYPE_TEXT, DISPLAY_ID, DEVICE_ID));
    firstMousePc->assertPointerIconSet(PointerIconStyle::TYPE_TEXT);
    secondMousePc->assertPointerIconNotSet();

    // Set pointer icon for another mouse.
    ASSERT_TRUE(mChoreographer.setPointerIcon(PointerIconStyle::TYPE_TEXT, ANOTHER_DISPLAY_ID,
                                              SECOND_DEVICE_ID));
    secondMousePc->assertPointerIconSet(PointerIconStyle::TYPE_TEXT);
    firstMousePc->assertPointerIconNotSet();
}

using SkipPointerScreenshotForPrivacySensitiveDisplaysFixtureParam =
        std::tuple<std::string_view /*name*/, uint32_t /*source*/, ControllerType, PointerBuilder,
                   std::function<void(PointerChoreographer&)>, int32_t /*action*/>;

class SkipPointerScreenshotForPrivacySensitiveDisplaysTestFixture
      : public PointerChoreographerTest,
        public ::testing::WithParamInterface<
                SkipPointerScreenshotForPrivacySensitiveDisplaysFixtureParam> {
protected:
    void initializePointerDevice(const PointerBuilder& pointerBuilder, const uint32_t source,
                                 const std::function<void(PointerChoreographer&)> onControllerInit,
                                 const int32_t action) {
        mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));

        // Add appropriate pointer device
        mChoreographer.notifyInputDevicesChanged(
                {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, source, DISPLAY_ID)}});
        onControllerInit(mChoreographer);

        // Emit input events to create PointerController
        mChoreographer.notifyMotion(MotionArgsBuilder(action, source)
                                            .pointer(pointerBuilder)
                                            .deviceId(DEVICE_ID)
                                            .displayId(DISPLAY_ID)
                                            .build());
    }
};

INSTANTIATE_TEST_SUITE_P(
        PointerChoreographerTest, SkipPointerScreenshotForPrivacySensitiveDisplaysTestFixture,
        ::testing::Values(
                std::make_tuple(
                        "TouchSpots", AINPUT_SOURCE_TOUCHSCREEN, ControllerType::TOUCH,
                        FIRST_TOUCH_POINTER,
                        [](PointerChoreographer& pc) { pc.setShowTouchesEnabled(true); },
                        AMOTION_EVENT_ACTION_DOWN),
                std::make_tuple(
                        "Mouse", AINPUT_SOURCE_MOUSE, ControllerType::MOUSE, MOUSE_POINTER,
                        [](PointerChoreographer& pc) {}, AMOTION_EVENT_ACTION_DOWN),
                std::make_tuple(
                        "Stylus", AINPUT_SOURCE_STYLUS, ControllerType::STYLUS, STYLUS_POINTER,
                        [](PointerChoreographer& pc) { pc.setStylusPointerIconEnabled(true); },
                        AMOTION_EVENT_ACTION_HOVER_ENTER),
                std::make_tuple(
                        "DrawingTablet", AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_STYLUS,
                        ControllerType::MOUSE, STYLUS_POINTER, [](PointerChoreographer& pc) {},
                        AMOTION_EVENT_ACTION_HOVER_ENTER)),
        [](const testing::TestParamInfo<
                SkipPointerScreenshotForPrivacySensitiveDisplaysFixtureParam>& p) {
            return std::string{std::get<0>(p.param)};
        });

TEST_P(SkipPointerScreenshotForPrivacySensitiveDisplaysTestFixture,
       WindowInfosListenerIsOnlyRegisteredWhenRequired) {
    const auto& [name, source, controllerType, pointerBuilder, onControllerInit, action] =
            GetParam();
    assertWindowInfosListenerNotRegistered();

    // Listener should registered when a pointer device is added
    initializePointerDevice(pointerBuilder, source, onControllerInit, action);
    assertWindowInfosListenerRegistered();

    mChoreographer.notifyInputDevicesChanged({});
    assertWindowInfosListenerNotRegistered();
}

TEST_P(SkipPointerScreenshotForPrivacySensitiveDisplaysTestFixture,
       InitialDisplayInfoIsPopulatedForListener) {
    const auto& [name, source, controllerType, pointerBuilder, onControllerInit, action] =
            GetParam();
    // listener should not be registered if there is no pointer device
    assertWindowInfosListenerNotRegistered();

    gui::WindowInfo windowInfo;
    windowInfo.displayId = DISPLAY_ID;
    windowInfo.inputConfig |= gui::WindowInfo::InputConfig::SENSITIVE_FOR_PRIVACY;
    mInjectedInitialWindowInfos = {windowInfo};

    initializePointerDevice(pointerBuilder, source, onControllerInit, action);
    assertWindowInfosListenerRegistered();

    // Pointer indicators should be hidden based on the initial display info
    auto pc = assertPointerControllerCreated(controllerType);
    pc->assertIsSkipScreenshotFlagSet(DISPLAY_ID);
    pc->assertIsSkipScreenshotFlagNotSet(ANOTHER_DISPLAY_ID);

    // un-marking the privacy sensitive display should reset the state
    windowInfo.inputConfig.clear();
    gui::DisplayInfo displayInfo;
    displayInfo.displayId = DISPLAY_ID;
    mRegisteredWindowInfoListener
            ->onWindowInfosChanged(/*windowInfosUpdate=*/
                                   {{windowInfo}, {displayInfo}, /*vsyncId=*/0, /*timestamp=*/0});

    pc->assertIsSkipScreenshotFlagNotSet(DISPLAY_ID);
    pc->assertIsSkipScreenshotFlagNotSet(ANOTHER_DISPLAY_ID);
}

TEST_P(SkipPointerScreenshotForPrivacySensitiveDisplaysTestFixture,
       SkipsPointerScreenshotForPrivacySensitiveWindows) {
    const auto& [name, source, controllerType, pointerBuilder, onControllerInit, action] =
            GetParam();
    initializePointerDevice(pointerBuilder, source, onControllerInit, action);

    // By default pointer indicators should not be hidden
    auto pc = assertPointerControllerCreated(controllerType);
    pc->assertIsSkipScreenshotFlagNotSet(DISPLAY_ID);
    pc->assertIsSkipScreenshotFlagNotSet(ANOTHER_DISPLAY_ID);

    // marking a display privacy sensitive should set flag to hide pointer indicators on the
    // display screenshot
    gui::WindowInfo windowInfo;
    windowInfo.displayId = DISPLAY_ID;
    windowInfo.inputConfig |= gui::WindowInfo::InputConfig::SENSITIVE_FOR_PRIVACY;
    gui::DisplayInfo displayInfo;
    displayInfo.displayId = DISPLAY_ID;
    assertWindowInfosListenerRegistered();
    mRegisteredWindowInfoListener
            ->onWindowInfosChanged(/*windowInfosUpdate=*/
                                   {{windowInfo}, {displayInfo}, /*vsyncId=*/0, /*timestamp=*/0});

    pc->assertIsSkipScreenshotFlagSet(DISPLAY_ID);
    pc->assertIsSkipScreenshotFlagNotSet(ANOTHER_DISPLAY_ID);

    // un-marking the privacy sensitive display should reset the state
    windowInfo.inputConfig.clear();
    mRegisteredWindowInfoListener
            ->onWindowInfosChanged(/*windowInfosUpdate=*/
                                   {{windowInfo}, {displayInfo}, /*vsyncId=*/0, /*timestamp=*/0});

    pc->assertIsSkipScreenshotFlagNotSet(DISPLAY_ID);
    pc->assertIsSkipScreenshotFlagNotSet(ANOTHER_DISPLAY_ID);
}

TEST_P(SkipPointerScreenshotForPrivacySensitiveDisplaysTestFixture,
       DoesNotSkipPointerScreenshotForHiddenPrivacySensitiveWindows) {
    const auto& [name, source, controllerType, pointerBuilder, onControllerInit, action] =
            GetParam();
    initializePointerDevice(pointerBuilder, source, onControllerInit, action);

    // By default pointer indicators should not be hidden
    auto pc = assertPointerControllerCreated(controllerType);
    pc->assertIsSkipScreenshotFlagNotSet(DISPLAY_ID);
    pc->assertIsSkipScreenshotFlagNotSet(ANOTHER_DISPLAY_ID);

    gui::WindowInfo windowInfo;
    windowInfo.displayId = DISPLAY_ID;
    windowInfo.inputConfig |= gui::WindowInfo::InputConfig::SENSITIVE_FOR_PRIVACY;
    windowInfo.inputConfig |= gui::WindowInfo::InputConfig::NOT_VISIBLE;
    gui::DisplayInfo displayInfo;
    displayInfo.displayId = DISPLAY_ID;
    assertWindowInfosListenerRegistered();
    mRegisteredWindowInfoListener
            ->onWindowInfosChanged(/*windowInfosUpdate=*/
                                   {{windowInfo}, {displayInfo}, /*vsyncId=*/0, /*timestamp=*/0});

    pc->assertIsSkipScreenshotFlagNotSet(DISPLAY_ID);
    pc->assertIsSkipScreenshotFlagNotSet(ANOTHER_DISPLAY_ID);
}

TEST_P(SkipPointerScreenshotForPrivacySensitiveDisplaysTestFixture,
       DoesNotUpdateControllerForUnchangedPrivacySensitiveWindows) {
    const auto& [name, source, controllerType, pointerBuilder, onControllerInit, action] =
            GetParam();
    initializePointerDevice(pointerBuilder, source, onControllerInit, action);

    auto pc = assertPointerControllerCreated(controllerType);
    gui::WindowInfo windowInfo;
    windowInfo.displayId = DISPLAY_ID;
    windowInfo.inputConfig |= gui::WindowInfo::InputConfig::SENSITIVE_FOR_PRIVACY;
    gui::DisplayInfo displayInfo;
    displayInfo.displayId = DISPLAY_ID;
    assertWindowInfosListenerRegistered();
    mRegisteredWindowInfoListener
            ->onWindowInfosChanged(/*windowInfosUpdate=*/
                                   {{windowInfo}, {displayInfo}, /*vsyncId=*/0, /*timestamp=*/0});

    gui::WindowInfo windowInfo2 = windowInfo;
    windowInfo2.inputConfig.clear();
    pc->assertSkipScreenshotFlagChanged();

    // controller should not be updated if there are no changes in privacy sensitive windows
    mRegisteredWindowInfoListener->onWindowInfosChanged(/*windowInfosUpdate=*/
                                                        {{windowInfo, windowInfo2},
                                                         {displayInfo},
                                                         /*vsyncId=*/0,
                                                         /*timestamp=*/0});
    pc->assertSkipScreenshotFlagNotChanged();
}

TEST_F_WITH_FLAGS(
        PointerChoreographerTest, HidesPointerScreenshotForExistingPrivacySensitiveWindows,
        REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(com::android::input::flags,
                                            hide_pointer_indicators_for_secure_windows))) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));

    // Add a first mouse device
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE, DISPLAY_ID)}});

    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_MOUSE)
                                        .pointer(MOUSE_POINTER)
                                        .deviceId(DEVICE_ID)
                                        .displayId(DISPLAY_ID)
                                        .build());

    gui::WindowInfo windowInfo;
    windowInfo.displayId = DISPLAY_ID;
    windowInfo.inputConfig |= gui::WindowInfo::InputConfig::SENSITIVE_FOR_PRIVACY;
    gui::DisplayInfo displayInfo;
    displayInfo.displayId = DISPLAY_ID;
    assertWindowInfosListenerRegistered();
    mRegisteredWindowInfoListener
            ->onWindowInfosChanged(/*windowInfosUpdate=*/
                                   {{windowInfo}, {displayInfo}, /*vsyncId=*/0, /*timestamp=*/0});

    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    pc->assertIsSkipScreenshotFlagSet(DISPLAY_ID);
    pc->assertIsSkipScreenshotFlagNotSet(ANOTHER_DISPLAY_ID);

    // Add a second touch device and controller
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_TOUCHSCREEN, DISPLAY_ID)}});
    mChoreographer.setShowTouchesEnabled(true);
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN)
                    .pointer(FIRST_TOUCH_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());

    // Pointer indicators should be hidden for this controller by default
    auto pc2 = assertPointerControllerCreated(ControllerType::TOUCH);
    pc->assertIsSkipScreenshotFlagSet(DISPLAY_ID);
    pc->assertIsSkipScreenshotFlagNotSet(ANOTHER_DISPLAY_ID);

    // un-marking the privacy sensitive display should reset the state
    windowInfo.inputConfig.clear();
    mRegisteredWindowInfoListener
            ->onWindowInfosChanged(/*windowInfosUpdate=*/
                                   {{windowInfo}, {displayInfo}, /*vsyncId=*/0, /*timestamp=*/0});

    pc->assertIsSkipScreenshotFlagNotSet(DISPLAY_ID);
    pc->assertIsSkipScreenshotFlagNotSet(ANOTHER_DISPLAY_ID);
    pc2->assertIsSkipScreenshotFlagNotSet(DISPLAY_ID);
    pc2->assertIsSkipScreenshotFlagNotSet(ANOTHER_DISPLAY_ID);
}

TEST_P(StylusTestFixture, SetsPointerIconForStylus) {
    const auto& [name, source, controllerType] = GetParam();

    // Make sure there is a PointerController.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, source, DISPLAY_ID)}});
    mChoreographer.setStylusPointerIconEnabled(true);
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, source)
                                        .pointer(STYLUS_POINTER)
                                        .deviceId(DEVICE_ID)
                                        .displayId(DISPLAY_ID)
                                        .build());
    auto pc = assertPointerControllerCreated(controllerType);
    pc->assertPointerIconNotSet();

    // Set pointer icon for the device.
    ASSERT_TRUE(mChoreographer.setPointerIcon(PointerIconStyle::TYPE_TEXT, DISPLAY_ID, DEVICE_ID));
    pc->assertPointerIconSet(PointerIconStyle::TYPE_TEXT);

    // Set pointer icon for wrong device id. This should be ignored.
    ASSERT_FALSE(mChoreographer.setPointerIcon(PointerIconStyle::TYPE_TEXT, DISPLAY_ID,
                                               SECOND_DEVICE_ID));
    pc->assertPointerIconNotSet();

    // The stylus stops hovering. This should cause the icon to be reset.
    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_EXIT, source)
                                        .pointer(STYLUS_POINTER)
                                        .deviceId(DEVICE_ID)
                                        .displayId(DISPLAY_ID)
                                        .build());
    pc->assertPointerIconSet(PointerIconStyle::TYPE_NOT_SPECIFIED);
}

TEST_P(StylusTestFixture, SetsCustomPointerIconForStylus) {
    const auto& [name, source, controllerType] = GetParam();

    // Make sure there is a PointerController.
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, source, DISPLAY_ID)}});
    mChoreographer.setStylusPointerIconEnabled(true);
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, source)
                                        .pointer(STYLUS_POINTER)
                                        .deviceId(DEVICE_ID)
                                        .displayId(DISPLAY_ID)
                                        .build());
    auto pc = assertPointerControllerCreated(controllerType);
    pc->assertCustomPointerIconNotSet();

    // Set custom pointer icon for the device.
    ASSERT_TRUE(mChoreographer.setPointerIcon(std::make_unique<SpriteIcon>(
                                                      PointerIconStyle::TYPE_CUSTOM),
                                              DISPLAY_ID, DEVICE_ID));
    pc->assertCustomPointerIconSet(PointerIconStyle::TYPE_CUSTOM);

    // Set custom pointer icon for wrong device id. This should be ignored.
    ASSERT_FALSE(mChoreographer.setPointerIcon(std::make_unique<SpriteIcon>(
                                                       PointerIconStyle::TYPE_CUSTOM),
                                               DISPLAY_ID, SECOND_DEVICE_ID));
    pc->assertCustomPointerIconNotSet();
}

TEST_P(StylusTestFixture, SetsPointerIconForTwoStyluses) {
    const auto& [name, source, controllerType] = GetParam();

    // Make sure there are two StylusPointerControllers. They can be on a same display.
    mChoreographer.setStylusPointerIconEnabled(true);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, source, DISPLAY_ID),
              generateTestDeviceInfo(SECOND_DEVICE_ID, source, DISPLAY_ID)}});
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, source)
                                        .pointer(STYLUS_POINTER)
                                        .deviceId(DEVICE_ID)
                                        .displayId(DISPLAY_ID)
                                        .build());
    auto firstStylusPc = assertPointerControllerCreated(controllerType);
    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, source)
                                        .pointer(STYLUS_POINTER)
                                        .deviceId(SECOND_DEVICE_ID)
                                        .displayId(DISPLAY_ID)
                                        .build());
    auto secondStylusPc = assertPointerControllerCreated(controllerType);

    // Set pointer icon for one stylus.
    ASSERT_TRUE(mChoreographer.setPointerIcon(PointerIconStyle::TYPE_TEXT, DISPLAY_ID, DEVICE_ID));
    firstStylusPc->assertPointerIconSet(PointerIconStyle::TYPE_TEXT);
    secondStylusPc->assertPointerIconNotSet();

    // Set pointer icon for another stylus.
    ASSERT_TRUE(mChoreographer.setPointerIcon(PointerIconStyle::TYPE_TEXT, DISPLAY_ID,
                                              SECOND_DEVICE_ID));
    secondStylusPc->assertPointerIconSet(PointerIconStyle::TYPE_TEXT);
    firstStylusPc->assertPointerIconNotSet();
}

TEST_P(StylusTestFixture, SetsPointerIconForMouseAndStylus) {
    const auto& [name, source, controllerType] = GetParam();

    // Make sure there are PointerControllers for a mouse and a stylus.
    mChoreographer.setStylusPointerIconEnabled(true);
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE, ui::LogicalDisplayId::INVALID),
              generateTestDeviceInfo(SECOND_DEVICE_ID, source, DISPLAY_ID)}});
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_MOVE, AINPUT_SOURCE_MOUSE)
                    .pointer(MOUSE_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(ui::LogicalDisplayId::INVALID)
                    .build());
    auto mousePc = assertPointerControllerCreated(ControllerType::MOUSE);
    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, source)
                                        .pointer(STYLUS_POINTER)
                                        .deviceId(SECOND_DEVICE_ID)
                                        .displayId(DISPLAY_ID)
                                        .build());
    auto stylusPc = assertPointerControllerCreated(controllerType);

    // Set pointer icon for the mouse.
    ASSERT_TRUE(mChoreographer.setPointerIcon(PointerIconStyle::TYPE_TEXT, DISPLAY_ID, DEVICE_ID));
    mousePc->assertPointerIconSet(PointerIconStyle::TYPE_TEXT);
    stylusPc->assertPointerIconNotSet();

    // Set pointer icon for the stylus.
    ASSERT_TRUE(mChoreographer.setPointerIcon(PointerIconStyle::TYPE_TEXT, DISPLAY_ID,
                                              SECOND_DEVICE_ID));
    stylusPc->assertPointerIconSet(PointerIconStyle::TYPE_TEXT);
    mousePc->assertPointerIconNotSet();
}

TEST_F(PointerChoreographerTest, SetPointerIconVisibilityHidesPointerOnDisplay) {
    // Make sure there are two PointerControllers on different displays.
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID, ANOTHER_DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE, ui::LogicalDisplayId::INVALID),
              generateTestDeviceInfo(SECOND_DEVICE_ID, AINPUT_SOURCE_MOUSE, ANOTHER_DISPLAY_ID)}});
    auto firstMousePc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_EQ(DISPLAY_ID, firstMousePc->getDisplayId());
    auto secondMousePc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_EQ(ANOTHER_DISPLAY_ID, secondMousePc->getDisplayId());

    // Both pointers should be visible.
    ASSERT_TRUE(firstMousePc->isPointerShown());
    ASSERT_TRUE(secondMousePc->isPointerShown());

    // Hide the icon on the second display.
    mChoreographer.setPointerIconVisibility(ANOTHER_DISPLAY_ID, false);
    ASSERT_TRUE(firstMousePc->isPointerShown());
    ASSERT_FALSE(secondMousePc->isPointerShown());

    // Move and set pointer icons for both mice. The second pointer should still be hidden.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                    .pointer(MOUSE_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                    .pointer(MOUSE_POINTER)
                    .deviceId(SECOND_DEVICE_ID)
                    .displayId(ANOTHER_DISPLAY_ID)
                    .build());
    ASSERT_TRUE(mChoreographer.setPointerIcon(PointerIconStyle::TYPE_TEXT, DISPLAY_ID, DEVICE_ID));
    ASSERT_TRUE(mChoreographer.setPointerIcon(PointerIconStyle::TYPE_TEXT, ANOTHER_DISPLAY_ID,
                                              SECOND_DEVICE_ID));
    firstMousePc->assertPointerIconSet(PointerIconStyle::TYPE_TEXT);
    secondMousePc->assertPointerIconSet(PointerIconStyle::TYPE_TEXT);
    ASSERT_TRUE(firstMousePc->isPointerShown());
    ASSERT_FALSE(secondMousePc->isPointerShown());

    // Allow the icon to be visible on the second display, and move the mouse.
    mChoreographer.setPointerIconVisibility(ANOTHER_DISPLAY_ID, true);
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                    .pointer(MOUSE_POINTER)
                    .deviceId(SECOND_DEVICE_ID)
                    .displayId(ANOTHER_DISPLAY_ID)
                    .build());
    ASSERT_TRUE(firstMousePc->isPointerShown());
    ASSERT_TRUE(secondMousePc->isPointerShown());
}

TEST_F(PointerChoreographerTest, SetPointerIconVisibilityHidesPointerWhenDeviceConnected) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);

    // Hide the pointer on the display, and then connect the mouse.
    mChoreographer.setPointerIconVisibility(DISPLAY_ID, false);
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    auto mousePc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_EQ(DISPLAY_ID, mousePc->getDisplayId());

    // The pointer should not be visible.
    ASSERT_FALSE(mousePc->isPointerShown());
}

TEST_F(PointerChoreographerTest, SetPointerIconVisibilityHidesPointerForTouchpad) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);

    // Hide the pointer on the display.
    mChoreographer.setPointerIconVisibility(DISPLAY_ID, false);

    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_TOUCHPAD,
                                     ui::LogicalDisplayId::INVALID)}});
    auto touchpadPc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_EQ(DISPLAY_ID, touchpadPc->getDisplayId());

    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER,
                                                  AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_TOUCHPAD)
                                        .pointer(TOUCHPAD_POINTER)
                                        .deviceId(DEVICE_ID)
                                        .displayId(DISPLAY_ID)
                                        .build());

    // The pointer should not be visible.
    ASSERT_FALSE(touchpadPc->isPointerShown());
}

TEST_P(StylusTestFixture, SetPointerIconVisibilityHidesPointerForStylus) {
    const auto& [name, source, controllerType] = GetParam();

    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setStylusPointerIconEnabled(true);

    // Hide the pointer on the display.
    mChoreographer.setPointerIconVisibility(DISPLAY_ID, false);

    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, source, DISPLAY_ID)}});
    mChoreographer.notifyMotion(MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, source)
                                        .pointer(STYLUS_POINTER)
                                        .deviceId(DEVICE_ID)
                                        .displayId(DISPLAY_ID)
                                        .build());
    ASSERT_TRUE(mChoreographer.setPointerIcon(PointerIconStyle::TYPE_TEXT, DISPLAY_ID, DEVICE_ID));
    auto pc = assertPointerControllerCreated(controllerType);
    pc->assertPointerIconSet(PointerIconStyle::TYPE_TEXT);

    // The pointer should not be visible.
    ASSERT_FALSE(pc->isPointerShown());
}

TEST_F(PointerChoreographerTest, DrawingTabletCanReportMouseEvent) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);

    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, DRAWING_TABLET_SOURCE,
                                     ui::LogicalDisplayId::INVALID)}});
    // There should be no controller created when a drawing tablet is connected
    assertPointerControllerNotCreated();

    // But if it ends up reporting a mouse event, then the mouse controller will be created
    // dynamically.
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                    .pointer(MOUSE_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_TRUE(pc->isPointerShown());

    // The controller is removed when the drawing tablet is removed
    mChoreographer.notifyInputDevicesChanged({/*id=*/0, {}});
    assertPointerControllerRemoved(pc);
}

TEST_F(PointerChoreographerTest, MultipleDrawingTabletsReportMouseEvents) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);

    // First drawing tablet is added
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, DRAWING_TABLET_SOURCE,
                                     ui::LogicalDisplayId::INVALID)}});
    assertPointerControllerNotCreated();

    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                    .pointer(MOUSE_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_TRUE(pc->isPointerShown());

    // Second drawing tablet is added
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, DRAWING_TABLET_SOURCE,
                                     ui::LogicalDisplayId::INVALID),
              generateTestDeviceInfo(SECOND_DEVICE_ID, DRAWING_TABLET_SOURCE,
                                     ui::LogicalDisplayId::INVALID)}});
    assertPointerControllerNotRemoved(pc);

    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                    .pointer(MOUSE_POINTER)
                    .deviceId(SECOND_DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());

    // First drawing tablet is removed
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, DRAWING_TABLET_SOURCE,
                                     ui::LogicalDisplayId::INVALID)}});
    assertPointerControllerNotRemoved(pc);

    // Second drawing tablet is removed
    mChoreographer.notifyInputDevicesChanged({/*id=*/0, {}});
    assertPointerControllerRemoved(pc);
}

TEST_F(PointerChoreographerTest, MouseAndDrawingTabletReportMouseEvents) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));
    mChoreographer.setDefaultMouseDisplayId(DISPLAY_ID);

    // Mouse and drawing tablet connected
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, DRAWING_TABLET_SOURCE,
                                     ui::LogicalDisplayId::INVALID),
              generateTestDeviceInfo(SECOND_DEVICE_ID, AINPUT_SOURCE_MOUSE,
                                     ui::LogicalDisplayId::INVALID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_TRUE(pc->isPointerShown());

    // Drawing tablet reports a mouse event
    mChoreographer.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, DRAWING_TABLET_SOURCE)
                    .pointer(MOUSE_POINTER)
                    .deviceId(DEVICE_ID)
                    .displayId(DISPLAY_ID)
                    .build());

    // Remove the mouse device
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, DRAWING_TABLET_SOURCE,
                                     ui::LogicalDisplayId::INVALID)}});

    // The mouse controller should not be removed, because the drawing tablet has produced a
    // mouse event, so we are treating it as a mouse too.
    assertPointerControllerNotRemoved(pc);

    mChoreographer.notifyInputDevicesChanged({/*id=*/0, {}});
    assertPointerControllerRemoved(pc);
}

class PointerVisibilityOnKeyPressTest : public PointerChoreographerTest {
protected:
    const std::unordered_map<int32_t, int32_t>
            mMetaKeyStates{{AKEYCODE_ALT_LEFT, AMETA_ALT_LEFT_ON},
                           {AKEYCODE_ALT_RIGHT, AMETA_ALT_RIGHT_ON},
                           {AKEYCODE_SHIFT_LEFT, AMETA_SHIFT_LEFT_ON},
                           {AKEYCODE_SHIFT_RIGHT, AMETA_SHIFT_RIGHT_ON},
                           {AKEYCODE_SYM, AMETA_SYM_ON},
                           {AKEYCODE_FUNCTION, AMETA_FUNCTION_ON},
                           {AKEYCODE_CTRL_LEFT, AMETA_CTRL_LEFT_ON},
                           {AKEYCODE_CTRL_RIGHT, AMETA_CTRL_RIGHT_ON},
                           {AKEYCODE_META_LEFT, AMETA_META_LEFT_ON},
                           {AKEYCODE_META_RIGHT, AMETA_META_RIGHT_ON},
                           {AKEYCODE_CAPS_LOCK, AMETA_CAPS_LOCK_ON},
                           {AKEYCODE_NUM_LOCK, AMETA_NUM_LOCK_ON},
                           {AKEYCODE_SCROLL_LOCK, AMETA_SCROLL_LOCK_ON}};

    void notifyKey(ui::LogicalDisplayId targetDisplay, int32_t keyCode,
                   int32_t metaState = AMETA_NONE) {
        if (metaState == AMETA_NONE && mMetaKeyStates.contains(keyCode)) {
            // For simplicity, we always set the corresponding meta state when sending a meta
            // keycode. This does not take into consideration when the meta state is updated in
            // reality.
            metaState = mMetaKeyStates.at(keyCode);
        }
        mChoreographer.notifyKey(KeyArgsBuilder(AKEY_EVENT_ACTION_DOWN, AINPUT_SOURCE_KEYBOARD)
                                         .displayId(targetDisplay)
                                         .keyCode(keyCode)
                                         .metaState(metaState)
                                         .build());
        mChoreographer.notifyKey(KeyArgsBuilder(AKEY_EVENT_ACTION_UP, AINPUT_SOURCE_KEYBOARD)
                                         .displayId(targetDisplay)
                                         .keyCode(keyCode)
                                         .metaState(metaState)
                                         .build());
    }

    void metaKeyCombinationHidesPointer(FakePointerController& pc, int32_t keyCode,
                                        int32_t metaKeyCode) {
        ASSERT_TRUE(pc.isPointerShown());
        notifyKey(DISPLAY_ID, keyCode, mMetaKeyStates.at(metaKeyCode));
        ASSERT_FALSE(pc.isPointerShown());

        unfadePointer();
    }

    void metaKeyCombinationDoesNotHidePointer(FakePointerController& pc, int32_t keyCode,
                                              int32_t metaKeyCode) {
        ASSERT_TRUE(pc.isPointerShown());
        notifyKey(DISPLAY_ID, keyCode, mMetaKeyStates.at(metaKeyCode));
        ASSERT_TRUE(pc.isPointerShown());
    }

    void unfadePointer() {
        // unfade pointer by injecting mose hover event
        mChoreographer.notifyMotion(
                MotionArgsBuilder(AMOTION_EVENT_ACTION_HOVER_ENTER, AINPUT_SOURCE_MOUSE)
                        .pointer(MOUSE_POINTER)
                        .deviceId(DEVICE_ID)
                        .displayId(DISPLAY_ID)
                        .build());
    }
};

TEST_F(PointerVisibilityOnKeyPressTest, KeystrokesWithoutImeConnectionDoesNotHidePointer) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));

    // Mouse connected
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE, DISPLAY_ID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_TRUE(pc->isPointerShown());

    notifyKey(ui::LogicalDisplayId::INVALID, AKEYCODE_0);
    notifyKey(ui::LogicalDisplayId::INVALID, AKEYCODE_A);
    notifyKey(ui::LogicalDisplayId::INVALID, AKEYCODE_CTRL_LEFT);

    ASSERT_TRUE(pc->isPointerShown());
}

TEST_F(PointerVisibilityOnKeyPressTest, AlphanumericKeystrokesWithImeConnectionHidePointer) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));

    // Mouse connected
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE, DISPLAY_ID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_TRUE(pc->isPointerShown());

    EXPECT_CALL(mMockPolicy, isInputMethodConnectionActive).WillRepeatedly(testing::Return(true));

    notifyKey(DISPLAY_ID, AKEYCODE_0);
    ASSERT_FALSE(pc->isPointerShown());

    unfadePointer();

    notifyKey(DISPLAY_ID, AKEYCODE_A);
    ASSERT_FALSE(pc->isPointerShown());
}

TEST_F(PointerVisibilityOnKeyPressTest, MetaKeystrokesDoNotHidePointer) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));

    // Mouse connected
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(SECOND_DEVICE_ID, AINPUT_SOURCE_MOUSE, DISPLAY_ID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_TRUE(pc->isPointerShown());

    EXPECT_CALL(mMockPolicy, isInputMethodConnectionActive).WillRepeatedly(testing::Return(true));

    const std::vector<int32_t> metaKeyCodes{AKEYCODE_ALT_LEFT,   AKEYCODE_ALT_RIGHT,
                                            AKEYCODE_SHIFT_LEFT, AKEYCODE_SHIFT_RIGHT,
                                            AKEYCODE_SYM,        AKEYCODE_FUNCTION,
                                            AKEYCODE_CTRL_LEFT,  AKEYCODE_CTRL_RIGHT,
                                            AKEYCODE_META_LEFT,  AKEYCODE_META_RIGHT,
                                            AKEYCODE_CAPS_LOCK,  AKEYCODE_NUM_LOCK,
                                            AKEYCODE_SCROLL_LOCK};
    for (int32_t keyCode : metaKeyCodes) {
        notifyKey(ui::LogicalDisplayId::INVALID, keyCode);
    }

    ASSERT_TRUE(pc->isPointerShown());
}

TEST_F(PointerVisibilityOnKeyPressTest, KeystrokesWithoutTargetHidePointerOnlyOnFocusedDisplay) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID, ANOTHER_DISPLAY_ID}));
    mChoreographer.setFocusedDisplay(DISPLAY_ID);

    // Mouse connected
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0,
             {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE, DISPLAY_ID),
              generateTestDeviceInfo(SECOND_DEVICE_ID, AINPUT_SOURCE_MOUSE, ANOTHER_DISPLAY_ID)}});
    auto pc1 = assertPointerControllerCreated(ControllerType::MOUSE);
    auto pc2 = assertPointerControllerCreated(ControllerType::MOUSE);
    ASSERT_TRUE(pc1->isPointerShown());
    ASSERT_TRUE(pc2->isPointerShown());

    EXPECT_CALL(mMockPolicy, isInputMethodConnectionActive).WillRepeatedly(testing::Return(true));

    notifyKey(ui::LogicalDisplayId::INVALID, AKEYCODE_0);
    ASSERT_FALSE(pc1->isPointerShown());
    ASSERT_TRUE(pc2->isPointerShown());
    unfadePointer();

    notifyKey(ui::LogicalDisplayId::INVALID, AKEYCODE_A);
    ASSERT_FALSE(pc1->isPointerShown());
    ASSERT_TRUE(pc2->isPointerShown());
}

TEST_F(PointerVisibilityOnKeyPressTest, TestMetaKeyCombinations) {
    mChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));

    // Mouse connected
    mChoreographer.notifyInputDevicesChanged(
            {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE, DISPLAY_ID)}});
    auto pc = assertPointerControllerCreated(ControllerType::MOUSE);
    EXPECT_CALL(mMockPolicy, isInputMethodConnectionActive).WillRepeatedly(testing::Return(true));

    // meta key combinations that should hide pointer
    metaKeyCombinationHidesPointer(*pc, AKEYCODE_A, AKEYCODE_SHIFT_LEFT);
    metaKeyCombinationHidesPointer(*pc, AKEYCODE_A, AKEYCODE_SHIFT_RIGHT);
    metaKeyCombinationHidesPointer(*pc, AKEYCODE_A, AKEYCODE_CAPS_LOCK);
    metaKeyCombinationHidesPointer(*pc, AKEYCODE_0, AKEYCODE_NUM_LOCK);
    metaKeyCombinationHidesPointer(*pc, AKEYCODE_A, AKEYCODE_SCROLL_LOCK);

    // meta key combinations that should not hide pointer
    metaKeyCombinationDoesNotHidePointer(*pc, AKEYCODE_A, AKEYCODE_ALT_LEFT);
    metaKeyCombinationDoesNotHidePointer(*pc, AKEYCODE_A, AKEYCODE_ALT_RIGHT);
    metaKeyCombinationDoesNotHidePointer(*pc, AKEYCODE_A, AKEYCODE_CTRL_LEFT);
    metaKeyCombinationDoesNotHidePointer(*pc, AKEYCODE_A, AKEYCODE_CTRL_RIGHT);
    metaKeyCombinationDoesNotHidePointer(*pc, AKEYCODE_A, AKEYCODE_SYM);
    metaKeyCombinationDoesNotHidePointer(*pc, AKEYCODE_A, AKEYCODE_FUNCTION);
    metaKeyCombinationDoesNotHidePointer(*pc, AKEYCODE_A, AKEYCODE_META_LEFT);
    metaKeyCombinationDoesNotHidePointer(*pc, AKEYCODE_A, AKEYCODE_META_RIGHT);
}

class PointerChoreographerWindowInfoListenerTest : public testing::Test {};

TEST_F_WITH_FLAGS(
        PointerChoreographerWindowInfoListenerTest,
        doesNotCrashIfListenerCalledAfterPointerChoreographerDestroyed,
        REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(com::android::input::flags,
                                            hide_pointer_indicators_for_secure_windows))) {
    sp<android::gui::WindowInfosListener> registeredListener;
    sp<android::gui::WindowInfosListener> localListenerCopy;
    {
        testing::NiceMock<MockPointerChoreographerPolicyInterface> mockPolicy;
        EXPECT_CALL(mockPolicy, createPointerController(ControllerType::MOUSE))
                .WillOnce(testing::Return(std::make_shared<FakePointerController>()));
        TestInputListener testListener;
        std::vector<gui::WindowInfo> injectedInitialWindowInfos;
        TestPointerChoreographer testChoreographer{testListener, mockPolicy, registeredListener,
                                                   injectedInitialWindowInfos};
        testChoreographer.setDisplayViewports(createViewports({DISPLAY_ID}));

        // Add mouse to create controller and listener
        testChoreographer.notifyInputDevicesChanged(
                {/*id=*/0, {generateTestDeviceInfo(DEVICE_ID, AINPUT_SOURCE_MOUSE, DISPLAY_ID)}});

        ASSERT_NE(nullptr, registeredListener) << "WindowInfosListener was not registered";
        localListenerCopy = registeredListener;
    }
    ASSERT_EQ(nullptr, registeredListener) << "WindowInfosListener was not unregistered";

    gui::WindowInfo windowInfo;
    windowInfo.displayId = DISPLAY_ID;
    windowInfo.inputConfig |= gui::WindowInfo::InputConfig::SENSITIVE_FOR_PRIVACY;
    gui::DisplayInfo displayInfo;
    displayInfo.displayId = DISPLAY_ID;
    localListenerCopy->onWindowInfosChanged(
            /*windowInfosUpdate=*/{{windowInfo}, {displayInfo}, /*vsyncId=*/0, /*timestamp=*/0});
}

} // namespace android

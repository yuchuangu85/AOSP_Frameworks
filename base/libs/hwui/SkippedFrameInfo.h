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

#pragma once

namespace android::uirenderer {

//| 跳帧原因               | 触发条件            | 性能影响    | 恢复策略                |
//| ------------------ | --------------- | ------- | ------------------- |
//| `AlreadyDrawn`     | 同一VSync已绘制      | 避免重复绘制  | 下一VSync自动恢复         |
//| `NoBuffer`         | dequeueBuffer超时 | 减少GPU阻塞 | 250ms后重试            |
//| `NoOutputTarget`   | Surface丢失       | 节省CPU   | Surface创建后恢复        |
//| `ContextIsStopped` | Activity暂停      | 完全停止渲染  | `setStopped(false)` |
//| `NothingToDraw`    | 脏区为空            | 节省GPU   | 内容变化后自动恢复           |

enum class SkippedFrameReason {
    DrawingOff,
    ContextIsStopped,
    NothingToDraw,
    NoOutputTarget,
    NoBuffer,
    AlreadyDrawn,
};

} /* namespace android::uirenderer */

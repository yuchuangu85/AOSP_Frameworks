/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "DamageAccumulator.h"

#include <log/log.h>

#include "RenderNode.h"
#include "utils/MathUtils.h"

namespace android {
namespace uirenderer {

enum TransformType {
    TransformInvalid = 0,
    TransformRenderNode,
    TransformMatrix4,
    TransformNone,
};

struct DirtyStack {
    TransformType type;
    union {
        const RenderNode* renderNode;
        const Matrix4* matrix4;
    };
    // When this frame is pop'd, this rect is mapped through the above transform
    // and applied to the previous (aka parent) frame
    SkRect pendingDirty;
    DirtyStack* prev;
    DirtyStack* next;
};

DamageAccumulator::DamageAccumulator() {
    mHead = mAllocator.create_trivial<DirtyStack>();
    memset(mHead, 0, sizeof(DirtyStack));
    // Create a root that we will not pop off
    mHead->prev = mHead;
    mHead->type = TransformNone;
}

static void computeTransformImpl(const DirtyStack* currentFrame, Matrix4* outMatrix) {
    if (currentFrame->prev != currentFrame) {
        computeTransformImpl(currentFrame->prev, outMatrix);
    }
    switch (currentFrame->type) {
        case TransformRenderNode:
            currentFrame->renderNode->applyViewPropertyTransforms(*outMatrix);
            break;
        case TransformMatrix4:
            outMatrix->multiply(*currentFrame->matrix4);
            break;
        case TransformNone:
            // nothing to be done
            break;
        default:
            LOG_ALWAYS_FATAL("Tried to compute transform with an invalid type: %d",
                             currentFrame->type);
    }
}

void DamageAccumulator::computeCurrentTransform(Matrix4* outMatrix) const {
    outMatrix->loadIdentity();
    computeTransformImpl(mHead, outMatrix);
}

/*
 * 设计特点
 * - 内存管理优化
 *   - 使用对象分配器 (mAllocator) 而不是直接 new，提高性能
 *   - create_trivial 表示不需要调用构造函数，直接分配内存
 *   - 预分配栈帧，避免频繁的内存分配
 *
 *   链表结构
 *    损伤累积器使用双向链表管理栈帧：
 *    Frame1 <-> Frame2 <-> Frame3 <-> ...
 *      ↑
 *    mHead (当前活动帧)
 *
 *   性能考虑
 *    - 避免在每次压栈时都分配新内存，通过检查 mHead->next 实现帧复用
 *    - 使用内存分配器提高内存分配效率
 *    - 简单的链表操作，时间复杂度 O(1)
 */
void DamageAccumulator::pushCommon() {
    // 栈帧分配与初始化
    // - 检查是否需要分配新帧：如果当前头帧没有下一个帧，需要分配新的栈帧
    // - 内存分配：使用分配器创建新的 DirtyStack 对象
    // - 链表连接：
    //    - nextFrame->prev = mHead - 新帧指向前一帧
    //    - mHead->next = nextFrame - 当前帧指向新帧
    //    - nextFrame->next = nullptr - 新帧的下一帧暂时为空
    if (!mHead->next) {
        DirtyStack* nextFrame = mAllocator.create_trivial<DirtyStack>();
        nextFrame->next = nullptr;
        nextFrame->prev = mHead;
        mHead->next = nextFrame;
    }
    // 将头指针移动到新分配的栈帧，使其成为当前活动帧。
    mHead = mHead->next;
    // 将新帧的待处理损伤区域设置为空，为后续的损伤累积做准备。
    mHead->pendingDirty.setEmpty();
}

void DamageAccumulator::pushTransform(const RenderNode* transform) {
    pushCommon();
    // 明确标识这个栈帧包含的是渲染节点变换，这在后续的 popTransform() 中很重要，因为会根据这个类型调用相应的处理函数。
    mHead->type = TransformRenderNode;
    // 保存指向渲染节点的指针，这样在弹出栈帧时可以：
    // - 访问节点的变换属性
    // - 获取裁剪边界信息
    // - 检查透明度和投影设置
    mHead->renderNode = transform;
}

void DamageAccumulator::pushTransform(const Matrix4* transform) {
    pushCommon();
    mHead->type = TransformMatrix4;
    mHead->matrix4 = transform;
}

void DamageAccumulator::popTransform() {
    // 防止弹出根帧（根帧的prev指向自己）
    LOG_ALWAYS_FATAL_IF(mHead->prev == mHead, "Cannot pop the root frame!");
    // 保存当前栈帧指针
    DirtyStack* dirtyFrame = mHead;
    // 将头指针移动到前一个栈帧（弹出操作）
    mHead = mHead->prev;
    switch (dirtyFrame->type) {
        // 处理渲染节点变换：
        //  - 将当前帧的损伤区域通过渲染节点的逆变换映射回父节点坐标系
        //  - 合并到父帧的待处理损伤区域
        case TransformRenderNode:
            applyRenderNodeTransform(dirtyFrame);
            break;
        // 处理4x4矩阵变换：
        //  - 使用矩阵的逆变换将损伤区域转换回父坐标系
        //  - 合并到父帧
        case TransformMatrix4:
            applyMatrix4Transform(dirtyFrame);
            break;
        // 直接将当前帧的损伤区域合并到父帧
        // 不需要坐标变换
        case TransformNone:
            mHead->pendingDirty.join(dirtyFrame->pendingDirty);
            break;
        default:
            LOG_ALWAYS_FATAL("Tried to pop an invalid type: %d", dirtyFrame->type);
    }
}

static inline void mapRect(const Matrix4* matrix, const SkRect& in, SkRect* out) {
    if (in.isEmpty()) return;
    Rect temp(in);
    if (CC_LIKELY(!matrix->isPerspective())) {
        matrix->mapRect(temp);
    } else {
        // Don't attempt to calculate damage for a perspective transform
        // as the numbers this works with can break the perspective
        // calculations. Just give up and expand to DIRTY_MIN/DIRTY_MAX
        temp.set(DIRTY_MIN, DIRTY_MIN, DIRTY_MAX, DIRTY_MAX);
    }
    out->join({RECT_ARGS(temp)});
}

void DamageAccumulator::applyMatrix4Transform(DirtyStack* frame) {
    mapRect(frame->matrix4, frame->pendingDirty, &mHead->pendingDirty);
}

static inline void applyMatrix(const SkMatrix* transform, SkRect* rect) {
    if (transform && !transform->isIdentity()) {
        if (CC_LIKELY(!transform->hasPerspective())) {
            transform->mapRect(rect);
        } else {
            // Don't attempt to calculate damage for a perspective transform
            // as the numbers this works with can break the perspective
            // calculations. Just give up and expand to DIRTY_MIN/DIRTY_MAX
            rect->setLTRB(DIRTY_MIN, DIRTY_MIN, DIRTY_MAX, DIRTY_MAX);
        }
    }
}

static inline void applyMatrix(const SkMatrix& transform, SkRect* rect) {
    return applyMatrix(&transform, rect);
}

static inline void mapRect(const RenderProperties& props, const SkRect& in, SkRect* out) {
    if (in.isEmpty()) return;
    SkRect temp(in);
    if (Properties::getStretchEffectBehavior() == StretchEffectBehavior::UniformScale) {
        const StretchEffect& stretch = props.layerProperties().getStretchEffect();
        if (!stretch.isEmpty()) {
            applyMatrix(stretch.makeLinearStretch(props.getWidth(), props.getHeight()), &temp);
        }
    }
    applyMatrix(props.getTransformMatrix(), &temp);
    if (props.getStaticMatrix()) {
        applyMatrix(props.getStaticMatrix(), &temp);
    } else if (props.getAnimationMatrix()) {
        applyMatrix(props.getAnimationMatrix(), &temp);
    }
    temp.offset(props.getLeft(), props.getTop());
    out->join(temp);
}

static DirtyStack* findParentRenderNode(DirtyStack* frame) {
    while (frame->prev != frame) {
        frame = frame->prev;
        if (frame->type == TransformRenderNode) {
            return frame;
        }
    }
    return nullptr;
}

static DirtyStack* findProjectionReceiver(DirtyStack* frame) {
    if (frame) {
        while (frame->prev != frame) {
            frame = frame->prev;
            if (frame->type == TransformRenderNode && frame->renderNode->hasProjectionReceiver()) {
                return frame;
            }
        }
    }
    return nullptr;
}

static void applyTransforms(DirtyStack* frame, DirtyStack* end) {
    SkRect* rect = &frame->pendingDirty;
    while (frame != end) {
        if (frame->type == TransformRenderNode) {
            mapRect(frame->renderNode->properties(), *rect, rect);
        } else {
            mapRect(frame->matrix4, *rect, rect);
        }
        frame = frame->prev;
    }
}

void DamageAccumulator::applyRenderNodeTransform(DirtyStack* frame) {
    if (frame->pendingDirty.isEmpty()) {
        return;
    }

    // 如果节点完全透明（alpha <= 0），不会产生可见的损伤，直接返回。
    const RenderProperties& props = frame->renderNode->properties();
    if (props.getAlpha() <= 0) {
        return;
    }

    // 裁切处理
    // - 如果启用了边界裁剪，将损伤区域限制在节点的边界内
    // - 使用节点的宽度和高度创建裁剪矩形
    // - 如果损伤区域与边界没有交集，清空损伤区域
    // Perform clipping
    if (props.getClipDamageToBounds()) {
        if (!frame->pendingDirty.intersect(SkRect::MakeIWH(props.getWidth(), props.getHeight()))) {
            frame->pendingDirty.setEmpty();
        }
    }

    // 坐标变换
    // - 将当前帧的损伤区域通过渲染节点的变换矩阵映射到父节点坐标系
    // - 结果合并到父帧的待处理损伤区域中
    // - 考虑了节点的平移、旋转、缩放等变换
    // apply all transforms
    mapRect(props, frame->pendingDirty, &mHead->pendingDirty);

    // 处理特殊的向后投影效果（如阴影、反射等）：
    // 向后投影流程：
    // - 查找父节点：findParentRenderNode(frame) 找到当前节点的直接父节点
    // - 查找投影接收者：findProjectionReceiver(parentNode) 找到实际的投影目标节点
    // - 应用变换：applyTransforms(frame, projectionReceiver) 将损伤区域变换到投影接收者的坐标系
    // - 合并损伤：将变换后的损伤区域合并到投影接收者
    // - 清空当前：清空当前帧的损伤区域，因为已经投影到其他地方
    // project backwards if necessary
    if (props.getProjectBackwards() && !frame->pendingDirty.isEmpty()) {
        // First, find our parent RenderNode:
        DirtyStack* parentNode = findParentRenderNode(frame);
        // Find our parent's projection receiver, which is what we project onto
        DirtyStack* projectionReceiver = findProjectionReceiver(parentNode);
        if (projectionReceiver) {
            applyTransforms(frame, projectionReceiver);
            projectionReceiver->pendingDirty.join(frame->pendingDirty);
        }

        frame->pendingDirty.setEmpty();
    }
}

static void computeClipAndTransformImpl(const DirtyStack* currentFrame, SkRect* crop,
                                        Matrix4* outMatrix) {
    SkRect currentCrop = *crop;
    switch (currentFrame->type) {
        case TransformRenderNode: {
            const RenderProperties& props = currentFrame->renderNode->properties();
            // Perform clipping
            if (props.getClipDamageToBounds() && !currentCrop.isEmpty()) {
                if (!currentCrop.intersect(SkRect::MakeIWH(props.getWidth(), props.getHeight()))) {
                    currentCrop.setEmpty();
                }
            }

            // apply all transforms
            crop->setEmpty();
            mapRect(props, currentCrop, crop);
        } break;
        case TransformMatrix4:
            crop->setEmpty();
            mapRect(currentFrame->matrix4, currentCrop, crop);
            break;
        default:
            break;
    }

    if (currentFrame->prev != currentFrame) {
        computeClipAndTransformImpl(currentFrame->prev, crop, outMatrix);
    }
    switch (currentFrame->type) {
        case TransformRenderNode:
            currentFrame->renderNode->applyViewPropertyTransforms(*outMatrix);
            break;
        case TransformMatrix4:
            outMatrix->multiply(*currentFrame->matrix4);
            break;
        case TransformNone:
            // nothing to be done
            break;
        default:
            LOG_ALWAYS_FATAL("Tried to compute transform with an invalid type: %d",
                             currentFrame->type);
    }
}

SkRect DamageAccumulator::computeClipAndTransform(const SkRect& bounds, Matrix4* outMatrix) const {
    SkRect cropInGlobal = bounds;
    outMatrix->loadIdentity();
    computeClipAndTransformImpl(mHead, &cropInGlobal, outMatrix);
    SkRect cropInLocal;
    Matrix4 globalToLocal;
    globalToLocal.loadInverse(*outMatrix);
    mapRect(&globalToLocal, cropInGlobal, &cropInLocal);
    return cropInLocal;
}

void DamageAccumulator::dirty(float left, float top, float right, float bottom) {
    mHead->pendingDirty.join({left, top, right, bottom});
}

void DamageAccumulator::peekAtDirty(SkRect* dest) const {
    *dest = mHead->pendingDirty;
}

void DamageAccumulator::finish(SkRect* totalDirty) {
    LOG_ALWAYS_FATAL_IF(mHead->prev != mHead, "Cannot finish, mismatched push/pop calls! %p vs. %p",
                        mHead->prev, mHead);
    // Root node never has a transform, so this is the fully mapped dirty rect
    *totalDirty = mHead->pendingDirty;
    totalDirty->roundOut(totalDirty);
    mHead->pendingDirty.setEmpty();
}

DamageAccumulator::StretchResult DamageAccumulator::findNearestStretchEffect() const {
    DirtyStack* frame = mHead;
    while (frame->prev != frame) {
        if (frame->type == TransformRenderNode) {
            const auto& renderNode = frame->renderNode;
            const auto& frameRenderNodeProperties = renderNode->properties();
            const auto& effect =
                    frameRenderNodeProperties.layerProperties().getStretchEffect();
            const float width = (float) frameRenderNodeProperties.getWidth();
            const float height = (float) frameRenderNodeProperties.getHeight();
            if (!effect.isEmpty()) {
                Matrix4 stretchMatrix;
                computeTransformImpl(frame, &stretchMatrix);
                Rect stretchRect = Rect(0.f, 0.f, width, height);
                stretchMatrix.mapRect(stretchRect);

                return StretchResult{
                        .stretchEffect = &effect,
                        .parentBounds = SkRect::MakeLTRB(stretchRect.left, stretchRect.top,
                                                         stretchRect.right, stretchRect.bottom),
                        .width = width,
                        .height = height};
            }
        }
        frame = frame->prev;
    }
    return StretchResult{};
}

} /* namespace uirenderer */
} /* namespace android */

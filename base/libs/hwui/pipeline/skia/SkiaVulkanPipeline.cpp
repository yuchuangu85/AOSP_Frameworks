/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "pipeline/skia/SkiaVulkanPipeline.h"

#include <SkSurface.h>
#include <SkTypes.h>
#include <cutils/properties.h>
#include <gui/TraceUtils.h>
#include <include/gpu/ganesh/GrDirectContext.h>
#include <include/gpu/ganesh/GrTypes.h>
#include <include/gpu/ganesh/vk/GrVkTypes.h>
#include <strings.h>

#include "DeferredLayerUpdater.h"
#include "LightingInfo.h"
#include "Readback.h"
#include "pipeline/skia/ShaderCache.h"
#include "pipeline/skia/SkiaGpuPipeline.h"
#include "pipeline/skia/SkiaProfileRenderer.h"
#include "pipeline/skia/VkInteropFunctorDrawable.h"
#include "renderstate/RenderState.h"
#include "renderthread/Frame.h"
#include "renderthread/IRenderPipeline.h"

using namespace android::uirenderer::renderthread;

namespace android {
namespace uirenderer {
namespace skiapipeline {

SkiaVulkanPipeline::SkiaVulkanPipeline(renderthread::RenderThread& thread)
        : SkiaGpuPipeline(thread) {
    thread.renderState().registerContextCallback(this);
}

SkiaVulkanPipeline::~SkiaVulkanPipeline() {
    mRenderThread.renderState().removeContextCallback(this);
}

VulkanManager& SkiaVulkanPipeline::vulkanManager() {
    return mRenderThread.vulkanManager();
}

MakeCurrentResult SkiaVulkanPipeline::makeCurrent() {
    // In case the surface was destroyed (e.g. a previous trimMemory call) we
    // need to recreate it here.
    if (mHardwareBuffer) {
        mRenderThread.requireVkContext();
    } else if (!isSurfaceReady() && mNativeWindow) {
        setSurface(mNativeWindow.get(), SwapBehavior::kSwap_default);
    }
    return isContextReady() ? MakeCurrentResult::AlreadyCurrent : MakeCurrentResult::Failed;
}

Frame SkiaVulkanPipeline::getFrame() {
    LOG_ALWAYS_FATAL_IF(mVkSurface == nullptr, "getFrame() called on a context with no surface!");
    return vulkanManager().dequeueNextBuffer(mVkSurface);
}

/*
 * 参数说明
    frame: 帧信息
    screenDirty, dirty: 脏区域定义
    lightGeometry: 光照几何信息
    layerUpdateQueue: 图层更新队列
    contentDrawBounds: 内容绘制边界
    opaque: 是否不透明
    lightInfo: 光照信息
    renderNodes: 渲染节点列表
    profiler: 性能分析器
    bufferParams: 硬件缓冲区参数
    profilerLock: 分析器锁
 */
IRenderPipeline::DrawResult SkiaVulkanPipeline::draw(
        const Frame& frame, const SkRect& screenDirty, const SkRect& dirty,
        const LightGeometry& lightGeometry, LayerUpdateQueue* layerUpdateQueue,
        const Rect& contentDrawBounds, bool opaque, const LightInfo& lightInfo,
        const std::vector<sp<RenderNode>>& renderNodes, FrameInfoVisualizer* profiler,
        const HardwareBufferRenderParams& bufferParams, std::mutex& profilerLock) {
    // 根据是否使用硬件缓冲区选择不同的表面获取方式：
    //  - 硬件缓冲区模式：从参数获取表面和变换
    //  - 普通Vulkan表面模式：从Vulkan表面获取当前表面和预变换
    sk_sp<SkSurface> backBuffer;
    SkMatrix preTransform;
    if (mHardwareBuffer) {
        backBuffer = getBufferSkSurface(bufferParams);
        preTransform = bufferParams.getTransform();
    } else {
        backBuffer = mVkSurface->getCurrentSkSurface();
        preTransform = mVkSurface->getCurrentPreTransform();
    }

    // 如果无法获取有效的渲染表面，返回失败结果。
    if (backBuffer.get() == nullptr) {
        return {false, -1, android::base::unique_fd{}};
    }

    // 将全局光源位置根据表面旋转进行坐标变换，并更新光照信息。
    // update the coordinates of the global light position based on surface rotation
    SkPoint lightCenter = preTransform.mapXY(lightGeometry.center.x, lightGeometry.center.y);
    LightGeometry localGeometry = lightGeometry;
    localGeometry.center.x = lightCenter.fX;
    localGeometry.center.y = lightCenter.fY;

    LightingInfo::updateLighting(localGeometry, lightInfo);
    // 核心渲染调用，处理所有渲染节点和图层更新。
    renderFrame(*layerUpdateQueue, dirty, renderNodes, opaque, contentDrawBounds, backBuffer,
                preTransform);

    // 脏区域显示，性能分析图表，使用锁确保线程安全
    // Draw visual debugging features
    if (CC_UNLIKELY(Properties::showDirtyRegions ||
                    ProfileType::None != Properties::getProfileType())) {
        std::scoped_lock lock(profilerLock);
        SkCanvas* profileCanvas = backBuffer->getCanvas();
        SkAutoCanvasRestore saver(profileCanvas, true);
        profileCanvas->concat(preTransform);
        SkiaProfileRenderer profileRenderer(profileCanvas, frame.width(), frame.height());
        profiler->draw(profileRenderer);
    }

    // 提交Vulkan绘制命令到GPU：
    //  - 使用ATrace标记性能跟踪区间
    //  - 完成帧的提交并获取结果（包括提交时间和栅栏）
    VulkanManager::VkDrawResult drawResult;
    {
        ATRACE_NAME("flush commands");
        drawResult = vulkanManager().finishFrame(backBuffer.get());
    }
    // 清空图层更新队列，为下一帧做准备。
    layerUpdateQueue->clear();

    // 在调试模式下输出资源缓存使用情况。
    // Log memory statistics
    if (CC_UNLIKELY(Properties::debugLevel != kDebugDisabled)) {
        dumpResourceCacheUsage();
    }

    // 返回绘制结果：成功标志，提交时间戳，呈现栅栏（用于同步）
    return {true, drawResult.submissionTime, std::move(drawResult.presentFence)};
}

bool SkiaVulkanPipeline::swapBuffers(const Frame& frame, IRenderPipeline::DrawResult& drawResult,
                                     const SkRect& screenDirty, FrameInfo* currentFrameInfo,
                                     bool* requireSwap) {
    // Even if we decided to cancel the frame, from the perspective of jank
    // metrics the frame was swapped at this point
    currentFrameInfo->markSwapBuffers();

    if (mHardwareBuffer) {
        return false;
    }

    *requireSwap = drawResult.success;

    if (*requireSwap) {
        vulkanManager().swapBuffers(mVkSurface, screenDirty, std::move(drawResult.presentFence));
    }

    return *requireSwap;
}

DeferredLayerUpdater* SkiaVulkanPipeline::createTextureLayer() {
    mRenderThread.requireVkContext();

    return new DeferredLayerUpdater(mRenderThread.renderState());
}

void SkiaVulkanPipeline::onStop() {}

[[nodiscard]] android::base::unique_fd SkiaVulkanPipeline::flush() {
    int fence = -1;
    vulkanManager().createReleaseFence(&fence, mRenderThread.getGrContext());
    return android::base::unique_fd(fence);
}

// We can safely ignore the swap behavior because VkManager will always operate
// in a mode equivalent to EGLManager::SwapBehavior::kBufferAge
bool SkiaVulkanPipeline::setSurface(ANativeWindow* surface, SwapBehavior /*swapBehavior*/) {
    mNativeWindow = surface;

    if (mVkSurface) {
        vulkanManager().destroySurface(mVkSurface);
        mVkSurface = nullptr;
    }

    if (surface) {
        mRenderThread.requireVkContext();
        mVkSurface =
                vulkanManager().createSurface(surface, mColorMode, mSurfaceColorSpace,
                                              mSurfaceColorType, mRenderThread.getGrContext(), 0);
    }

    return mVkSurface != nullptr;
}

void SkiaVulkanPipeline::setTargetSdrHdrRatio(float ratio) {
    SkiaPipeline::setTargetSdrHdrRatio(ratio);
    if (mVkSurface) {
        mVkSurface->setColorSpace(mSurfaceColorSpace);
    }
}

bool SkiaVulkanPipeline::isSurfaceReady() {
    return CC_UNLIKELY(mVkSurface != nullptr);
}

bool SkiaVulkanPipeline::isContextReady() {
    return CC_LIKELY(vulkanManager().hasVkContext());
}

void SkiaVulkanPipeline::invokeFunctor(const RenderThread& thread, Functor* functor) {
    VkInteropFunctorDrawable::vkInvokeFunctor(functor);
}

sk_sp<Bitmap> SkiaVulkanPipeline::allocateHardwareBitmap(renderthread::RenderThread& renderThread,
                                                         SkBitmap& skBitmap) {
    LOG_ALWAYS_FATAL("Unimplemented");
    return nullptr;
}

void SkiaVulkanPipeline::onContextDestroyed() {
    if (mVkSurface) {
        vulkanManager().destroySurface(mVkSurface);
        mVkSurface = nullptr;
    }
}

const SkM44& SkiaVulkanPipeline::getPixelSnapMatrix() const {
    return mVkSurface->getPixelSnapMatrix();
}

} /* namespace skiapipeline */
} /* namespace uirenderer */
} /* namespace android */

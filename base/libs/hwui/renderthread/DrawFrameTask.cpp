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

#include "DrawFrameTask.h"

#include <gui/TraceUtils.h>
#include <utils/Log.h>

#include <algorithm>

#include "../DeferredLayerUpdater.h"
#include "../DisplayList.h"
#include "../Properties.h"
#include "../RenderNode.h"
#include "CanvasContext.h"
#include "HardwareBufferRenderParams.h"
#include "RenderThread.h"

namespace android {
namespace uirenderer {
namespace renderthread {

DrawFrameTask::DrawFrameTask()
        : mRenderThread(nullptr)
        , mContext(nullptr)
        , mContentDrawBounds(0, 0, 0, 0)
        , mSyncResult(SyncResult::OK) {}

DrawFrameTask::~DrawFrameTask() {}

void DrawFrameTask::setContext(RenderThread* thread, CanvasContext* context,
                               RenderNode* targetNode) {
    mRenderThread = thread;
    mContext = context;
    mTargetNode = targetNode;
}

void DrawFrameTask::pushLayerUpdate(DeferredLayerUpdater* layer) {
    LOG_ALWAYS_FATAL_IF(!mContext,
                        "Lifecycle violation, there's no context to pushLayerUpdate with!");

    for (size_t i = 0; i < mLayers.size(); i++) {
        if (mLayers[i].get() == layer) {
            return;
        }
    }
    mLayers.push_back(layer);
}

void DrawFrameTask::removeLayerUpdate(DeferredLayerUpdater* layer) {
    for (size_t i = 0; i < mLayers.size(); i++) {
        if (mLayers[i].get() == layer) {
            mLayers.erase(mLayers.begin() + i);
            return;
        }
    }
}

int DrawFrameTask::drawFrame() {
    LOG_ALWAYS_FATAL_IF(!mContext, "Cannot drawFrame with no CanvasContext!");

    mSyncResult = SyncResult::OK;
    mSyncQueued = systemTime(SYSTEM_TIME_MONOTONIC);
    postAndWait();

    return mSyncResult;
}

/*
   UI 线程  postAndWait() 睡
       │        │
       │        ├─► RenderThread::run()
       │             1. 取 VSyncId
       │             2. 记 sync 延迟
       │             3. syncFrameState()  克隆树、上传纹理
       │             4. 可提前？─yes─► unblockUiThread() 唤醒 UI
       │             5. draw() / skip + flush
       │             6. 帧回调
       │             7. 兜底 unblockUiThread()
       │
       ◄───────────── 醒来，继续下一帧逻辑
 */
void DrawFrameTask::postAndWait() {
    ATRACE_CALL(); // 1. 仅调试：systrace 打标签，方便性能分析
    AutoMutex _lock(mLock); // 2. 加锁：保护下面两个临界操作
    mRenderThread->queue().post([this]() { run(); }); // 3. 把任务扔给渲染线程
    mSignal.wait(mLock); // 4. UI 线程挂起，等渲染线程做完
}

void DrawFrameTask::run() {
    // 阶段 0：拿“身份证”与调试信息: 从 mFrameInfo 数组里取出 VSync 序号，后面所有 trace 都以它为标记，
    // 方便 systrace 里把 UI 线程、RenderThread、GPU 三段串成一条流水线。
    const int64_t vsyncId = mFrameInfo[static_cast<int>(FrameInfoIndex::FrameTimelineVsyncId)];
    ATRACE_FORMAT("DrawFrames %" PRId64, vsyncId);

    // 阶段 1：把“UI 线程排队耗时”告诉 GPU 端
    // mSyncQueued 是 UI 线程调用 postAndWait() 时打的时间戳；这里算出的差值就是 Sync 延迟，用于 GPU 端性能分析。
    // 下面第二行和第四行把 HDR/SDR 比例 与 HardwareBuffer 参数 一并塞进 CanvasContext，供后续 GPU 管线使用。
    mContext->setSyncDelayDuration(systemTime(SYSTEM_TIME_MONOTONIC) - mSyncQueued);
    mContext->setTargetSdrHdrRatio(mRenderSdrHdrRatio);

    auto hardwareBufferParams = mHardwareBufferParams;
    mContext->setHardwareBufferRenderParams(hardwareBufferParams);
    IRenderPipeline* pipeline = mContext->getRenderPipeline();
    bool canUnblockUiThread;
    bool canDrawThisFrame;
    bool solelyTextureViewUpdates;
    {
        // 阶段 2：syncFrameState() —— 与 UI 线程“对表”
        // TreeInfo 是一个 双向结果桶：
        //  – 输入：告诉 syncFrameState() 这次要做完整遍历。
        //  – 输出：记录 是否跳帧、是否只有 TextureView 更新 等。
        // syncFrameState() 里会：
        //  – 把 UI 线程刚刚 record 下来的 DisplayList 树 克隆 到渲染线程；
        //  – 生成 OpenGL/Vulkan 指令、上传纹理、计算脏区；
        //  – 如果期间发现 没有任何脏像素 或 被 SurfaceFlinger 挡住，就填 skippedFrameReason，本帧直接跳过。
        // 返回值 canUnblockUiThread 决定 UI 线程能否提前被唤醒（见阶段 3）。

        TreeInfo info(TreeInfo::MODE_FULL, *mContext);
        info.forceDrawFrame = mForceDrawFrame;
        mForceDrawFrame = false;
        canUnblockUiThread = syncFrameState(info);
        canDrawThisFrame = !info.out.skippedFrameReason.has_value();
        solelyTextureViewUpdates = info.out.solelyTextureViewUpdates;

        if (mFrameCommitCallback) {
            mContext->addFrameCommitListener(std::move(mFrameCommitCallback));
            mFrameCommitCallback = nullptr;
        }
    }

    // Grab a copy of everything we need
    CanvasContext* context = mContext;
    std::function<std::function<void(bool)>(int32_t, int64_t)> frameCallback =
            std::move(mFrameCallback);
    std::function<void()> frameCompleteCallback = std::move(mFrameCompleteCallback);
    mFrameCallback = nullptr;
    mFrameCompleteCallback = nullptr;

    // 阶段 3：提前唤醒 UI 线程（减少卡顿）
    // 如果 syncFrameState() 已经把 所有 GPU 指令 都准备完毕，UI 线程就可以继续往前跑，不必等到 GPU 真正画完。
    // unblockUiThread() 里做的就是 mSignal.signal()，让 postAndWait() 的 wait() 返回。
    // → 这样 UI 线程 可以立刻开始准备下一帧的逻辑，并行 于 GPU 渲染，降低掉帧概率。
    // From this point on anything in "this" is *UNSAFE TO ACCESS*
    if (canUnblockUiThread) {
        unblockUiThread();
    }

    // Even if we aren't drawing this vsync pulse the next frame number will still be accurate
    if (CC_UNLIKELY(frameCallback)) {
        context->enqueueFrameWork([frameCallback, context, syncResult = mSyncResult,
                                   frameNr = context->getFrameNumber()]() {
            auto frameCommitCallback = frameCallback(syncResult, frameNr);
            if (frameCommitCallback) {
                context->addFrameCommitListener(std::move(frameCommitCallback));
            }
        });
    }

    // 阶段 4：真正 draw() —— GPU 干活
    // 正常路径：draw() 会把 DisplayList 翻译成 GL/Vulkan 命令，提交给 GPU。
    // 跳过路径：也要 flush 一下，否则前面 syncFrameState() 上传的纹理会滞留在命令缓冲区，影响下一帧。
    if (CC_LIKELY(canDrawThisFrame)) {
        context->draw(solelyTextureViewUpdates);
    } else {
#ifdef __ANDROID__
        // Do a flush in case syncFrameState performed any texture uploads. Since we skipped
        // the draw() call, those uploads (or deletes) will end up sitting in the queue.
        // Do them now
        // 跳帧，但之前上传的纹理还在 GL 队列里，先 flush
        if (GrDirectContext* grContext = mRenderThread->getGrContext()) {
            grContext->flushAndSubmit();
        }
#endif
        // wait on fences so tasks don't overlap next frame
        context->waitOnFences(); // 等 GPU fence，防止任务重叠
    }

    // 阶段 5：帧完成回调
    // 应用层通过 View.postFrameCallback() 注册的 帧完成监听器 在这里触发，典型用途：动画库拿 实际呈现时间戳 做速度修正。
    if (CC_UNLIKELY(frameCompleteCallback)) {
        std::invoke(frameCompleteCallback);
    }

    // 阶段 6：兜底唤醒 & HardwareBuffer 回调
    // 如果阶段 3 因为 GPU 指令没准备好 而没唤醒，现在 必须 唤醒，否则 UI 线程会一直睡。
    // 若本帧用了 AHardwareBuffer（SurfaceView、录像、相机流），再把 fence 回传给消费者，让它在 GPU 完成后立即读帧，避免撕裂。
    if (!canUnblockUiThread) {
        unblockUiThread(); // 阶段 3 没唤醒，现在必须唤醒
    }

    if (pipeline->hasHardwareBuffer()) {
        auto fence = pipeline->flush(); // 拿到 sync fence
        hardwareBufferParams.invokeRenderCallback(std::move(fence), 0);
    }
}

// syncFrameState() 里会：
//  – 把 UI 线程刚刚 record 下来的 DisplayList 树 克隆 到渲染线程；
//  – 生成 OpenGL/Vulkan 指令、上传纹理、计算脏区；
//  – 如果期间发现 没有任何脏像素 或 被 SurfaceFlinger 挡住，就填 skippedFrameReason，本帧直接跳过。
// true → 纹理、Shader、帧缓冲全就绪，UI 线程可以立刻被唤醒；
// false → 纹理缓存爆了，必须等 GPU 把上一帧纹理腾出来，UI 线程还得再睡一会。
bool DrawFrameTask::syncFrameState(TreeInfo& info) {
    ATRACE_CALL();
    // 步骤 1：把 VSync 时间戳喂给“时间领主”: TimeLord 是渲染侧的单例时钟管理器，负责把 UI 线程打的时间戳 映射成 GPU 端 的时钟基准，
    // 后续 draw() 里算动画进度、屏幕外插值都靠它。
    int64_t vsync = mFrameInfo[static_cast<int>(FrameInfoIndex::Vsync)];
    int64_t intendedVsync = mFrameInfo[static_cast<int>(FrameInfoIndex::IntendedVsync)];
    int64_t vsyncId = mFrameInfo[static_cast<int>(FrameInfoIndex::FrameTimelineVsyncId)];
    int64_t frameDeadline = mFrameInfo[static_cast<int>(FrameInfoIndex::FrameDeadline)];
    int64_t frameInterval = mFrameInfo[static_cast<int>(FrameInfoIndex::FrameInterval)];
    mRenderThread->timeLord().vsyncReceived(vsync, intendedVsync, vsyncId, frameDeadline,
            frameInterval);
    // 步骤 2：让当前 EGL/OpenGL 上下文成为“当前”
    // makeCurrent() 把 RenderThread 的 GL 上下文 绑到当前线程，失败（Surface 被销毁、窗口切换）就返回 false，本帧直接放弃。
    bool canDraw = mContext->makeCurrent();
    // unpinImages() 把 上一帧引用的 GraphicBuffer/纹理 解除引用，让 SurfaceFlinger 可以回收。
    mContext->unpinImages();
// 步骤 3：把 Layer 更新 应用到 GPU
#ifdef __ANDROID__
    // mLayers 里存的是 RenderLayer（TextureView、SurfaceView、MediaCodec 输出等）的 异步更新包。
    // apply() 会把 新的 GraphicBuffer、变换矩阵、裁剪区 塞进 GPU 纹理数组，供后面 draw() 采样。
    for (size_t i = 0; i < mLayers.size(); i++) {
        if (mLayers[i]) {
            mLayers[i]->apply();
        }
    }
#endif

    mLayers.clear();
    // 步骤 4：核心 —— 把 RenderNode 树 克隆到渲染线程
    mContext->setContentDrawBounds(mContentDrawBounds);
    /*
      prepareTree() 会深度遍历 mTargetNode（根节点），做：
        1.录制 DisplayList → 生成 OpenGL/Vulkan 命令；
        2.上传纹理、顶点、Path 到 GPU 缓存；
        3.计算脏区、合并裁剪；
        4.把动画状态写到 info.out.hasAnimations / requiresUiRedraw；
        5.如果纹理缓存不足，设置 info.prepareTextures = false，本帧放弃。
     */
    mContext->prepareTree(info, mFrameInfo, mSyncQueued, mTargetNode);

    // 步骤 5：判断 要不要跳帧
    // This is after the prepareTree so that any pending operations
    // (RenderNode tree state, prefetched layers, etc...) will be flushed.
    // hasOutputTarget() 为 false → Surface 被销毁或还没创建（比如 Activity 正在重启），直接标记 LostSurface。
    bool hasTarget = mContext->hasOutputTarget();
    // canDraw == false → 前面 makeCurrent() 失败，上下文已停。
    if (CC_UNLIKELY(!hasTarget || !canDraw)) {
        if (!hasTarget) {
            mSyncResult |= SyncResult::LostSurfaceRewardIfFound;
            info.out.skippedFrameReason = SkippedFrameReason::NoOutputTarget;
        } else {
            // If we have a surface but can't draw we must be stopped
            mSyncResult |= SyncResult::ContextIsStopped;
            info.out.skippedFrameReason = SkippedFrameReason::ContextIsStopped;
        }
    }

    // 步骤 6：汇总 同步结果 并返回
    // mSyncResult 是一个位域，UI 线程醒来后通过它可以知道：是否丢了 Surface、是否需要立即再安排一帧、是否被 dropped。
    if (info.out.hasAnimations) {
        if (info.out.requiresUiRedraw) {
            mSyncResult |= SyncResult::UIRedrawRequired;
        }
    }
    if (info.out.skippedFrameReason) {
        mSyncResult |= SyncResult::FrameDropped;
    }
    // 最终返回的 唯一 bool 就是 info.prepareTextures：
    //    – true  → 所有纹理/Shader 已就绪，UI 线程可提前唤醒；
    //    – false → GPU 端纹理缓存不足，必须等 GPU 把上一帧纹理腾完，UI 线程继续睡。
    // If prepareTextures is false, we ran out of texture cache space
    return info.prepareTextures;
}

void DrawFrameTask::unblockUiThread() {
    AutoMutex _lock(mLock);
    mSignal.signal();
}

} /* namespace renderthread */
} /* namespace uirenderer */
} /* namespace android */

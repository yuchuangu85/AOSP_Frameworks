# 数据交付规范（stage JSON）

目标：为《Android 一帧的旅程：从 requestLayout 到屏幕点亮》HTML 可视化页面提供**可验证的源码事实**。
仓库：/Users/yuchuan.gu/Workspace/MX/AOSP_Frameworks （AOSP，android17-release，HEAD 213ab0f81）
约束：**只允许阅读 base/ 与 native/ 下的源码，严禁读取/引用 docs/ 目录**。

## 交付文件
每个阶段一个 JSON 文件：`vsync-pipeline/data/stage-XX.json`（XX 为两位数字，如 stage-01.json）。
必须是**严格合法的 JSON**（UTF-8，无注释、无尾逗号）。写完后用 `python3 -m json.tool` 校验。

## JSON 结构

```json
{
  "id": "stage-01",
  "num": 1,
  "title": "中文标题（不超过 28 字）",
  "subtitle": "一句话副标题，点明本阶段的关键动作",
  "lane": "UI",                       // 主要发生地：UI / RT / SF / HWC / DISPLAY / BQ
  "durationHint": "约 0.5~3 ms（示意）", // 真实量级，若无法确定写 "量级：亚毫秒~数毫秒"
  "summary": "150~300 字中文摘要：谁在什么线程、因为什么、调用了什么、结果是什么。",
  "callchain": ["Class.method()", "Class.method()"],   // 严格按调用顺序，使用真实符号名
  "sections": [
    {
      "heading": "小节标题（如：为什么需要这一拍）",
      "html": "<p>中文正文……</p>"
    }
  ],
  "snippets": [
    {
      "file": "base/core/java/android/view/ViewRootImpl.java",
      "symbol": "scheduleTraversals",
      "lines": "3307-3360",
      "code": "源码原文，保留缩进；最多 45 行，可用 // ... 省略中间无关行",
      "note": "这段代码说明了什么（1~3 句中文）"
    }
  ],
  "keys": [
    { "name": "mTraversalScheduled", "type": "boolean", "desc": "作用/生命周期/并发语义" }
  ],
  "metrics": [
    { "name": "Choreographer#doFrame", "value": "trace tag / 典型耗时", "desc": "如何观测" }
  ],
  "pitfalls": ["常见误解或优化点 / 线上问题，中文 1~2 句"],
  "traceTags": ["Choreographer#doFrame", "Choreographer#doTraversal"],
  "next": "stage-02"
}
```

## 质量要求（评审会逐条检查）
1. **可验证**：所有类名/函数名/成员名必须来自源码；每个 snippet 的 file 必须存在，`lines` 必须覆盖 symbol，`code` 必须与源码对应行**逐字一致**（可删减，用 `// ...` 标记省略，不可改写）。建议用 `grep -n` 定位后用 `sed -n 'A,Bp'` 取原文。
2. **讲清线程与边界**：明确 Java/native、进程/线程、同步/异步、跨进程调用（binder/socket/BitTube）发生的位置。
3. **讲清时序与 deadline**：VSYNC 模型下这一阶段必须在哪个截止点前完成；错过会怎样（掉帧/jank）。
4. **4 个以上小节**、**800 字以上正文**（summary+sections 合计）、**1~4 个代码片段**、**3 个以上关键结构/字段**、**2 条以上坑点**、**2 个以上观测点**。
5. **不要写"可能/大概"来掩盖不确定**：无法从源码确定的，明确写"源码中未体现"。
6. html 字段只用：p、ul/ol/li、strong、em、code、br、table/thead/tbody/tr/th/td、blockquote。不要用 script/style/img/外链。
7. 源码路径一律写**相对仓库根目录**的路径，如 `base/core/java/android/view/Choreographer.java`。

## 额外提示
- 读者是资深 Android 系统工程师：术语可直接使用（BLAST、Transaction、DisplayList、RenderNode、HWC、VSYNC phase offset 等），但要在首次出现时解释清楚。
- 每阶段必须回答：**触发者 / 执行线程 / 关键函数 / 关键数据结构 / 内存或 IPC 传递 / 与 VSYNC 的关系 / 观测手段**。

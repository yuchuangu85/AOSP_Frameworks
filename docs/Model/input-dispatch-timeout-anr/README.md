# Input Dispatch Timeout ANR

直接打开 `index.html`。HTML 内嵌样式、图标、模型、脚本和源码摘录，不需要网络、服务或 npm 安装。

## 范围

- 本页专讲已发送连接的完成应答超时，不把 no-focused-window 等待混为 ACK 超时。
- 八种场景：Looper 阻塞、输入处理耗时、延迟 finish、finish 后 ACK 清理延迟、正常完成、handled=false、多事件独立计时、窗口 timeout 改变。
- 所有队列、时钟、状态、时间轴和步骤共用一个纯模型与播放位置。
- 单窗口连接、非批量 MotionEvent、发布成功；排除 key fallback、完整批处理、完整多窗口输入及 ANR 弹窗/进程处理。
- cancelEventsForAnr 只标注，不将合成取消事件逐条加入队列。恢复示例假设它们没有额外过期项。
- finish 后 ACK 清理延迟是抽象的极端边界场景，不是声称正常 ACK 会耗时数秒；该时段合并 native 发送、通道返回及 Dispatcher command 清理的延迟。
- 教学时间不是实测性能。相同时刻 ACK 优先清理是确定性演示规则，不是平台竞态保证。基础默认 5 秒仍可能被 HW multiplier 和窗口配置改变。
- policy 的转场间隔保持在后续 ACK 清理命令之前，遵守 command FIFO；这个视觉间隔不是源码规定的固定耗时。自定义时长后，描述与结果按当前参数更新，不沿用预设结论。
- 只修改此目录，不修改 AOSP `base/`、`native/` 或其他模型页。

## 生成与验证

```sh
node docs/Model/input-dispatch-timeout-anr/build.mjs
node --test docs/Model/input-dispatch-timeout-anr/tests/model.test.cjs
python3 -B ~/.codex/skills/source-pipeline-explorer/scripts/verify_sources.py \
  docs/Model/input-dispatch-timeout-anr/data/sources.json --source-root . \
  --report docs/Model/input-dispatch-timeout-anr/data/source-verification.json
```

已有 Playwright 安装时可执行浏览器验证，不新增依赖：

```sh
PLAYWRIGHT_MODULE=/path/to/playwright-core \
CHROME_PATH='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' \
node docs/Model/input-dispatch-timeout-anr/tests/browser.cjs
```

`src/` 和 `data/sources.json` 是编辑源，`index.html` 为生成产物。构建校验摘录并记录源码 hash；浏览器报告绑定最终 HTML hash。模型测试、源码匹配、机制复核、浏览器交互与真机 ANR 是不同证据范围。

工具图标来自 Lucide，采用 ISC license 并内嵌通知；页面主体机制图由 HTML/CSS 构建，遵循现有 `docs/Model` 的视觉约定。

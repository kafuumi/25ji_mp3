# AMP Component TODOs

## P0 核心闭环

### controller
- [ ] 在 `components/amp/include/amp/controller.h` 中增加公开的 `amp_controller_append_processor()` 接口
- [ ] 在 `components/amp/controller.c` 中实现 `amp_controller_append_processor()`，复用现有 `AMP_ELEMENT_PROCESSOR` 组装逻辑
- [ ] 在 `components/amp/include/amp/controller.h` 中公开 `amp_controller_deinit()`
- [ ] 梳理 controller 生命周期，确保 `init -> append -> run -> deinit` 路径完整可用

### file source
- [ ] 明确文件输入方案：保留 `file_reader` 作为目录扫描器，新增真正的文件流 reader element
- [ ] 新增单文件 reader element，支持打开指定音频文件并分块写入 ringbuf
- [ ] 先只支持单个 MP3 文件输入，不提前扩展多格式和播放列表
- [ ] 将文件 reader element 接入 controller 的 reader 入口

### audio_codec
- [ ] 将 `audio_codec` 作为 processor 接入 controller 管理的流水线
- [ ] 保持当前最小目标仅支持 MP3 解码，避免同时扩展 FLAC/AAC
- [ ] 补齐输入结束时的 EOS 处理逻辑
- [ ] 处理 ringbuf 读空、解码失败、输出 buffer 扩容失败等异常路径
- [ ] 确保文件结束后不会进入无限报错或空转循环

### i2s_writer
- [ ] 将 `audio_codec` 输出的 PCM 稳定写入 I2S
- [ ] 明确播放中、暂停、结束态下 writer 的行为
- [ ] 补齐播放结束后的 drain 或停止策略
- [ ] 以 `test.mp3` 为基准完成真实出声验证

### P0 验收
- [ ] 通过 controller 串起 `file reader -> audio_codec -> i2s_writer`
- [ ] 可以稳定播放 `/storage/music/test.mp3`
- [ ] 不再依赖测试代码手动调用 `set_input_rb()` / `set_output_rb()`

## P1 稳定性与状态控制

### event system
- [ ] 明确最小事件集：`PLAY`、`PAUSE`、`RESET`、`FATAL`、`EOS`、`AUDIO_INFO_CHANGED`
- [ ] 实现 `components/amp/controller.c` 中的 `AMP_EVENT_REPORT` handler
- [ ] 补齐 `sin_pcm_reader`、`i2s_writer`、文件 reader 的事件通知逻辑
- [ ] 将当前主要依赖 dashboard 轮询的控制逻辑逐步收敛到事件驱动

### dashboard
- [ ] 明确 `READY`、`PLAYING`、`PAUSE`、`FATAL` 的状态迁移规则
- [ ] 判断是否需要新增 `STOPPED` 或 `FINISHED` 状态
- [ ] 明确 `RESET` 对 ringbuf、decoder、文件位置的影响

### cleanup
- [ ] controller deinit 时释放 event loop、event handler、dashboard、ringbuf 和 element 私有资源
- [ ] 为 `audio_codec`、`i2s_writer`、reader element 补齐 `deinit` 接口挂接
- [ ] 确保播放器可重复执行 `init -> run -> deinit`

## P1 输出参数联动

### audio_codec -> i2s_writer
- [ ] 从 decoder 获取实际输出参数：采样率、位宽、声道数
- [ ] 通过 `AMP_EVENT_REPORT` 上报音频参数变化
- [ ] 由 controller 或 writer 消费参数变化并调用 `i2s_writer_audio_config()`

### i2s_writer_audio_config
- [ ] 补齐 I2S 重配置时的 disable / reconfig / enable 时序
- [ ] 明确重配置失败时的回退或报错策略
- [ ] 验证不同采样率文件输出时不会出现静音、爆音或写失败

## P2 文件与播放源能力

### file_reader
- [ ] 修正 `components/amp/file_reader.c` 中的扩展名过滤逻辑，避免错误匹配非音频文件
- [ ] 明确 `file_reader` 最终定位是否仅作为目录扫描器
- [ ] 若保留目录扫描器定位，避免把文件流读取职责混入 `file_reader`

### playlist / directory playback
- [ ] 在单文件播放稳定后再增加目录顺序播放能力
- [ ] 为下一首、上一首、自动切歌预留接口或状态设计
- [ ] 明确播完一首后的切换行为与状态更新逻辑

## P2 测试体系

### component tests
- [ ] 为 `controller` 增加流水线组装和状态切换测试
- [ ] 为 `file_reader` 增加扩展名过滤和目录扫描测试
- [ ] 为 `audio_codec` 增加 EOS、异常输入、buffer 扩容测试
- [ ] 为 `ringbuf` 增加基础边界和异常路径测试

### end-to-end tests
- [ ] 在 `test_apps/amp` 中增加完整流水线测试
- [ ] 增加 mock PCM sink，减少端到端测试对真实 I2S 外设的依赖
- [ ] 覆盖最小验证链路：`test.mp3 -> codec -> sink`

## P3 接口整理与扩展

### public API
- [ ] 清理只应内部使用的接口，收敛公开头文件边界
- [ ] 统一 reader / writer / codec / output 等命名风格
- [ ] 复查 `element_priv.h` 与公开 API 的职责分界

### format support
- [ ] 在 MP3 主链路稳定后再考虑 FLAC/AAC 支持
- [ ] 设计最小可用的格式识别与 decoder 选择策略
- [ ] 确保新增格式不会破坏现有 MP3 播放路径

### UI integration
- [ ] 在播放主链路稳定后，再接入 UI 的 `play/pause/toggle` 控制
- [ ] 后续结合 SD 卡浏览、OLED 显示补齐交互层能力

## 里程碑

### M1 可播单文件
- [ ] 指定 `/storage/music/test.mp3` 可稳定播放完成

### M2 可控可收尾
- [ ] 支持 `play/pause/reset`
- [ ] 播放结束和异常退出都能正确回收资源

### M3 可回归
- [ ] 具备基础端到端测试和关键模块回归测试

### M4 可扩展
- [ ] 支持目录或播放列表能力
- [ ] 为 UI 集成提供稳定接口

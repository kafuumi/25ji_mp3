# AMP 组件 TODO

> 更新日期：2026-06-29。本文档是 `components/amp` 的内部细节清单，项目级路线图见根目录 `TODO.md`。

## 范围说明

- `amp` 组件只负责音频管线内部能力：数据源、解码、ringbuf、controller、writer、状态和事件。
- 项目级播放器入口、SD 卡挂载、UI 和硬件 bring-up 由根目录 `TODO.md` 跟踪。
- 当前主线优先保障 MP3 文件播放链路稳定：`file_reader -> audio_decoder -> i2s_writer`。

## 已完成

- [x] `ringbuf.c/h` 已提供 `rb_read()`、`rb_write()`、`rb_done_write()`、`rb_reset_is_done_write()` 等基础 API。
- [x] `amp_mem.c/h` 已提供 PSRAM-aware 内存分配封装。
- [x] `dashboard.c/h` 已提供 `READY`、`PLAYING`、`PAUSE`、`FATAL` 状态和 EOS done 计数基础设施。
- [x] `element.h` 已统一 `amp_element_interface_t`、`amp_element_task_config_t` 和 element role。
- [x] `element_priv.h` 已定义内部 `struct amp_element`、task notify mask 和 `AMP_EL_SEND_DONE`。
- [x] `amp_event.h` 已定义 action/report event base，以及 `STREAM_EOS`、`AUDIO_FORMAT`、`AUDIO_DETAIL`、`FATAL` 事件 ID。
- [x] `controller.c` 已支持 reader、processor、writer 自动串联和 ringbuf 链接。
- [x] `controller.c` 已支持 play、pause、reset、toggle 状态动作广播。
- [x] `amp_file_reader` 已支持目录扫描、顺序文件读取、EOS 和音频格式事件上报。
- [x] `amp_audio_decoder` 已封装 `esp_audio_simple_dec`，支持跨曲目 reset/reopen、EOS flush 和输出 buffer 扩容。
- [x] `amp_i2s_writer` 已支持 I2S 输出、EOS latch、写入重试和 `write_pcm` API。
- [x] `amp_i2s_writer` 已能根据 `AUDIO_DETAIL` 事件重配采样率、位宽和声道。
- [x] `amp_i2s_writer` 在 pause/resume 状态切换时已执行 I2S channel disable/enable。
- [x] `amp_devnull_writer` 已提供用于测试的空 writer。
- [x] `amp_sine_pcm_reader` 已保留为基础音频验证用正弦波 reader。
- [x] `test_apps/amp` 已包含 `file_reader -> audio_decoder -> i2s_writer` 最小播放测试。

## 功能项

### P0 - 文件播放链路收敛

- [ ] 明确 `file_reader -> audio_decoder -> i2s_writer` 在一首 MP3 播放结束后的状态转换语义。
- [ ] 明确最后一首播放结束后的行为：保持 READY、PAUSE，或等待上层 playlist 决策。
- [ ] 让 `file_reader` 在无可播放文件时能向 controller 或上层返回明确状态。
- [ ] 定义目录条目的处理策略：跳过目录、递归扫描，或交给 playlist 层处理。

### P0 - FATAL 错误恢复

- [ ] 各 element 在硬错误时发送 `AMP_EVENT_REPORT_FATAL`。
- [ ] `i2s_writer` 在 `ESP_ERR_INVALID_STATE` 后重试仍失败时上报 FATAL。
- [ ] `audio_decoder` 在连续 decode 失败超过阈值时上报 FATAL 或请求跳过当前流。
- [ ] `file_reader` 在 `read()` 返回不可恢复错误时上报 FATAL。
- [ ] `amp_controller_handle_report_event()` 处理 FATAL 事件并切换到 `AMP_STATE_FATAL`。
- [ ] element 检测到 `cached_state == AMP_STATE_FATAL` 后退出或安全停驻 task loop。
- [ ] 定义 `amp_controller_action_reset()` 从 FATAL 恢复到 READY 的完整路径。

### P1 - 停止与重置 API

- [ ] 明确 stop 和 reset 的区别：stop 结束当前任务，reset 保留管线但回到 READY。
- [ ] 增加 `amp_controller_action_stop()` 或等价内部机制。
- [ ] 增加 `NOTIFY_VALUE_MASK_STOP`，广播给所有 element。
- [ ] element 收到 STOP 后关闭文件、清理 decoder 状态、停止 I2S 写入并退出 task。
- [ ] 确保 `stop -> deinit -> init -> run` 可以重复执行。

### P1 - 音量控制

- [ ] `dashboard.h` 增加 `_Atomic int volume` 字段，范围为 0-100。
- [ ] 增加 `AMP_DASH_LOAD_VOLUME` 和 `AMP_DASH_SET_VOLUME` 宏。
- [ ] `element_priv.h` 增加 `NOTIFY_VALUE_MASK_VOLUME`。
- [ ] `amp_event.h` 增加 `AMP_EVENT_ACTION_SET_VOLUME`。
- [ ] `controller.h/c` 增加 `amp_controller_action_set_volume(controller, int volume)`。
- [ ] `i2s_writer.c` 处理 VOLUME notify，缓存当前音量。
- [ ] `i2s_writer.c` 在 PCM 写入前使用 Q16 定点缩放应用音量。
- [ ] `sin_pcm_reader.c` 读取 dashboard 音量，替代自身配置中的固定 volume。

### P2 - 播放控制增强

- [ ] 完整验证 pause/resume 时 file reader 保留 fd 和文件偏移。
- [ ] 完整验证 pause/resume 时 audio decoder 保留 decoder、raw input 和 output 状态。
- [ ] 完整验证 I2S DMA 重启后能从 ringbuf 残量继续输出。
- [ ] 为上层 next/previous/seek 预留可打断当前 stream 的内部能力。

### P3 - Seek 支撑

- [ ] 调研 `esp_audio_simple_dec` 对 seek 或帧级恢复的支持程度。
- [ ] 设计 file reader 文件偏移与 PCM 时间的映射关系。
- [ ] 明确 seek 时清空 ringbuf、drain ringbuf 或 abort ringbuf 的策略。
- [ ] 定义 seek 后 decoder reset/reopen 的最小流程。

## 优化项

### P0 - EOS 收敛

- [ ] `amp_controller` 等待 `dash->done_count` 时从 `portMAX_DELAY` 改为有界超时。
- [ ] EOS 等待超时后将状态切到 `AMP_STATE_FATAL`。
- [ ] EOS 超时后广播状态变更，使 element 不会永久阻塞。
- [ ] 所有 element done 收敛后再统一重置 ringbuf done 标记。
- [ ] 保证每个 stream 的本地 EOS latch 都能在 `EOS_DONE` 后清除。
- [ ] `playlist` 增加循环控制。

### P1 - I2S 输出完整性

- [ ] `amp_i2s_writer` 在 `RB_DONE` 后、`AMP_EL_SEND_DONE` 前等待 I2S DMA 输出完成。
- [ ] 优先使用 `i2s_channel_wait_all_done()` 或等效带超时等待。
- [ ] 避免最后一个 PCM 样本被截断。
- [ ] 避免下一首重配 I2S 参数时上一首 PCM 尚未 drain 完。

### P1 - Ringbuf 反压

- [ ] `file_reader` 中 `rb_write()` 连续 timeout 后主动降速或让出 CPU。
- [ ] `audio_decoder` 输出 ringbuf 写满时增加更明确的等待或降级策略。
- [ ] 评估是否新增 `AMP_EVENT_REPORT_BACKPRESSURE`。
- [ ] 为 producer 长时间反压增加诊断日志，但避免热路径刷屏。

### P1 - 资源管理

- [ ] 多次播放不同文件时验证无累积内存泄漏。
- [ ] 多次 init/deinit 时验证 task、event handler、ringbuf 和 semaphore 都被释放。
- [ ] `amp_dashboard_deinit()` 删除 `media_sem` 和 `done_count`。
- [ ] controller 初始化失败时完整释放 event loop、dashboard 和已创建资源。
- [ ] element append 失败时释放新建 ringbuf，避免半初始化泄漏。

### P2 - Audio Detail 联动

- [ ] 补充不同采样率、位宽、声道的连续文件播放验证。
- [ ] 明确 `AUDIO_DETAIL` 事件的触发时机，避免相同参数重复重配。
- [ ] 处理 I2S 重配失败后的回退策略。
- [ ] 将 decoder 上报的 bitrate、sample rate、channel、bit width 暴露给上层状态查询。

### P2 - Sin PCM Reader 规范化

- [ ] 接入与其他 element 一致的 task state：`cached_state`、`event_wait_ticks`、`stop_requested`。
- [ ] 让 `process_notify` 逻辑与 writer/decoder 保持一致。
- [ ] 增加 `frames_remaining`，支持生成有限帧后 `rb_done_write()` 和 `AMP_EL_SEND_DONE`。
- [ ] 将其用于不依赖真实音频文件的端到端测试。

### P2 - Ringbuf 零拷贝

- [ ] 设计并实现 `rb_read_acquire()` 和 `rb_read_consume()`。
- [ ] 设计并实现 `rb_write_acquire()` 和 `rb_write_commit()`。
- [ ] 处理 ringbuf wraparound，只返回当前连续可读或可写区域。
- [ ] 保持 `can_read`、`can_write` 和 lock 语义不破坏现有读写 API。
- [ ] `i2s_writer` 用 acquire/consume 替代 `rb_read()` 到临时 buffer 的 memcpy。
- [ ] 处理 `i2s_channel_write()` 部分写入，只 consume 实际写入字节。

### P2 - Buffer 与任务调优

- [ ] 基于真实 MP3 播放调优 reader 读块大小。
- [ ] 基于 decoder 输出延迟调优 decoder 输入/输出 buffer。
- [ ] 基于 I2S 写入稳定性调优 writer 读块大小。
- [ ] 调整 reader、decoder、writer、controller task 优先级和 stack size。
- [ ] 降低稳定路径上的 INFO/WARN 日志频率。

### P3 - 格式扩展支撑

- [ ] 运行时格式识别从扩展名映射扩展到 header sniffing。
- [ ] 验证 AAC 链路。
- [ ] 验证 FLAC 链路。
- [ ] 增加 WAV PCM 直通路径，允许跳过 decoder。

### P3 - 测试体系

- [ ] `ringbuf`：补充边界条件、并发读写、abort、done、timeout 测试。
- [ ] `controller`：补充管线组装、EOS 收敛、状态切换、FATAL 测试。
- [ ] `audio_decoder`：补充 EOS flush、格式切换、OOM buffer 扩容、损坏输入测试。
- [ ] `file_reader`：补充空目录、无支持文件、损坏文件、混合格式目录测试。
- [ ] 增加 `file_reader -> audio_decoder -> devnull_writer` 集成测试，避免依赖 I2S 外设。
- [ ] 增加连续两首不同音频参数文件的集成测试。
- [ ] 增加 pause/resume 循环测试。
- [ ] 增加 FATAL 注入测试，覆盖 I2S、decoder、file reader 失败。

## Bug 修改项

### P0 - Controller EOS 死锁

- [ ] 修复 `controller.c` 中 EOS 等待无超时导致永久阻塞的问题。
- [ ] 修复某个 element 未发送 `AMP_EL_SEND_DONE` 时 controller 无法恢复的问题。
- [ ] 修复 EOS 收敛失败后仍可能继续 reset ringbuf done 标记的问题。

### P0 - FATAL 事件未闭环

- [ ] 修复 `AMP_EVENT_REPORT_FATAL` 已定义但 controller 未处理的问题。
- [ ] 修复 element 进入 FATAL 后 task loop 不退出或不降载的问题。
- [ ] 修复 FATAL 状态下 toggle/play/pause 行为不一致的问题。

### P1 - File Reader 退出清理

- [ ] 修复 `file_reader` 读文件错误退出时可能不关闭当前 fd 的问题。
- [ ] 修复 `amp_file_reader_deinit()` 遍历释放链表时不使用安全遍历的问题。
- [ ] 修复目录条目进入播放列表后可能被当作普通播放项反复跳过的问题。
- [ ] 修复无可播放文件时 reader task 反复空转的问题。

### P1 - Audio Decoder 错误处理

- [ ] 修复连续 decode 失败只累加计数但不触发恢复或上报的问题。
- [ ] 修复输出 buffer 扩容失败后缺少明确终止策略的问题。
- [ ] 修复 `audio_codec_register()` 中未使用且表达式可读性差的问题，最终保留单一路径注册 decoder。

### P1 - I2S Writer 错误处理

- [ ] 修复 I2S 写入重试失败后只记录日志、不上报错误的问题。
- [ ] 修复 I2S 重配失败后 channel enable 状态可能不一致的问题。
- [ ] 修复 `detail.channel` 日志格式与类型不匹配的问题。

### P1 - Dashboard 资源释放

- [ ] 修复 `amp_dashboard_deinit()` 未删除 `media_sem` 的问题。
- [ ] 修复 `done_count` 创建后未在 dashboard/controller deinit 中统一释放的问题。

### P2 - Ringbuf 边界行为

- [ ] 检查 `rb_read()` 中 4 字节对齐读取策略在非 I2S consumer 下是否会造成延迟或尾部数据处理异常。
- [ ] 检查 `rb_reset()` 和 `rb_reset_is_done_write()` 在并发读写中的锁保护是否足够。
- [ ] 检查 `rb_destroy()` 在 reader/writer task 未退出时被调用的风险。

### P2 - 已修复问题记录

- [x] `controller.c` 中 `sizeof(amp_controller_handle_t)` 已修正为 `sizeof(struct amp_controller)`。
- [x] `file_reader.c` 中 retry 计数不递增导致无限重试的问题已修复。
- [x] `i2s_writer.c` 中 `RB_DONE` 不发送 `AMP_EL_SEND_DONE` 导致 controller 永久阻塞的问题已修复。
- [x] `controller.c` 中 `ULLONG_MAX` 传参导致 overflow warning 的问题已修复。
- [x] `audio_decoder.c` 中同类型第二首不 reset decoder 的问题已修复。
- [x] `devnull_writer.c` 中本地 EOS latch 不因 `EOS_DONE` 清除的问题已修复。
- [x] `file_reader.c` 中扩展名比较已从 `strcmp` 改为 `strcasecmp`。
- [x] `audio_decoder.c` 中缺失 `esp_audio_dec_register_default()` 的问题已修复。

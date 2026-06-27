# AMP Component TODOs

> Updated: 2026-06-28. Reflects post-refactor state after naming/logging/API unification.

## Completed ✅

### infrastructure
- [x] `ringbuf.c/h` — ring buffer with `rb_read`/`rb_write`/`rb_done_write`/`rb_reset_is_done_write`
- [x] `amp_mem.c/h` — PSRAM-aware allocator
- [x] `dashboard.c/h` — atomic state machine (`READY`/`PLAYING`/`PAUSE`/`FATAL`) with counting semaphore for EOS convergence
- [x] `element.h` — unified `amp_element_interface_t`, `amp_element_task_config_t` (includes `intf`), `amp_element_role`
- [x] `element_priv.h` — internal `struct amp_element`, notify masks, `AMP_EL_SEND_DONE`

### event system
- [x] `amp_event.h` — event bases and IDs: `STREAM_EOS`, `AUDIO_FORMAT`, `AUDIO_DETAIL`, `FATAL`
- [x] `controller.c` — event loop, report handler, EOS convergence (`NOTIFY_VALUE_MASK_EOS_DONE` broadcast to all elements)
- [x] Elements receive `EOS_DONE` via task notify to clear per-stream done latches

### controller
- [x] Pipeline assembly: `append_reader`/`append_writer`/`append_processor` with automatic ringbuf linking
- [x] Action dispatch via task notify: `play`/`pause`/`reset`/`toggle`
- [x] Resource cleanup in `deinit`: event loop, dashboard, ringbuf list, element resources

### elements
- [x] `amp_file_reader` — directory scan + sequential file read + `STREAM_EOS`/`AUDIO_FORMAT` event posting
- [x] `amp_audio_decoder` — `esp_audio_simple_dec` wrapper with reset/reopen across tracks, EOS flush, buffer expansion
- [x] `amp_i2s_writer` — I2S output with EOS latch, retry with hard-error detection, `write_pcm` API
- [x] `amp_devnull_writer` — null sink for testing, same EOS latch as i2s writer
- [x] `amp_sine_pcm_reader` — sine wave test source (legacy, kept for basic validation)

### verification
- [x] `sin_pcm_reader -> i2s_writer` audio test in `main/src/player.c`
- [x] `file_reader -> audio_decoder -> i2s_writer` minimal player test in `test_apps/amp`
- [x] Both `test_apps/amp` and main firmware build cleanly (zero warnings)

---

## P0 — 核心稳定性

### 暂停停止 I2S DMA
- [ ] `amp_i2s_writer` 在 `cached_state` 从 PLAYING → PAUSE 时调用 `i2s_channel_disable(tx_chan)`
- [ ] 从 PAUSE → PLAYING 时调用 `i2s_channel_enable(tx_chan)`
- [ ] 需要在 task state 中增加 `prev_state` 字段来检测状态变迁
- [ ] 暂停和 `waiting_eos_done` 期间不调用 `i2s_channel_write`

### FATAL 错误恢复
- [ ] 各 element 在硬错误时发送 `AMP_EVENT_REPORT_FATAL`：
  - `i2s_writer`：`ESP_ERR_INVALID_STATE` 后重试仍失败
  - `audio_decoder`：连续 decode 失败计数超阈值
  - `file_reader`：`read()` 返回错误不可恢复
- [ ] `amp_controller_handle_report_event` 处理 `FATAL` 事件：切状态为 `AMP_STATE_FATAL`
- [ ] element 检测到 `cached_state == FATAL` 时退出 task loop
- [ ] `amp_controller_action_reset` 恢复路径：重设状态 → READY，允许外部重新调用 `amp_controller_run()`

### EOS 收敛超时
- [ ] `xSemaphoreTake(dash->done_count, ...)` 从 `portMAX_DELAY` 改为带超时（如 5000ms）
- [ ] 超时后强制切 `AMP_STATE_FATAL`，广播 `STATE` 给所有 element 使其退出
- [ ] 防止某个 element 永不 `AMP_EL_SEND_DONE` 导致 controller 永久死锁

---

## P1 — 数据流稳定性

### ringbuf 反压
- [ ] `file_reader` 中 `rb_write` 连续 timeout 后主动降速：增大 `event_wait_ticks`，让 consumer 有时间消费
- [ ] `audio_decoder` 中 output ringbuf 写满时的处理：当前仅 retry，应增加 consumer 同步等待或跳过当前帧的降级策略
- [ ] 考虑：producer 持续被反压时是否应通知 controller（新增 `AMP_EVENT_REPORT_BACKPRESSURE`）

### EOS 后 I2S drain
- [ ] `amp_i2s_writer` 在 `RB_DONE` 后、`AMP_EL_SEND_DONE` 之前等待 I2S DMA 完成
- [ ] 使用 `i2s_channel_wait_all_done()` 或等效超时等待（如 100ms）
- [ ] 确保最后一个 PCM 样本完整输出后才进入下一首，避免尾部截断

### 内存 / 资源
- [ ] 多次播放不同文件无累积泄漏
- [ ] 全链路 `stop -> deinit -> init -> run` 可重复执行

---

## P2 — 播放控制增强

### pause / resume 完整实现
- [ ] 暂停时保留播放位置（file_reader 文件偏移不做 seek，保持 fd 位置）
- [ ] `audio_decoder` 暂停时保留 `raw_dec` 和 `out_dec` 状态
- [ ] resume 后不影响数据连续性（I2S DMA 重启后从 ringbuf 残量续播）

### seek
- [ ] 解码器 seek 接口调研（`esp_audio_simple_dec` 支持程度）
- [ ] 文件偏移与 PCM 时间的映射关系
- [ ] seek 时 ringbuf 清空策略：`rb_reset` 还是 `rb_reset_is_done_write` + 等 consumer 消费完

---

## P3 — 音频参数联动

### decoder → writer 参数传递
- [ ] `audio_decoder` 首次解码成功后获取实际采样率/位宽/声道数
- [ ] post `AMP_EVENT_REPORT_AUDIO_DETAIL` 携带 `amp_event_report_audio_args`
- [ ] `amp_i2s_writer` 订阅 `AUDIO_DETAIL` 事件，收到后重配 I2S
- [ ] 重配时机：在当前 EOS 收敛完成、下一首 decoder 输出首帧 PCM 之前（避免 DMA 切参数导致尾部受损）
- [ ] 不同采样率文件连续播放时自动切换，切换失败回退策略

### sin_pcm_reader 规范化
- [ ] 接入标准 task state 结构：`cached_state` / `event_wait_ticks` / `stop_requested`
- [ ] `process_notify` 逻辑和其他 writer 一致（当前始终返回 true）
- [ ] 增加 `frames_remaining` 支持有限帧生成后 `rb_done_write` + `AMP_EL_SEND_DONE`
- [ ] 用于端到端测试，不依赖真实音频文件

### stop_requested 清理
- [ ] 删除各 element 中未接入的 `stop_requested` 字段，或新增 `amp_controller_stop()` 统一设置
- [ ] 如果新增 stop API：`xTaskNotify(el->task, NOTIFY_VALUE_MASK_STOP, eSetBits)` 广播，element 收到后退出 task

---

## P4 — 测试体系

### 单元测试
- [ ] `ringbuf` — 边界条件、concurrent read/write、abort 路径
- [ ] `controller` — pipeline 组装、EOS 收敛、状态切换
- [ ] `audio_decoder` — EOS flush、格式切换、OOM buffer 扩容
- [ ] `file_reader` — 空目录、损坏文件、格式混合目录

### 集成测试
- [ ] `file_reader -> audio_decoder -> devnull_writer` 全链路测试（无需 I2S 外设）
- [ ] 连续两首不同类型文件播放验证
- [ ] pause/resume 循环测试
- [ ] FATAL 注入测试（模拟 I2S 失败、decoder 失败等）

---

## P5 — ringbuf 零拷贝

### API 设计
```c
// 零拷贝读：返回可读区域的起始指针和长度，不复制数据
// 调用者直接通过指针访问 ringbuf 内部数据（直到调用 rb_read_consume）
int rb_read_acquire(ringbuf_handle_t rb, char **buf, int len, TickType_t ticks_to_wait);
// 返回值：实际可读字节数（>0），RB_TIMEOUT，RB_DONE，RB_ABORT

// 零拷贝读提交：消费 n 字节，推进读指针
// 调用 rb_read_acquire 后必须配对调用 rb_read_consume
esp_err_t rb_read_consume(ringbuf_handle_t rb, int consumed);

// 零拷贝写：返回可写区域的起始指针和长度
int rb_write_acquire(ringbuf_handle_t rb, char **buf, int len, TickType_t ticks_to_wait);
esp_err_t rb_write_commit(ringbuf_handle_t rb, int written);
```

### 实现要点
- [ ] `rb_read_acquire` 内需处理 wraparound 情况：如果连续可读区域不够 len，返回实际连续字节数
- [ ] `rb_read_consume` 仅推进 `p_r` 和减小 `fill_cnt`，保证 read_acquire 返回的指针仍然有效（直到下次 acquire）
- [ ] `rb_write_acquire` / `rb_write_commit` 同理
- [ ] 信号量控制逻辑不变：`can_read` 在有数据时释放，`can_write` 在有空间时释放

### `i2s_writer` 接入零拷贝
- [ ] 将 `rb_read(rb, read_buf, ...)` 替换为 `rb_read_acquire` → `i2s_channel_write(writer->tx_chan, buf, len)` → `rb_read_consume`
- [ ] 消除 ringbuf 内部 buffer 到 `read_buf` 的 `memcpy`（当前 1024 bytes per read）
- [ ] 预期收益：减少 CPU 负载和栈/堆内存占用

### 考虑
- [ ] 零拷贝读时如果 `i2s_channel_write` 部分写入（`written < len`），`rb_read_consume` 只消费实际写入量
- [ ] 多 consumer 场景下零拷贝不安全（当前只有一个 consumer per ringbuf，不是问题）

---

## P6 — 播放列表与格式扩展

### playlist
- [ ] 当前 `file_reader` 按目录顺序播放全部文件，需要更灵活的播放顺序
- [ ] 支持单曲循环、目录循环、随机播放模式
- [ ] 支持 `next()`/`prev()`/`seek_to(index)` 跳转接口

### 文件格式
- [ ] 运行时格式识别（不只是扩展名映射，可考虑 header sniffing）
- [ ] AAC/FLAC 链路验证
- [ ] WAV 支持（PCM 直通，跳过 decoder）

### 性能
- [ ] 音频 buffer 大小调优（当前硬编码 1024/2048）
- [ ] decoder 输出攒批策略，减少 ringbuf write 次数
- [ ] task 优先级调优

---

## P7 — UI 集成

- [ ] 接入选歌界面：浏览目录 → 选择文件 → 播放
- [ ] 播放状态反馈（进度、曲目名、播放/暂停状态）
- [ ] 按键映射：play/pause/toggle、next/prev、volume

---

## BUGS 已修复 (since last review)

| 严重度 | 文件 | 问题 | 状态 |
|--------|------|------|------|
| 🔴 | `controller.c` | `sizeof(amp_controller_handle_t)` → `sizeof(struct amp_controller)` | ✅ |
| 🟡 | `file_reader.c` | `retry_count` 不递增导致无限重试 | ✅ |
| 🟡 | `i2s_writer.c` | `RB_DONE` 不发送 `AMP_EL_SEND_DONE` 导致 controller 永久阻塞 | ✅ |
| 🟡 | `controller.c` | `ULLONG_MAX` 传参导致 overflow 警告 | ✅ |
| 🟡 | `audio_decoder.c` | 同类型第二首不 reset decoder | ✅ |
| 🟡 | `devnull_writer.c` | `is_done` 本地 latch 不因 EOS_DONE 清除 | ✅ |
| 🟢 | `file_reader.c` | `strcmp` → `strcasecmp` | ✅ |
| 🟢 | `audio_decoder.c` | `esp_audio_dec_register_default()` 缺失 | ✅ |

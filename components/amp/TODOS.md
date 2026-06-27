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

## P0 — 播放稳定性

### 错误恢复
- [ ] I2S 写持续失败时的整体流水线降级或停止策略
- [ ] `audio_decoder` decode 连续失败后的 recovery 路径
- [ ] `file_reader` 文件损坏/截断时的处理
- [ ] controller 收到 `FATAL` 报告后的状态迁移与资源回收

### EOS 时序
- [ ] `i2s_writer` 在 `RB_DONE` 后 drain I2S DMA 缓冲区再 `AMP_EL_SEND_DONE`
- [ ] 确保最后一个 PCM 样本完整输出后才进入下一首，避免截断

### 内存 / 资源
- [ ] 多次播放不同文件无累积泄漏（valgrind/heap tracing）
- [ ] 全链路 `stop -> deinit -> init -> run` 可重复执行

---

## P1 — 多文件播放

### playlist
- [ ] 当前 `file_reader` 按目录顺序播放全部文件，需要更灵活的播放顺序
- [ ] 支持单曲循环、目录循环、随机播放模式
- [ ] 支持 `next()`/`prev()`/`seek_to(index)` 跳转接口

### 文件格式
- [ ] 运行时格式识别（不只是扩展名映射，可考虑 header sniffing）
- [ ] AAC/FLAC 链路验证
- [ ] WAV 支持（PCM 直通，跳过 decoder）

---

## P2 — 播放控制增强

### pause / resume
- [ ] 暂停时保留播放位置（文件偏移 + decoder 内部状态）
- [ ] resume 后无缝继续，不丢帧不爆音
- [ ] `i2s_writer` 在 pause 时停止 DMA，resume 时重启

### seek
- [ ] 解码器 seek 接口 (`esp_audio_simple_dec` 是否支持)
- [ ] 文件偏移与 PCM 时间的映射
- [ ] seek 后 ringbuf 清空 + decoder 重新同步

---

## P3 — 音频参数联动

### decoder → writer 参数传递
- [ ] 首次解码后获取实际采样率/位宽/声道数
- [ ] 通过 `AUDIO_DETAIL` 事件通知 writer 重配 I2S
- [ ] 不同采样率文件连续播放时自动切换 I2S 配置
- [ ] 重配置失败时的回退策略

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

## P5 — 优化

### ringbuf 零拷贝
- [ ] `rb_read_acquire()`/`rb_read_consume()` — 零拷贝读
- [ ] `rb_write_acquire()`/`rb_write_commit()` — 零拷贝写
- [ ] `i2s_writer` 接入零拷贝读路径，消除 ringbuf → DMA buffer 的 memcpy

### 性能
- [ ] 音频 buffer 大小调优（当前硬编码 1024/2048）
- [ ] decoder 输出攒批策略，减少 ringbuf write 次数
- [ ] task 优先级调优

---

## P6 — UI 集成

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

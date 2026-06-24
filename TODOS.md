# AMP Component TODOs

> Updated: 2026-06-25. Reflects current `components/amp/` pipeline state after ringbuf/dashboard/controller action work.

## Completed ✅

### infra
- [x] `ringbuf.c` / `ringbuf.h` — ESP-ADF 风格环形缓冲区，支持 `rb_read` / `rb_write` / `rb_abort` / `rb_done_write` / `rb_unblock_reader`
- [x] `amp_mem.c` / `amp_mem.h` — PSRAM 感知的内存分配器（`amp_malloc` / `amp_calloc` / `amp_realloc`）
- [x] `dashboard.c` / `dashboard.h` — 原子状态管理，`READY` / `PLAYING` / `PAUSE` / `FATAL`
- [x] `element.h` — 公开的 `amp_element_interface_t` + `amp_element_role` + `AMP_ELEMENT_ENTRY()`
- [x] `element_priv.h` — 内部结构体 `struct amp_element` + `AMP_EVENT_ACTION` 定义

### controller
- [x] `amp_controller_init()` / `amp_controller_run()` — 创建 event loop + 启动 element task
- [x] `amp_controller_append_reader()` / `amp_controller_append_writer()` — 公开 append API
- [x] 内部 `amp_controller_append()` 已处理 `READER` / `PROCESSOR` / `WRITER` 三种 role 的 rb 串联
- [x] `amp_controller_action_play()` / `_pause()` / `_reset()` / `_toggle_play()` — 状态切换 + 事件发送

### elements
- [x] `sin_pcm_reader` — 正弦波测试源，task 中生成 PCM 写入 ringbuf
- [x] `i2s_writer` — I2S 输出 element，task 中从 ringbuf 读数据写入 I2S，支持 event handler
- [x] `audio_codec` — 基础解码 loop 可工作（`test_audio_codec.c` 验证），基于 `esp_audio_simple_dec`
- [x] `file_reader` — 目录扫描 + 单文件流读取可工作

### verification
- [x] `sin_pcm_reader -> i2s_writer` 两元素流水线在 `main/src/player.c` 中可跑，出声

---

## P0 — 核心闭环：`file_reader -> audio_codec -> i2s_writer`

### controller 公开接口补齐
- [ ] 在 `controller.h` 中公开 `amp_controller_deinit()` （`.c` 中已实现）
- [ ] 在 `controller.h` 中公开 `amp_controller_append_processor()` （内部逻辑已有，缺少 wrapper）
- [ ] 修复 `amp_controller_init()` 第 225 行：`amp_malloc(sizeof(amp_controller_handle_t))` → `amp_malloc(sizeof(struct amp_controller))`
- [ ] `amp_controller_deinit()` 中增加 `esp_event_loop_delete(controller->event_bus)`
- [ ] `amp_controller_deinit()` 中增加 `amp_dashboard_deinit(controller->dashboard)`

### audio_codec 完善
- [ ] 在 `audio_codec_task_run()` 中增加 EOS 处理：读空 + `is_done_write` → flush decoder → `RB_DONE`
- [ ] 在 `audio_codec_task_run()` 中增加 dashboard 状态检查（PAUSE / FATAL）
- [ ] 补齐 `amp_element_interface_t.deinit` 到 vtable（当前 NULL）
- [ ] 补齐 `amp_element_interface_t.setup_event_handler` 到 vtable
- [ ] 解码输出 buffer 改为动态扩容（`BUFF_NOT_ENOUGH` → `amp_realloc`）
- [ ] 格式设置从硬编码 MP3 改为运行时可切换（预留 `set_format` 接口）
- [ ] 注册 raw decoder：`esp_audio_dec_register_default()` 与 `esp_audio_simple_dec_register_default()` 都要调

### file_reader 完善
- [ ] `file_reader` 保持为 **目录扫描 + 文件流读取** 合一，不做拆分
- [ ] task 中补齐：`fopen` → 分块 `fread` → `rb_write` → `fclose` → `rb_done_write`
- [ ] 修复 `file_reader_deinit()` 中 `amp_free(fr)` 缺失（泄漏 `file_reader` 自身）
- [ ] 扩展名过滤改为 `strcasecmp`（当前 `strcmp` 大小写敏感）

### i2s_writer 完善
- [ ] 补齐 `amp_element_interface_t.deinit` 到 vtable（`i2s_writer_element_deinit` 已存在但未挂钩）
- [ ] `i2s_writer_report_event_handler` 实现 REPORT 事件响应（目前是空 switch）
- [ ] 增加 EOS 后的 drain 策略：收到 report 后等 ringbuf 排空再停止

### P0 验收
- [ ] 通过 controller 串起 `file_reader -> audio_codec -> i2s_writer`
- [ ] 可以稳定播放 `/storage/music/test.mp3`
- [ ] 不再依赖测试代码手动调用 `set_input_rb()` / `set_output_rb()`
- [ ] 播放结束（EOS）后 pipeline 正确回收，不卡死不泄漏

---

## P1 — 稳定性与事件驱动

### event system
- [ ] 实现 `components/amp/controller.c` 中的 `amp_controller_handle_report_event()`（当前 TODO stub）
- [ ] 实现 `sin_pcm_reader_report_event_handler`（当前空 switch）
- [ ] 实现 `i2s_writer_report_event_handler`（当前只有 default: return）
- [ ] 实现 `audio_codec` 的 event handler（目前未注册）
- [ ] 为 `file_reader` 补齐事件通知（EOF → `AMP_EVENT_REPORT_FILE_END`）
- [ ] 将当前 dashboard 轮询逻辑逐步收敛到事件驱动

### dashboard state machine
- [ ] 文档化 `READY → PLAYING → PAUSE / FATAL` 状态图
- [ ] 在 controller 中实现 `REPORT` 事件接收后的状态迁移（FATAL → READY 等）
- [ ] 明确 `AMP_EVENT_ACTION_RESET` 对 ringbuf、decoder、文件位置的影响

### cleanup & lifecycle
- [ ] 确保 `init -> append -> run -> stop -> deinit` 可重复执行不漏资源
- [ ] 为 `audio_codec` / `i2s_writer` / `file_reader` 补齐 `deinit` 接口挂接到 vtable
- [ ] controller deinit 时正确释放：event loop、dashboard、rb_list、element 私有资源
- [ ] 验证多次播放不同文件不会累积内存泄漏

---

## P2 — 输出参数联动

### audio_codec → i2s_writer 参数传递
- [ ] 从 decoder 获取实际输出参数：采样率、位宽、声道数
- [ ] 通过 `AMP_EVENT_REPORT` 上报音频参数变化
- [ ] 由 controller 或 writer 消费参数变化并调用 `i2s_writer_audio_config()`

### i2s_writer_audio_config
- [ ] 补齐 I2S 重配置时的 disable / reconfig / enable 时序
- [ ] 明确重配置失败时的回退或报错策略
- [ ] 验证不同采样率文件输出时不会出现静音、爆音或写失败

---

## P3 — 文件与播放源能力

### file_reader / playlist
- [ ] `file_reader` 内置目录扫描能力，单文件播放稳定后增加顺序播放
- [ ] 为下一首、上一首、自动切歌预留接口或状态设计
- [ ] 明确播完一首后的切换行为与状态更新逻辑

### format support
- [ ] 在 MP3 主链路稳定后再考虑 FLAC / AAC / WAV 支持
- [ ] `audio_codec` 增加运行时格式切换：`audio_codec_set_format(handle, dec_type)`
- [ ] 设计最小可用的格式识别（从文件扩展名映射到 `esp_audio_simple_dec_type_t`）

---

## P4 — 测试体系

### component tests
- [ ] `controller` — 流水线组装和状态切换测试
- [ ] `file_reader` — 扩展名过滤和目录扫描测试
- [ ] `audio_codec` — EOS、异常输入、buffer 扩容测试
- [ ] `ringbuf` — rb_read/rb_write 基础边界和异常路径测试

### end-to-end tests
- [ ] 在 `test_apps/amp` 中增加完整流水线测试
- [ ] 增加 mock PCM sink，减少端到端测试对真实 I2S 外设的依赖
- [ ] 覆盖最小验证链路：`test.mp3 → codec → sink`
- [ ] 覆盖 pause/resume/eof/error 场景

---

## P5 — 优化与扩展

### ringbuf 零拷贝
- [ ] 新增 `rb_read_acquire()` / `rb_read_consume()` API
- [ ] 新增 `rb_write_acquire()` / `rb_write_commit()` API
- [ ] 在 `i2s_writer` 中接入零拷贝读路径（消除 ringbuf → I2S 之间的 memcpy）

### public API 清理
- [ ] 清理只应内部使用的接口，收敛公开头文件边界
- [ ] 统一 reader / writer / codec / processor 命名风格
- [ ] 复查 `element_priv.h` 与公开 API 的职责分界
- [ ] 修复 `element_priv.h` 中 `event_bus` 字段类型：`esp_event_handler_t` → `esp_event_loop_handle_t`

### UI integration
- [ ] 在播放主链路稳定后，接入 UI 的 `play / pause / toggle` 控制
- [ ] 后续结合 SD 卡浏览、OLED 显示补齐交互层能力

---

## BUGS 待修复

| 严重度 | 文件 | 问题 |
|--------|------|------|
| 🔴 | `controller.c:225` | `amp_malloc(sizeof(amp_controller_handle_t))` 分配的是指针大小而非结构体大小 |
| 🟡 | `element_priv.h:31` | `esp_event_handler_t event_bus` 类型错误，应为 `esp_event_loop_handle_t` |
| 🟡 | `file_reader.c:206` | `file_reader_deinit()` 缺少 `amp_free(fr)` |
| 🟡 | `i2s_writer.c:86` | `.deinit` 未挂接到 vtable（`i2s_writer_element_deinit` 存在但未用） |
| 🟡 | `audio_codec.c` | `.deinit` / `.setup_event_handler` 在 vtable 中为 NULL |
| 🟡 | `controller.c` | deinit 未释放 event loop 和 dashboard |
| 🟢 | `file_reader.c` | 扩展名过滤用 `strcmp` 应改为 `strcasecmp` |
| 🟢 | `audio_codec.c:100` | 缺少 `esp_audio_dec_register_default()` 调用 |

---

## 里程碑

### M1 — 可播单文件
- [ ] 指定 `/storage/music/test.mp3` 可稳定播放完成
- [ ] 通过 controller 正式 pipeline（非手动拼装）

### M2 — 可控可收尾
- [ ] 支持 `play / pause / reset` 全路径
- [ ] 播放结束和异常退出都能正确回收资源

### M3 — 可回归
- [ ] 具备基础端到端测试和关键模块回归测试

### M4 — 可扩展
- [ ] 支持目录或播放列表能力
- [ ] 为 UI 集成提供稳定接口
- [ ] 支持 MP3 以外的音频格式

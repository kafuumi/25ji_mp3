# 25ji_mp3 TODO

> 更新日期：2026-06-29。本文档是项目级路线图，目标是把当前固件推进为可用的 MP3 播放器。

## 范围说明

- MVP 阶段优先实现从 SD 卡挂载点 `/sdcard` 播放 MP3。
- AAC、FLAC、WAV、UI、seek、播放列表模式和可配置播放目录先降级到后续阶段。
- `components/amp/TODOS.md` 作为 `amp` 组件内部细节清单，记录音频管线内部实现、稳定性和测试项。

## 已完成

- [x] `components/bsp/bsp.h` 已集中定义 SD、I2C、I2S、静音和按键引脚。
- [x] `components/bsp/bsp_sd_card.c` 已实现 SD 卡挂载能力，当前项目配置使用 SPI 模式。
- [x] 已通过 `bsp_audio_mute()` 提供音频静音控制。
- [x] `amp` 组件已具备 reader、processor、writer 组成的控制器管线模型。
- [x] `amp_file_reader` 已支持扫描目录并顺序读取 `.mp3`、`.aac`、`.flac` 文件。
- [x] `amp_audio_decoder` 已封装 `esp_audio_simple_dec`，并支持 MP3 解码器选择。
- [x] `amp_i2s_writer` 已支持通过 I2S 输出 PCM，并能根据 decoder 上报的音频参数重配输出。
- [x] `test_apps/amp` 已提供带 FATFS 测试资源的音频管线测试应用。
- [x] 主固件当前已能通过 `sin_pcm_reader -> i2s_writer` 验证音频输出。

## 功能项

### P0 - MVP MP3 播放

- [ ] 将主固件中的正弦波音频 demo 替换为真实 MP3 播放流程。
- [ ] 启动时完成 BSP 初始化、音频取消静音和 SD 卡挂载。
- [ ] 固定扫描播放根目录 `/sdcard` 下的 MP3 文件。
- [ ] 在主固件中搭建 `file_reader -> audio_decoder -> i2s_writer` 管线。
- [ ] 管线初始化成功后启动 controller，并自动进入 PLAYING 状态。
- [ ] `/sdcard` 下没有 MP3 文件时输出明确日志，并让播放器保持安全空闲状态。

### P0 - Player 层封装

- [ ] 新增项目级 player 模块，统一持有 file reader、decoder、writer 和 controller 句柄。
- [ ] 提供最小 API：`player_init()`、`player_start()`、`player_pause()`、`player_toggle()`、`player_deinit()`。
- [ ] 让 `main/src/main.c` 只负责板级初始化和播放器启动。
- [ ] 初始化中途失败时清理已经创建的句柄，避免泄漏。

### P1 - 播放控制

- [ ] 实现 SD 文件播放下可靠的 pause/resume，并保持当前文件偏移。
- [ ] 在 `amp_controller` reset 路径稳定后增加明确的 stop/reset 行为。
- [ ] 播放列表导航实现后增加 next/previous 曲目动作。
- [ ] 增加播放器状态查询接口，供 UI 和诊断日志使用。

### P2 - 播放列表

- [ ] 在 `amp_file_reader` 之上增加播放列表抽象，避免直接依赖目录遍历顺序。
- [ ] 支持下一首和上一首。
- [ ] 支持单曲循环和目录循环。
- [ ] 支持随机播放。
- [ ] 支持按索引选择曲目。

### P2 - 音量控制

- [ ] 在 `amp_dashboard` 中增加全局音量状态。
- [ ] 增加 `amp_controller_action_set_volume(controller, volume)`，校验 0-100 范围。
- [ ] 在 `amp_i2s_writer` 中使用定点 PCM 缩放实现音量控制。
- [ ] 线性音量稳定后评估对数音量映射。
- [ ] 评估音量变化时的短淡入淡出，避免突变噪声。

### P3 - UI

- [ ] 开始 UI 工作时重新启用主固件中的 I2C 初始化。
- [ ] 通过现有 `ui` 组件初始化 OLED。
- [ ] 将按键绑定到播放/暂停、下一首、上一首和音量动作。
- [ ] 显示当前曲目名、播放状态、音频格式和错误状态。
- [ ] 保持 UI 可选，确保无 UI 时也能完成 MP3 播放 bring-up。

### P3 - 格式扩展

- [ ] MP3 播放稳定后再验证 AAC 和 FLAC。
- [ ] 需要混合媒体支持时增加运行时格式识别，不只依赖扩展名。
- [ ] 增加 WAV 支持，作为可绕过 decoder 的 PCM 直通路径。
- [ ] 明确不支持格式的行为：跳过文件、记录原因、继续下一首支持的曲目。

### P4 - Seek

- [ ] 调研 `esp_audio_simple_dec` 是否支持 MP3 seek 或帧级恢复。
- [ ] 设计 MP3 文件偏移到播放时间的映射方式。
- [ ] 明确 seek 时 ringbuf 的清空或 drain 策略。
- [ ] 播放管线稳定后再增加 seek UI 和控制 API。

## 优化项

### P1 - 可配置性

- [ ] 后续将播放根目录改为可配置，默认值仍保持 `/sdcard`。
- [ ] 增加 Kconfig 选项配置播放根目录、初始音量和是否自动播放。
- [ ] 如果调优证明有必要，增加 Kconfig 选项配置 ringbuf 和 decoder buffer 大小。

### P1 - 管线稳定性

- [ ] 在 `amp_controller` 中增加 EOS 收敛超时，避免永久死锁。
- [ ] writer 上报 EOS done 之前等待 I2S DMA drain，避免最后的 PCM 样本被截断。
- [ ] 验证不同采样率和声道布局的多个 MP3 文件连续播放。
- [ ] 验证 SD 卡读取过程中的 pause/resume 循环。

### P1 - 资源管理

- [ ] 验证重复 `init -> play -> stop/deinit` 不泄漏 heap、task、event handler 或 semaphore。
- [ ] 确保所有 amp element 都能在 reset/deinit 时退出 task loop。
- [ ] `dashboard` deinit 时删除其持有的 FreeRTOS semaphore。
- [ ] 增加部分初始化失败路径的清理覆盖。

### P2 - 性能

- [ ] 根据 SD 卡吞吐和 decoder 延迟调优 file reader、decoder、writer buffer 大小。
- [ ] 在真实 MP3 硬件播放跑通后调优 task 栈大小和优先级。
- [ ] 管线稳定后降低热路径中的过量日志。
- [ ] 增加 ringbuf 零拷贝读写 API，减少 memcpy 开销。
- [ ] 零拷贝 API 验证后将 `i2s_writer` 接入零拷贝读取。

### P2 - 测试覆盖

- [ ] 增加不依赖 I2S 硬件的 `file_reader -> audio_decoder -> devnull_writer` 测试。
- [ ] 增加空 `/sdcard`、无 MP3 文件和不支持文件的测试。
- [ ] 增加多个 MP3 文件顺序播放测试。
- [ ] 增加 pause/resume 集成测试。
- [ ] 增加 decoder、file reader、ringbuf 和 I2S writer 的故障注入测试。

### P3 - 硬件 bring-up

- [ ] SDIO 电气问题修复前保持 SD 卡 SPI 模式。
- [ ] CMD/CLK 线路问题确认清楚后再重新评估 SDIO。
- [ ] 验证 PCM5102A 输出电平和后级功放连接。
- [ ] 根据最终功耗和外设方案评估是否需要外接 32 kHz 晶振。

## Bug 修改项

### P0 - SD 卡挂载错误处理

- [ ] 在 `bsp_sd_card_init()` 中保存并检查 `esp_vfs_fat_sdspi_mount()` 返回值。
- [ ] 普通播放器固件中禁用自动格式化，或仅通过明确 debug 选项开启。
- [ ] `/sdcard` 挂载失败时返回清晰启动错误。
- [ ] 启动时记录 SD 挂载模式、引脚和挂载路径。

### P0 - FATAL 错误路径

- [ ] 在 `amp_controller_handle_report_event()` 中处理 `AMP_EVENT_REPORT_FATAL`。
- [ ] element 发生不可恢复错误时将 controller 状态切到 `AMP_STATE_FATAL`。
- [ ] 通知所有 element 进入 FATAL 状态，使 task loop 能安全退出或停驻。
- [ ] 只有在 element 关闭行为确定后，再定义从 FATAL reset 恢复的语义。

### P0 - EOS 死锁

- [ ] 将 `amp_controller` 中 EOS 等待的 `portMAX_DELAY` 改为有界超时。
- [ ] 任一 element 超时未上报 done 时，将 controller 标记为 FATAL。
- [ ] 所有 element 收敛或故障处理完成前，不提前重置 ringbuf done 标记。

### P1 - SDIO 编译路径

- [ ] 如果保留 SDIO 模式可编译，修正 SDIO 分支中的 `BOARD_PIN_SD_*` 为 `BSP_PIN_SD_*`。
- [ ] 增加 SPI 和 SDIO 两个 Kconfig 分支的构建检查，或暂时移除不支持的 SDIO 模式。

### P1 - UI 按键初始化

- [ ] 同时创建 prev 和 next 两个按键句柄。
- [ ] 使用 `BSP_PIN_BTN_PREV` 和 `BSP_PIN_BTN_NEXT`，避免硬编码 GPIO。
- [ ] 为 prev 和 next 注册不同回调。
- [ ] 根据实际板级电路确认 active level 和上下拉配置。

### P1 - File Reader 鲁棒性

- [ ] MVP 路径下 `amp_file_reader_read_dir()` 在没有支持的 MP3 文件时返回错误。
- [ ] 固定 `/sdcard` 扁平播放时避免把目录作为可播放条目，或明确递归扫描行为。
- [ ] 释放 file source 链表时使用安全遍历。
- [ ] file reader 因错误退出时关闭已打开的文件描述符。

### P2 - Audio Decoder 鲁棒性

- [ ] 连续 decoder 失败时转换为 FATAL 上报或跳过当前曲目。
- [ ] 明确损坏 MP3 文件的处理策略：跳过、重试或停止播放。
- [ ] 最终 init 路径确定后移除未使用或重复的 decoder 注册代码。

### P2 - I2S Writer 鲁棒性

- [ ] 重复发生不可恢复 I2S 写入失败后上报 FATAL。
- [ ] 确保 I2S 重配不会发生在上一首残留 PCM 尚未 drain 完成时。
- [ ] I2S disable/enable 过程中正确保持 pause/resume 状态。

### P2 - 硬件问题

- [ ] TF 卡检测引脚未连接 ESP32，因此不支持热插拔检测。
- [ ] 更换不能承受目标电流的错误 10 uH 电感。
- [ ] 启用 SDIO 前继续排查 SDIO CMD/CLK 线路电气问题。

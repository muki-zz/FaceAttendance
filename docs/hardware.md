# FaceAttend 硬件设计与板级接口规范

> 文档类型：Hardware / BSP Reference  
> 适用对象：硬件联调、BSP、驱动维护、故障定位  
> 原则：仅记录当前工程代码或现有板卡资料能够确认的信号；未确认信号标记为“待核对”，不根据芯片通用参考设计臆测。

## 1. 板级概览

FaceAttend 终端以 ESP32-S3 为主控，当前主链路涉及：

- ESP32-S3；
- 外部 PSRAM；
- PCA9557 I/O 扩展器；
- SPI LCD；
- FTx36 电容触摸；
- DVP Camera；
- Wi-Fi；
- 可选音频 Codec / ADC 硬件。

当前考勤主功能不依赖环境传感器或音频模块。早期文档中的环境传感器和智能农业目录不属于当前 FaceAttend 主线。

---

# 2. 已确认 GPIO / 总线映射

## 2.1 公共 I2C

当前 BSP：

| 信号 | ESP32-S3 GPIO | 说明 |
| --- | ---: | --- |
| I2C SDA | GPIO1 | 公共 I2C0 |
| I2C SCL | GPIO2 | 公共 I2C0 |
| I2C Port | I2C_NUM_0 | PCA9557 / Touch 等共享 |

应用层创建公共 bus 后，各组件复用同一 bus，不应重复安装互相独立的 I2C driver。

## 2.2 PCA9557

当前 BSP 使用地址：

```text
0x19
```

已确认扩展引脚：

| PCA9557 IO | 功能 | 说明 |
| --- | --- | --- |
| IO0 | LCD_CS | LCD 片选，不是 ESP GPIO0 |
| IO1 | PA_EN | 音频功放使能，硬件保留 |
| IO2 | DVP_PWDN | Camera Power Down |
| IO3 | EXT_IO1 | 扩展 / 保留 |
| IO4 | EXT_IO2 | 扩展 / 保留 |
| IO5 | EXT_IO3 | 扩展 / 保留 |
| IO6 | EXT_IO4 | 扩展 / 保留 |
| IO7 | EXT_IO5 | 扩展 / 保留 |

Camera PWDN 由 PCA9557 IO2 控制，**不得误写为 ESP32 GPIO2**。

## 2.3 LCD

当前 BSP / driver：

| 信号 | GPIO / 来源 |
| --- | --- |
| SCLK | GPIO41 |
| MOSI | GPIO40 |
| DC / RS | GPIO39 |
| Backlight | GPIO42 |
| CS | PCA9557 IO0 |
| RESET | 共用系统 RESET 网络，软件配置 `-1` |
| SPI Host | SPI2_HOST |
| Logical resolution | 480 × 320 |
| SPI pixel clock | 40 MHz |

LCD 初始化方向：

```text
swap_xy = true
mirror_x = true
mirror_y = true
invert_color = true
```

当前组件目录和代码使用 `lcd_st7789 / st7789_panel`。原始硬件笔记中同时出现“ST7789”和“ST7796U”描述，两者存在冲突；在 BOM / PCB 器件丝印没有最终确认前，技术文档以**当前驱动实现 ST7789**为软件基线，硬件变更必须重新核对初始化命令。

## 2.4 Touch FTx36

| 项目 | 当前值 |
| --- | --- |
| I2C Address | `0x38` |
| Bus | 公共 I2C0 |
| BSP clock | 10 kHz |
| Raw width | 320 |
| Raw height | 480 |
| `swap_xy` | true |
| `mirror_x` | false |
| `mirror_y` | true |
| 工作模式 | Normal |
| G_MODE | Polling |
| 最大解析触点 | 2 |

驱动启动时读取：

- `0xA3`：Chip ID；
- `0xA8`：Vendor ID。

当前实现只接受已知兼容 ID，避免对未知触摸控制器盲写寄存器。

## 2.5 DVP Camera

当前 `board_camera_default_config()`：

| 信号 | GPIO |
| --- | ---: |
| XCLK | 5 |
| PCLK | 7 |
| VSYNC | 3 |
| HREF | 46 |
| D0 | 16 |
| D1 | 18 |
| D2 | 8 |
| D3 | 17 |
| D4 | 15 |
| D5 | 6 |
| D6 | 4 |
| D7 | 9 |
| PWDN | PCA9557 IO2 |
| RESET | 共用系统 RESET，driver 使用 `-1` |
| SCCB | 复用 I2C0，driver pin 配置 `-1` |

Camera 默认参数：

```text
XCLK         20 MHz
Pixel format RGB565
Frame size   QVGA 320×240
FB count     1
FB location  PSRAM
Grab mode    CAMERA_GRAB_WHEN_EMPTY
```

工程当前注释将传感器路径标记为 GC0308。

---

# 3. 音频硬件

音频部分来自现有板级资料，当前不是人脸考勤主链路的必需依赖。

## 3.1 I2S 信号

| 信号 | GPIO | 状态 |
| --- | ---: | --- |
| MCLK | GPIO38 | 已记录 |
| BCLK | GPIO14 | 已记录 |
| WS / LRCK | GPIO13 | 已记录 |
| DOUT | GPIO45 | 已记录，主控输出 |
| DIN | 待核对 | 原始资料未给出明确 GPIO |

## 3.2 ES7210

定位：4 通道 ADC，用于音频采集。

原始板级资料记录：

- I2C 控制；
- SCL 最大 400 kHz；
- PCM / I2S 数据路径；
- 采样频率范围记为 8–100 kHz；
- 24-bit。

这些参数应在启用音频功能前以实际芯片数据手册和 PCB 连接再次核对。

## 3.3 ES8311

定位：音频 Codec / 输出控制。

原始资料记录：

- I2C 控制；
- PCM / I2S；
- 24-bit；
- DAC volume register `0x32`；
- DAC mute control 位于 `0x31`。

当前 FaceAttend 考勤核心任务没有依赖 ES8311。

---

# 4. PCA9557 设计约束

PCA9557 为 8-bit I/O expander，通过 I2C 控制。

当前板级使用中最关键的两个信号是：

```text
IO0 -> LCD_CS
IO2 -> CAMERA_PWDN
```

驱动设计注意事项：

1. 修改单个 bit 时应维护输出 latch，不应破坏其他 IO 状态。
2. Camera PWDN 上电流程需要先设置锁存值，再配置输出。
3. LCD CS 由扩展器控制，因此 LCD SPI driver 中 `cs_gpio_num=-1`。
4. PCA9557 bus 生命周期由 BSP 持有，单个上层组件不应销毁公共 I2C bus。

---

# 5. LCD 并发约束

LVGL flush 与 camera preview 共用 LCD。

并发模型：

```text
lvgl_gui -----\
               +--> app_ctx.lcd_mutex --> lcd_display
cam_preview ---/
```

`lcd_display` 组件内部还有自己的锁，但 `app_ctx.lcd_mutex` 是应用层跨业务路径的串行化约束。

任何新增直接绘屏代码必须遵守相同锁顺序，避免：

- SPI transaction 交叉；
- framebuffer 正在发送时重用；
- LVGL / preview 互相覆盖造成异常。

---

# 6. Camera 与 Wi-Fi 内存约束

这是当前板级最重要的资源限制之一。

Camera framebuffer 位于 PSRAM，但 camera DMA 和 Wi-Fi driver 仍需要内部 DMA-capable RAM。工程当前使用：

```text
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=6
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=16
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=16
CONFIG_ESP_WIFI_RX_BA_WIN=6
CONFIG_CAMERA_CORE1=y
```

Wi-Fi 初始化前后的诊断重点是：

```text
internal_free
internal_largest
dma_free
dma_largest
```

总 free heap 充足并不代表 Wi-Fi 一定能初始化；`largest` 过小意味着内部堆碎片仍可能导致连续 buffer 分配失败。

---

# 7. Camera / Wi-Fi 启动顺序

当前稳定策略：

```text
Camera capture / preview
        ↓
Cloud task delay
        ↓
Wi-Fi driver init
        ↓
SNTP / MQTT
        ↓
Face model engine
```

实际工程还通过 Face engine 延迟启动，让 Wi-Fi 优先取得必要的内部 RAM。

不要在没有内存测量数据的情况下把人脸模型、Camera 和默认大 Wi-Fi buffer 同时提前拉起。

---

# 8. SoftAP 配网硬件相关行为

默认热点：

```text
SSID prefix : FaceAttend
Password    : 88888888
```

配网页面只有在 AP netif ready 且 AP IP 有效后才启动 HTTP server。

如果出现：

```text
httpd_server_init: error in listen (112)
```

应优先检查 AP netif 启动时序，而不是直接判断为端口冲突。

---

# 9. 电源与复位约束

已确认的软件假设：

- LCD reset 接系统 RESET 网络，应用层不独立控制；
- Camera reset 同样不由普通 GPIO 软件驱动；
- Camera PWDN 由 PCA9557 IO2；
- Touch 驱动不会再次拉低共享 RESET。

因此新增驱动时，不应自行“猜测”一个 Reset GPIO。

---

# 10. RTC 与离线时间边界

当前硬件未记录带电池保持的 RTC。

结果：

```text
设备已完成 SNTP
+ 不掉电
→ 断网后仍可按课表考勤

设备完全断电
+ 冷启动无网络
→ 无法可靠知道真实日期/星期/周次
→ 不允许自动课表签到
```

如果未来要求“断电后完全离线仍按课表工作”，最有价值的硬件升级是增加带后备电源的 RTC，而不是单纯增加 RAM。

---

# 11. 待核对项

以下内容在当前资料中仍不完整，硬件文档不做推断：

| 项目 | 状态 |
| --- | --- |
| Audio DIN GPIO | 待核对 |
| LCD 实际面板控制器丝印 | 软件为 ST7789；硬件笔记曾出现 ST7796U，需 BOM/实物确认 |
| 外部扩展 IO3–IO7 具体用途 | 待核对 |
| 音频功放型号和完整电源时序 | 待核对 |
| Camera 具体模组料号 / 地址 | 工程路径按 GC0308 使用，最终 BOM 建议确认 |

---

# 12. 硬件修改流程

修改 GPIO、总线或面板型号时，应同步更新：

1. `app/bsp/src/app_ctx.c`
2. 对应 `components/*` board config
3. `docs/hardware.md`
4. 必要的 `sdkconfig`
5. 硬件版本 / PCB revision 记录

禁止只改一处宏定义而不更新硬件文档，否则后续维护极易产生“文档 GPIO 与实际固件不一致”。


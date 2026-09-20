# ESP32-S3 / GC0308 摄像头组件驱动

本目录是独立参考代码，没有加入现有工程，也没有修改工程 CMake、组件清单、分区表或 sdkconfig。使用乐鑫官方 `espressif/esp32-camera`，不自行实现 DVP 接收或 GC0308 寄存器初始化。

对象是此前笔记中的 GC0308；AXK724147G 是连接器型号，不是摄像头芯片型号。如果实际更换了传感器，必须重新核对模块，本驱动会校验 GC0308 的 PID 和地址。

## 1. 分层与文件

| 层级 | 文件 | 职责 |
| --- | --- | --- |
| 示例层 | `demo_camera.c/.h` | 借用已有句柄，采集并记录十帧信息，归还帧和关闭设备 |
| 设备层 | `camera_device.c/.h` | 参数检查、单实例管理、采集、镜像、彩条、保存裸帧、清理 |
| 板级层 | `board_camera.c/.h` | J6 引脚、默认参数、PCA9557 IO2 休眠控制 |
| 共用基础层 | `../es8311_native/i2c_bus.c/.h` | 已有 Legacy I2C 总线管理，新增只读 `i2c_bus_get_config()` |
| 共用扩展 IO 层 | `../es8311_native/pca9557_io.c/.h` | 共用 PCA9557 句柄，通过读改写保护其他引脚 |
| 官方组件层 | `esp32-camera` | SCCB 配置传感器、DVP 接收、DMA 与帧缓冲管理 |

摄像头不创建第二个 PCA9557 对象，也不再次初始化 I2C。`bus` 与 `expander` 必须来自同一块板子的共享总线，且有效期覆盖摄像头使用过程。LCD_CS 使用 IO0、PA_EN 使用 IO1，摄像头只操作 IO2。

## 2. 板级连接与默认参数

| 摄像头信号 | ESP32-S3 / 扩展 IO |
| --- | --- |
| D0 / D1 / D2 / D3 | GPIO16 / GPIO18 / GPIO8 / GPIO17 |
| D4 / D5 / D6 / D7 | GPIO15 / GPIO6 / GPIO4 / GPIO9 |
| XCLK / PCLK | GPIO5 / GPIO7 |
| VSYNC / HREF | GPIO3 / GPIO46 |
| SCCB SDA / SCL | GPIO1 / GPIO2，复用已有 100 kHz I2C |
| PWDN | PCA9557 地址 0x19 的 IO2，高电平休眠 |
| RESET | 系统 RESET 网络，不是独立可控 GPIO |

SCCB 用于读写摄像头寄存器；图像数据通过 D0～D7 和同步信号传入，不通过 I2C 传输。ESP32-S3 的组件实现使用 LCD_CAM/GDMA，不能套用经典 ESP32 的 I2S 摄像头硬件描述。

默认 RGB565、QVGA（320×240）、20 MHz XCLK、一个 PSRAM 帧缓冲。一帧像素占 153600 字节，另有 DMA 和管理开销。封装支持 QQVGA（160×120）、QVGA 和 VGA（640×480），不支持硬件 JPEG；GC0308 最高 VGA。XCLK 可配置范围为 10～24 MHz，这是本封装的输入限制，不承诺整个范围的模块稳定性或固定帧率。

当前工程未启用 PSRAM，本驱动会返回错误并提示 `enable PSRAM first`。N16R8 模组应按实际硬件启用 Octal PSRAM，并通过启动日志确认容量；本次未替你修改配置。官方也提示非 JPEG 图像对内存带宽要求较高，因此先使用 QVGA 和单缓冲验证。[官方组件说明](https://github.com/espressif/esp32-camera)

## 3. 以后手动接入工程时的准备

以下只是接入说明，本次没有执行安装或修改工程。

1. 使用 ESP-IDF 5.5.5，目标设置为 ESP32-S3。
2. 添加官方摄像头组件。为匹配本次核对的接口，可在未来的组件清单中固定到下列 Git 提交；它不是 Registry 版本号。
3. 将共用 I2C/PCA9557 编译为唯一一份组件，不要在摄像头目录重复复制并编译。
4. 在 Camera configuration 中启用 GC0308，并选择 Legacy I2C driver；对应配置为 `CONFIG_GC0308_SUPPORT=y` 和 `CONFIG_SCCB_HARDWARE_I2C_DRIVER_LEGACY=y`。
5. 启用实际模组对应的 PSRAM。先单独采集，再接 LCD、音频或网络业务。

未来 `idf_component.yml` 示例：

```yaml
dependencies:
  idf: ">=5.5.5,<5.6"
  espressif/esp32-camera:
    git: https://github.com/espressif/esp32-camera.git
    version: 202df95d7b1dc72e9303ad78f47b8dc9f339e6a1
```

未来独立摄像头组件的 CMake 注册方式如下；`board_io` 是示意名称，必须换成你实际导出 `i2c_bus.h` 和 `pca9557_io.h` 的共用组件名：

```cmake
idf_component_register(
    SRCS "board_camera.c" "camera_device.c" "demo_camera.c"
    INCLUDE_DIRS "."
    REQUIRES esp32-camera board_io
    PRIV_REQUIRES esp_driver_ledc freertos heap
)
```

本框架已有总线基于 `driver/i2c.h`。不能让摄像头选择新的 I2C 驱动再借用 Legacy 端口。当前代码用编译检查阻止这种混搭。摄像头配置的 `pin_sccb_sda=-1`、`pin_sccb_scl=-1` 和 `sccb_i2c_port=已有端口` 使组件复用总线，而非重新安装驱动。关闭摄像头也不删除借用的总线。[本次核对的 SCCB 源码](https://github.com/espressif/esp32-camera/blob/202df95d7b1dc72e9303ad78f47b8dc9f339e6a1/driver/sccb.c)

## 4. 调用顺序

板级公共初始化（已有 I2C、已有 PCA9557）→ 默认配置 → 打开摄像头 → 可选镜像/彩条 → 获取帧 → 使用帧 → 归还帧 → 循环采集或关闭。

最小示例只需把已经创建的两个句柄传入：

```c
#include "demo_camera.h"

// shared_bus 和 shared_expander 是已有句柄，不在这里重复初始化。
ESP_ERROR_CHECK(demo_camera_capture(shared_bus, shared_expander));
```

此示例只输出十帧的宽、高、格式和长度，不自动显示到 LCD，也不自动挂载存储设备。需要定制处理时，参考 `demo_camera.c` 中带清理路径的完整实现。

## 5. 封装接口与参数

除默认配置函数返回 void，其余均返回 `esp_err_t`，成功为 `ESP_OK`。

| 函数 | 参数说明 | 作用与约束 |
| --- | --- | --- |
| `camera_device_default_config(config)` | `config`：待填写的配置结构体指针 | 写入 RGB565/QVGA/20 MHz/高电平休眠默认值 |
| `camera_device_open(dev, bus, expander, config)` | `dev`：首次必须清零的设备对象；`bus`：已初始化共用 I2C 句柄；`expander`：已有 PCA9557 句柄；`config`：输入配置 | 校验总线和 PSRAM、唤醒、初始化组件、核对传感器；失败也应进入 close 清理路径 |
| `camera_device_get_frame(dev, fb)` | `fb`：`camera_fb_t **` 输出参数 | 等待并借用一帧；一次最多借出一帧；失败输出 NULL |
| `camera_device_return_frame(dev, fb)` | `fb`：此前借出的指针变量地址 | 归还给组件并把调用者指针置 NULL；不能用 free 替代 |
| `camera_device_set_orientation(dev, hmirror, vflip)` | 两个 bool：水平镜像、垂直翻转 | 必须没有借出帧；不是 90° 旋转，不交换宽高 |
| `camera_device_set_colorbar(dev, enabled)` | `enabled`：是否启用传感器测试彩条 | 排查采集与颜色链路；正式拍摄前关闭 |
| `camera_device_get_id(dev, id, address)` | `id`：传感器 ID 输出；`address`：7 位 SCCB 地址输出，均不可 NULL | 返回识别结果，GC0308 预期 PID 0x9B、地址 0x21 |
| `camera_device_capture_to_file(dev, file, bytes)` | `file`：已打开的可写 FILE；`bytes`：实际写入字节数输出 | 获取一帧、写入并刷新文件、归还；不负责挂载、打开、关闭文件 |
| `camera_device_close(dev)` | `dev`：设备对象 | 释放组件、置休眠；保留共享 I2C/PCA9557；借出帧未归还时拒绝关闭 |
| `i2c_bus_get_config(bus, config)` | `bus`：有效共用句柄；`config`：配置副本输出 | 本次新增的只读查询，不初始化总线、不发送 I2C 事务 |

配置中的 `pixel_format` 本版只接受 `PIXFORMAT_RGB565`；`frame_size` 是分辨率枚举，不是像素字节数；`xclk_hz` 是外部输入时钟 Hz，不是 SCCB 速率，也不是帧率。

## 6. 实际调用的官方 API

| API | 参数及返回值 | 本驱动中的用途 |
| --- | --- | --- |
| `esp_camera_init(&config)` | 输入 `camera_config_t *`；返回 `esp_err_t` | 初始化采集、探测和配置传感器、准备缓冲 |
| `esp_camera_sensor_get()` | 无参数；返回 `sensor_t *`，未初始化可能 NULL | 读取 ID，访问传感器配置函数指针 |
| `sensor->set_pixformat(sensor, PIXFORMAT_RGB565)` | 传感器对象、像素格式；int，0 成功 | 再次检查 RGB565 寄存器设置的返回结果 |
| `sensor->set_hmirror(sensor, enabled)` | 对象、0/1；int，0 成功 | 水平镜像 |
| `sensor->set_vflip(sensor, enabled)` | 对象、0/1；int，0 成功 | 垂直翻转 |
| `sensor->set_colorbar(sensor, enabled)` | 对象、0/1；int，0 成功 | 测试彩条 |
| `esp_camera_fb_get()` | 无参数；返回 `camera_fb_t *` 或 NULL | 等待一帧，本次核对的组件内部等待上限约 4 秒 |
| `esp_camera_fb_return(fb)` | 借出的帧指针；返回 void | 将缓冲交回组件复用 |
| `esp_camera_deinit()` | 无参数；返回 `esp_err_t` | 释放摄像头内部资源 |

`camera_fb_t` 的 `buf` 是像素地址，`len` 是字节数，`width/height` 是像素尺寸，`format` 是实际像素格式。不要把传感器函数返回的 int 直接当作统一的 ESP-IDF 错误码。[官方 API 头文件](https://github.com/espressif/esp32-camera/blob/202df95d7b1dc72e9303ad78f47b8dc9f339e6a1/driver/include/esp_camera.h)、[传感器接口](https://github.com/espressif/esp32-camera/blob/202df95d7b1dc72e9303ad78f47b8dc9f339e6a1/driver/include/sensor.h)

## 7. 帧生命周期、颜色和错误处理

- 所有摄像头调用由同一管理任务串行执行。单实例检查不是跨任务互斥锁，不支持多个任务同时 open/get/close。
- 不得绕过此封装再直接调用 `esp_camera_*`，也不要从其他模块直接修改 GC0308 寄存器。
- `return_frame` 后原像素内存随时可能被覆盖，不能继续读取。LCD 异步 DMA 完成前不能归还，或先复制到独立缓冲。
- 裸 RGB565 保存结果不是 JPEG/BMP。文件不会携带宽高、格式或字节序元数据，需要调用者另外记录；不能只改扩展名充当图片。
- RGB/BGR 通道排列与 RGB565 两字节顺序是两件事。先用测试彩条确认采集输出，再匹配 LCD 接口，不能默认需要交换，也不能重复交换。
- 镜像和彩条设置后丢弃两帧作为过渡处理，并不承诺传感器 AE/AWB 已稳定。如果寄存器写入失败，设备进入 fault，需 close 后重新打开；等待过渡帧超时也不代表配置已经回滚。
- close 如果休眠控制失败，会保留上下文供重试，不能立刻销毁设备对象。示例清理失败采用 `ESP_ERROR_CHECK` 终止策略，防止栈对象消失后仍保留所有权。
- 错误使用 `ESP_RETURN_ON_ERROR` / `ESP_GOTO_ON_ERROR` 传播与清理；正常分支及释放资源的 if 保留，不能用会中止程序的检查宏代替所有控制流程。

## 8. 验证范围

已使用本机 ESP-IDF 5.5.5 的 ESP32-S3 GCC，对 `board_camera.c`、`camera_device.c`、`demo_camera.c` 和新增查询后的 `i2c_bus.c` 进行 `-fsyntax-only -Wall -Wextra -Werror` 检查，通过。检查使用上文固定提交的官方摄像头头文件，并在检查命令中开启 GC0308/Legacy 配置宏；这没有改变工程 sdkconfig。

未执行完整组件链接、烧录或实机采集，尚不能宣称已经上板验证。接入后的首轮检查应包括 PSRAM 容量、PID/地址、320×240/153600 字节帧、彩条颜色、镜像方向，以及关闭后 LCD_CS 和 PA_EN 仍保持原状态。

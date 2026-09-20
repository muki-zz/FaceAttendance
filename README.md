# FaceAttend：校园 AI 人脸签到考勤系统

> 文档状态：Release Candidate  
> 适用工程：ESP32-S3 FaceAttend  
> 软件基线：ESP-IDF 5.5.x、FreeRTOS、ESP-WHO / ESP-DL、LVGL、ESP-MQTT  
> 文档维护原则：以当前工程接口、持久化格式和运行行为为准；实验性或预留功能必须显式标记，不与已实现功能混写。

## 1. 项目概述

FaceAttend 是面向校园课堂场景的离线优先人脸考勤终端。系统以 ESP32-S3 为主控，在设备本地完成人脸录入、检测、识别、课表匹配、签到判定和考勤记录持久化；网络可用时通过 MQTT 将本地记录同步到巴法云，并可由配套微信小程序进行远程查看、统计和考勤表导出。

系统设计目标是：

- **离线可用**：人脸识别和本地记录不依赖云端推理。
- **按课表签到**：正式签到绑定到当天课程 `session_id`，而不是仅依赖一个全局时间窗口。
- **资源受控**：不新增课表调度任务；学期规则持久化，RAM 中仅按需生成当天最多 10 个课次。
- **网络解耦**：摄像头、人脸推理、GUI、存储和云同步分别运行，网络异常不阻塞识别主流程。
- **可靠补传**：考勤记录先落本地 JSONL，MQTT QoS 1 收到 PUBACK 后才推进上传游标。
- **可维护**：驱动、BSP、业务、GUI、云服务和课表模块职责分离。

原始文档已经明确了“本地人脸库、实时比对、离线考勤、联网同步和 FreeRTOS 解耦”的系统定位，但同时夹杂了环境传感器、智能农业目录、固定签到窗口等早期内容；本版文档以当前考勤工程为唯一基线。  

## 2. 已实现功能

### 2.1 终端功能

| 功能 | 状态 | 说明 |
| --- | --- | --- |
| 本地人脸录入 | 已实现 | 录入学号、姓名、班级并写入本地人脸库及学生映射 |
| 本地人脸识别 | 已实现 | ESP-WHO / ESP-DL 本地检测与识别 |
| 课程 Session 签到 | 已实现 | 根据当前日期、周次、课表、班级和时间完成签到判定 |
| 重复签到抑制 | 已实现 | 去重键为 `student_id + session_id` |
| 正常 / 迟到判定 | 已实现 | 使用当前 Session 的 `late_minute` |
| 当天签到记录查询 | 已实现 | LVGL 表格分页，只展示当天记录 |
| 当天考勤统计 | 已实现 | 统计学生数、签到数、正常、迟到、缺勤、历史未同步记录 |
| 当天课表查询 | 已实现 | `Today Schedule` 页面，只读展示当天课次 |
| SPIFFS 持久化 | 已实现 | 学生、签到、课表、配置文件 |
| Wi-Fi SoftAP 配网 | 已实现 | `FaceAttend-xxxxxx` 热点 + HTTP 配网页面 |
| SNTP 校时 | 已实现 | 多服务器非阻塞轮询 |
| 巴法云 MQTT | 已实现 | 考勤上报、课表下发 |
| 离线记录补传 | 已实现 | NVS `upload_id` + QoS 1 PUBACK |
| 教师微信小程序 | 已实现配套工程 | 实时看板、历史记录、统计、XLSX 导出 |

### 2.2 课表能力

课表采用“**学期模板 + 临时例外 + 当天缓存**”模型：

```text
semester_schedule.json
        │
        │  周次 / 星期 / 节次规则
        ▼
  timetable.c
        │
        ├── schedule_overrides.json
        │      cancel / replace / add / restore
        │
        ▼
  today sessions[<=10]
        │
        ▼
  Face attendance
```

正常情况下，一个学期只需下发一次完整课表；停课、调课和补课通过小型 `schedule_override` 消息更新。

## 3. 系统架构

### 3.1 软件分层

```text
┌─────────────────────────────────────────────────────────┐
│                    Application Layer                    │
│  Face | Storage | Timetable | GUI | Cloud | Wi-Fi Prov │
├─────────────────────────────────────────────────────────┤
│                       Middleware                        │
│    FreeRTOS | LVGL | SPIFFS | NVS | cJSON | ESP-MQTT  │
├─────────────────────────────────────────────────────────┤
│                    Driver / Component                   │
│ I2C | PCA9557 | ST7789 | FTx36 | Camera | esp32-camera │
├─────────────────────────────────────────────────────────┤
│                       Hardware                          │
│              ESP32-S3 + PSRAM + peripherals             │
└─────────────────────────────────────────────────────────┘
```

### 3.2 业务数据流

```text
Camera
  │
  ▼
cam_capture ──► face_engine ──► Student mapping
                                 │
                                 ▼
                         Timetable session
                                 │
                    ┌────────────┴────────────┐
                    ▼                         ▼
               reject / duplicate        save check-in
                                              │
                                              ▼
                                         SPIFFS JSONL
                                              │
                                      network available
                                              │
                                              ▼
                                         cloud_sync
                                              │
                                              ▼
                                     BaFa MQTT / Mini App
```

## 4. 主要 FreeRTOS 任务

| 任务 | 典型优先级 | 职责 |
| --- | ---: | --- |
| `face_engine` | 6 | 人脸检测、识别、注册和课表签到判定 |
| `cam_capture` | 5 | 摄像头取帧并向推理 / 预览路径投递 |
| `lvgl_gui` | 4 | LVGL 渲染、触摸、页面刷新 |
| `cam_preview` | 3 | 摄像头预览显示 |
| `cloud_sync` | 2 | Wi-Fi 状态、SNTP、MQTT、离线记录补传 |
| ESP-MQTT internal | 组件内部 | MQTT 网络收发与重连 |

课表模块**不创建独立 FreeRTOS Task**。当天课表仅在 GUI 查询或人脸签到判定时按需生成。

为降低摄像头和 Wi-Fi 的竞争，工程将 esp32-camera 内部 `cam_task` 固定到 core 1，并采用低内存 Wi-Fi buffer 配置。

## 5. 启动流程

```text
app_main()
   │
   ├─ storage_init()
   │    ├─ NVS
   │    ├─ SPIFFS
   │    ├─ attendance config
   │    └─ timetable_init()
   │
   ├─ app_ctx_create()
   │    ├─ I2C / PCA9557
   │    ├─ LCD / Touch
   │    └─ LCD mutex
   │
   ├─ cloud_service_start()
   │    └─ 创建 cloud_sync；网络延后启动
   │
   ├─ face_service_start()
   │    ├─ camera capture / preview
   │    └─ face engine 延迟启动
   │
   └─ LVGL GUI
```

网络启动被有意延迟，以优先完成摄像头初始化并减少内部 DMA RAM 碎片。

## 6. 签到业务规则

### 6.1 正式签到条件

人脸识别成功后依次执行：

1. 系统墙钟时间必须有效。
2. 当前日期必须能够生成当天课表。
3. 当前时间必须落入某个 Session 的 `open_minute ~ end_minute`。
4. Session `class_id=0`，或学生 `class_id` 与 Session 一致。
5. 当前学生在该 `session_id` 下尚无签到记录。
6. `current_minute < late_minute` 记为 `normal`，否则记为 `late`。
7. 写入 `/spiffs/checkin.jsonl`。

### 6.2 去重

正式去重键：

```text
student_id + session_id
```

因此同一学生可以在同一天不同课程分别签到，但同一节课重复刷脸只保存一次。

### 6.3 时间不可用

课表签到依赖真实日期、星期和周次，因此冷启动后若系统时间无效：

- Wi-Fi 已连接且 SNTP 正在同步：返回 `TIME_SYNCING`；
- 超出同步等待窗口仍无有效时间：返回 `TIME_REQUIRED`；
- **不伪造课程归属，不写正式课次签到**。

没有外接电池 RTC 时，“完全断电 + 冷启动 + 无网络”无法自动恢复真实墙钟，这是硬件边界而不是软件缺陷。

## 7. 课表模型

### 7.1 持久化文件

| 文件 | 作用 |
| --- | --- |
| `/spiffs/semester_schedule.json` | 学期周期课表 |
| `/spiffs/schedule_overrides.json` | 临时停课 / 调课 / 补课 |
| `/spiffs/checkin.jsonl` | 全量本地签到流水 |
| `/spiffs/students.jsonl` | 学生和 Face ID 映射 |
| `/spiffs/attendance_config.json` | 时区、NTP、MQTT、冷却时间等 |

### 7.2 资源限制

| 项目 | 上限 |
| --- | ---: |
| 学期规则 | 48 条 |
| 临时例外 | 16 条 |
| 当天 Session | 10 条 |
| 单次 MQTT 课表消息 | 8192 Byte |

学期规则和课表接收缓冲优先使用 PSRAM；当天 Session 使用固定小型结构缓存。

### 7.3 学期课表示例

```json
{
  "type": "semester_schedule",
  "semester": "2026-2027-1",
  "semester_start": "2026-09-07",
  "week_count": 18,
  "version": 1,
  "entries": [
    {
      "weekday": 1,
      "period": 1,
      "course_id": 101,
      "course_name": "Data Structure",
      "class_id": 231,
      "open": "07:50",
      "start": "08:00",
      "late_after": "08:10",
      "end": "08:45"
    }
  ]
}
```

`weekday` 取值：1=Monday，…，7=Sunday。若不提供 `weeks` / `week_mask`，默认每周生效。

### 7.4 临时例外

支持：

- `cancel`：停课；
- `replace`：替换某日期某节课；
- `add`：增加补课；
- `restore`：移除该日期课次的例外，恢复学期模板；
- `schedule_override_reset`：清空全部临时例外。

学期课表和 override 都使用单调递增 `version`，重复或旧版本消息会被忽略，避免 MQTT 重复投递导致 SPIFFS 反复擦写。

## 8. GUI

### 8.1 页面

| 页面 | 功能 |
| --- | --- |
| Home | 摄像头预览、识别结果、实时日期时间、网络状态、4 个导航按钮 |
| Register Face | 学号、姓名、班级录入与人脸注册 |
| Check-in Records | **仅当天**签到记录，表格分页 |
| Attendance Stats | **仅当天**考勤统计 |
| Today Schedule | 当天课表查询，当前 Session 使用 `>` 标识 |

首页 4 个功能按钮统一尺寸和间距，避免导航区域视觉错位。

### 8.2 主题

当前 GUI 使用统一蓝色主题：

| 用途 | 色值 |
| --- | --- |
| Primary | `#769FCD` |
| Accent | `#B9D7EA` |
| Surface Alt | `#D6E6F2` |
| Background | `#F7FBFC` |
| Main text | `#445463` |

## 9. 本地数据

### 9.1 学生

```json
{
  "face_id": 1,
  "student_id": "20260001",
  "name": "Student A",
  "class_id": 231,
  "created_ts": 1789717000
}
```

### 9.2 签到

当前记录包含课程上下文：

```json
{
  "record_id": 17,
  "session_id": 357190214,
  "face_id": 3,
  "student_id": "20260001",
  "name": "Student A",
  "class_id": 231,
  "course_id": 101,
  "course_name": "Data Structure",
  "period": 1,
  "ts": 1789718400,
  "status": 0
}
```

`record_id` 用于云端上传顺序和幂等；`session_id` 用于课程级签到去重。

## 10. 云服务

### 10.1 Topic 约定

假设基础考勤 Topic 为：

```text
attendance004
```

则：

| 用途 | Topic |
| --- | --- |
| 考勤上报 | `attendance004/set` |
| 课表下发 | `attendance004schedule` |

课表 Topic 由 `<bemfa_topic> + "schedule"` 自动派生，以保持字母数字 Topic 约束。

### 10.2 MQTT 考勤消息

```json
{
  "type": "attendance",
  "record_id": 17,
  "session_id": 357190214,
  "course_id": 101,
  "course_name": "Data Structure",
  "period": 1,
  "student_id": "20260001",
  "name": "Student A",
  "class_id": 231,
  "face_id": 3,
  "timestamp": 1789718400,
  "time": "2026-09-18T08:00:00+0800",
  "time_synced": true,
  "status": "normal"
}
```

### 10.3 上传可靠性

QoS 1 模式下，应用层一次只允许一条 attendance 消息处于 in-flight：

```text
read upload_id
   ↓
read first record_id > upload_id
   ↓
publish QoS1
   ↓
wait MQTT_EVENT_PUBLISHED / PUBACK
   ↓
storage_set_upload_mark(record_id)
   ↓
next record
```

PUBACK 到达前不会由应用层重复 publish 同一记录。极端 ACK 丢失仍属于“至少一次”语义，因此教师端仍应按 `record_id` 幂等处理。

## 11. Wi-Fi 与时间同步

### 11.1 配网

默认：

- SoftAP 前缀：`FaceAttend`
- 密码：`88888888`
- 最大 STA 重试：5
- STA 成功后关闭 AP / HTTP portal

HTTP portal 等待 AP netif 和 IP 有效后再启动，避免 `listen(112 / EHOSTDOWN)` 启动竞态。

### 11.2 SNTP

联网获得 IP 后启动非阻塞 SNTP。系统使用 POSIX timezone，示例：

```json
"timezone": "CST-8"
```

校时完成后，即使网络断开，只要设备不断电，系统时间仍可用于当天课表和签到。

## 12. 教师微信小程序

配套小程序提供：

- 实时设备在线状态；
- MQTT / HTTP 考勤查看；
- 今日考勤概览；
- 记录分页与筛选；
- 统计页；
- XLSX 考勤表导出。

客户端应按 `record_id` 去重，不应把 retained、历史补传或重复 QoS 消息当作新的现场签到提醒。

## 13. 工程目录

```text
FaceAttend/
├── main/
├── app/
│   ├── bsp/
│   ├── cloud/
│   ├── face/
│   ├── gui/
│   └── storage/
├── components/
│   ├── camera/
│   ├── i2c_bus/
│   ├── lcd_st7789/
│   ├── pca9557/
│   ├── touch_ft6x36/
│   └── wifi_provisioning/
├── docs/
│   ├── api.md
│   └── hardware.md
├── tools/
├── partitions.csv
├── sdkconfig
├── sdkconfig.defaults
└── README.md
```

## 14. 构建与烧录

建议使用项目既定 ESP-IDF 环境：

```bash
idf.py set-target esp32s3
idf.py fullclean
idf.py build
idf.py flash monitor
```

修改 Wi-Fi buffer、PSRAM、camera core、组件 CMake 或分区表后，应执行 `fullclean`。

## 15. 关键 sdkconfig 策略

为兼顾 Camera + Wi-Fi：

```text
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=6
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=16
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=16
CONFIG_ESP_WIFI_RX_BA_WIN=6
CONFIG_CAMERA_CORE1=y
```

这些参数属于当前板级资源约束的一部分，不建议在没有 heap / DMA 测量数据的情况下随意放大 Wi-Fi buffer。

## 16. 已知边界

1. 无电池 RTC 时，冷启动且无网络无法恢复真实日期时间，因此不能自动确定课程。
2. 设备 GUI 仅面向“当天”业务；跨日期历史查询和导出由教师端小程序承担。
3. SPIFFS JSONL 适合当前规模，不等同于数据库；大量长期历史应依赖云端或定期归档。
4. `class_id=0` 的 Session 表示允许任意已注册班级，应仅用于公共课或调试。
5. 巴法云 UID 属于敏感凭证，不应提交到公共仓库。
6. 音频硬件在硬件文档中保留说明，但不属于当前考勤主链路的必需依赖。

## 17. 文档索引

- `README.md`：项目定位、架构、部署和运维入口。
- `docs/api.md`：模块接口、数据结构、MQTT 协议和线程约束。
- `docs/hardware.md`：板级接口、GPIO、总线和外设约束。


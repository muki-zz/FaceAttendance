# FaceAttend API 与协议规范

> 文档类型：Developer Reference  
> 适用对象：嵌入式开发、联调、GUI、微信小程序、维护人员  
> 约定：本文件只描述当前主线工程实际使用或明确保留的接口；旧的固定签到窗口语义不再作为正式课表签到规则。

## 1. 通用约定

### 1.1 返回值

C 接口统一使用 ESP-IDF `esp_err_t`：

| 返回值 | 含义 |
| --- | --- |
| `ESP_OK` | 成功 |
| `ESP_ERR_INVALID_ARG` | 参数非法 |
| `ESP_ERR_INVALID_STATE` | 当前状态不允许执行 |
| `ESP_ERR_NOT_FOUND` | 未找到目标 |
| `ESP_ERR_NO_MEM` | 内存不足 |
| `ESP_ERR_TIMEOUT` | 等待超时 |
| `ESP_FAIL` | 其他失败 |

返回数量的查询接口通常以 `int` 返回记录数，`0` 表示无数据或参数不满足条件；调用者应结合输入参数和日志判断。

### 1.2 时间

- Unix 时间使用 `int64_t` / `time_t`。
- `STORAGE_TIME_VALID_EPOCH = 1700000000` 作为墙钟有效性阈值。
- 课表时间内部统一使用“自 00:00 起分钟数”，范围 `0..1439`。
- 本地日期格式：`YYYY-MM-DD`。
- 时区由 `attendance_config.json` 的 POSIX TZ 字符串控制。

### 1.3 并发

- 文件系统访问由全局 `g_fs_mutex` 串行化。
- `timetable` 自身使用独立 mutex 保护学期规则、override 和当天缓存。
- Face engine 不等待 MQTT。
- Cloud task 不直接执行人脸推理。
- LVGL 页面只通过公开接口读取状态，不直接修改底层持久化结构。

---

# 2. BSP

## 2.1 `app_ctx.h`

```c
typedef struct {
    i2c_bus_handle_t i2c_bus;
    pca9557_handle_t io_expander;
    lcd_display_handle_t lcd;
    touch_ft6x36_handle_t touch_device;
    camera_device_t camera;
    SemaphoreHandle_t lcd_mutex;
} app_ctx_t;

esp_err_t app_ctx_create(app_ctx_t *ctx);
void app_ctx_delete(app_ctx_t *ctx);
```

### `app_ctx_create`

职责：

1. 初始化 I2C0；
2. 创建 PCA9557；
3. 创建 LCD；
4. 取得原生 I2C bus handle；
5. 创建 FTx36 touch；
6. 创建应用层 `lcd_mutex`。

`lcd_mutex` 用于串行化 LVGL flush 与 camera preview 对 LCD 的共享访问。

---

# 3. 驱动层 API

## 3.1 I2C Bus

```c
esp_err_t i2c_bus_get(const i2c_bus_config_t *config,
                      i2c_bus_handle_t *ret_bus);

esp_err_t i2c_bus_get_native(i2c_bus_handle_t bus,
                             i2c_master_bus_handle_t *native_bus);

esp_err_t i2c_bus_delete(i2c_bus_handle_t bus);
```

`i2c_bus_get_native()` 用于把公共 I2C master bus 交给触摸屏等上层组件复用。

## 3.2 PCA9557

```c
esp_err_t pca9557_create(i2c_bus_handle_t bus,
                         uint8_t address,
                         pca9557_handle_t *out);

esp_err_t pca9557_config_output(pca9557_handle_t device,
                                uint8_t pin,
                                bool initial_high);

esp_err_t pca9557_config_input(pca9557_handle_t device,
                               uint8_t pin);

esp_err_t pca9557_set_level(pca9557_handle_t device,
                            uint8_t pin,
                            bool high);

esp_err_t pca9557_read_port(pca9557_handle_t device,
                            uint8_t *value);

esp_err_t pca9557_set_lcd_cs(pca9557_handle_t expander,
                             bool selected);

void pca9557_delete(pca9557_handle_t device);
```

当前板级使用：

- IO0：LCD CS；
- IO2：Camera PWDN。

## 3.3 LCD

```c
esp_err_t lcd_display_create(const lcd_display_config_t *config,
                             lcd_display_handle_t *ret_display);

esp_err_t lcd_display_draw_bitmap(lcd_display_handle_t display,
                                  int x_start,
                                  int y_start,
                                  int x_end,
                                  int y_end,
                                  const void *color_data);

esp_err_t lcd_display_fill(lcd_display_handle_t display,
                           uint16_t rgb565);

esp_err_t lcd_display_set_direction(lcd_display_handle_t display,
                                    bool swap_xy,
                                    bool mirror_x,
                                    bool mirror_y);

esp_err_t lcd_display_get_size(lcd_display_handle_t display,
                               uint16_t *width,
                               uint16_t *height);

esp_err_t lcd_display_set_backlight(lcd_display_handle_t display,
                                    bool on);

esp_err_t lcd_display_delete(lcd_display_handle_t display);
```

显示 facade 内部负责 SPI / panel 操作；应用层仍需使用 `app_ctx_t.lcd_mutex` 避免 LVGL 与 preview 同时访问。

## 3.4 Touch FTx36

```c
esp_err_t touch_ft6x36_create(
    const touch_ft6x36_config_t *config,
    touch_ft6x36_handle_t *out);

esp_err_t touch_ft6x36_read(
    touch_ft6x36_handle_t touch,
    touch_ft6x36_sample_t *sample);

esp_err_t touch_ft6x36_set_direction(
    touch_ft6x36_handle_t touch,
    bool swap_xy,
    bool mirror_x,
    bool mirror_y);

esp_err_t touch_ft6x36_set_mirror(
    touch_ft6x36_handle_t touch,
    bool mirror_x,
    bool mirror_y);

esp_err_t touch_ft6x36_delete(
    touch_ft6x36_handle_t touch);
```

当前实现使用 7-bit 地址 `0x38`，最多解析 2 个触点。

## 3.5 Camera

```c
void camera_device_default_config(camera_device_config_t *config);

esp_err_t camera_device_open(camera_device_t *dev,
                             i2c_bus_handle_t bus,
                             pca9557_handle_t expander,
                             const camera_device_config_t *config);

esp_err_t camera_device_get_frame(camera_device_t *dev,
                                  camera_fb_t **fb);

esp_err_t camera_device_return_frame(camera_device_t *dev,
                                     camera_fb_t **fb);

esp_err_t camera_device_set_orientation(camera_device_t *dev,
                                        bool hmirror,
                                        bool vflip);

esp_err_t camera_device_set_colorbar(camera_device_t *dev,
                                     bool enabled);

esp_err_t camera_device_get_id(camera_device_t *dev,
                               sensor_id_t *id,
                               uint8_t *address);

esp_err_t camera_device_capture_to_file(camera_device_t *dev,
                                        FILE *file,
                                        size_t *bytes);

esp_err_t camera_device_close(camera_device_t *dev);
```

板级默认 Camera 配置：

- QVGA `320×240`；
- `RGB565`；
- 1 个 framebuffer；
- framebuffer 位于 PSRAM；
- `CAMERA_GRAB_WHEN_EMPTY`；
- XCLK 20 MHz。

---

# 4. Storage API

## 4.1 常量

```c
#define STORAGE_STUDENT_ID_LEN      24
#define STORAGE_STUDENT_NAME_LEN    32
#define STORAGE_COURSE_NAME_LEN     32
#define STORAGE_MAX_STUDENTS        128

#define FACE_DB_PATH       "/spiffs/face_db.bin"
#define STUDENT_DB_PATH    "/spiffs/students.jsonl"
#define CHECKIN_LOG_PATH   "/spiffs/checkin.jsonl"
#define ATTENDANCE_CONFIG_PATH "/spiffs/attendance_config.json"
```

NVS：

```c
#define STORAGE_NVS_KEY_NEXT_RECORD "next_rec"
#define STORAGE_NVS_KEY_UPLOAD_MARK "upload_id"
```

## 4.2 签到状态

```c
typedef enum {
    CHECKIN_STATUS_NORMAL = 0,
    CHECKIN_STATUS_LATE = 1,
    CHECKIN_STATUS_ABSENT = 2,
    CHECKIN_STATUS_TIME_UNSYNCED = 3,
} checkin_status_t;
```

说明：

- `NORMAL`：课次签到正常；
- `LATE`：当前分钟大于等于 Session `late_minute`；
- `ABSENT`：兼容显式缺勤记录；当天统计还会对已结束 Session 动态推导缺勤；
- `TIME_UNSYNCED`：兼容历史旧记录。当前正式课表签到在无有效时间时会直接拒绝，而不是创建新的无课次正式签到。

## 4.3 配置结构

```c
typedef struct {
    uint16_t start_minute;
    uint16_t late_after_minute;
    uint16_t end_minute;
    uint16_t cooldown_sec;

    char timezone[STORAGE_TIMEZONE_LEN];
    char ntp_server[STORAGE_NTP_SERVER_LEN];

    bool cloud_enabled;
    char mqtt_broker_uri[STORAGE_MQTT_URI_LEN];
    char bemfa_uid[STORAGE_BEMFA_UID_LEN];
    char bemfa_topic[STORAGE_BEMFA_TOPIC_LEN];
    uint8_t mqtt_qos;
    bool mqtt_retain;
    uint8_t sync_batch_size;
} attendance_config_t;
```

`start_minute / late_after_minute / end_minute` 为旧配置兼容字段；当前课表模式正式签到使用 Session 自身的 `open/start/late/end`。`cooldown_sec` 仍用于识别去抖。

## 4.4 学生模型

```c
typedef struct {
    int face_id;
    char student_id[STORAGE_STUDENT_ID_LEN];
    char name[STORAGE_STUDENT_NAME_LEN];
    int class_id;
    int64_t created_ts;
} student_profile_t;
```

## 4.5 签到模型

```c
typedef struct {
    uint32_t record_id;
    uint32_t session_id;
    int face_id;
    char student_id[STORAGE_STUDENT_ID_LEN];
    char name[STORAGE_STUDENT_NAME_LEN];
    int64_t timestamp;
    checkin_status_t status;
    int class_id;
    uint16_t course_id;
    uint8_t period;
    char course_name[STORAGE_COURSE_NAME_LEN];
} checkin_record_t;
```

### 字段语义

| 字段 | 说明 |
| --- | --- |
| `record_id` | 本地单调递增记录号；云同步主键 |
| `session_id` | 当天某一课程 Session 的稳定标识 |
| `face_id` | 本地人脸数据库 ID |
| `student_id` | 学号 |
| `class_id` | 学生所属班级 |
| `course_id` | 当前课程 ID |
| `period` | 节次 |
| `timestamp` | 正式签到 Unix 时间 |
| `status` | 正常 / 迟到等 |
| `course_name` | GUI 和云端显示用课程名 |

## 4.6 统计模型

```c
typedef struct {
    uint32_t student_count;
    uint32_t checkin_count;
    uint32_t normal_count;
    uint32_t late_count;
    uint32_t absent_count;
    uint32_t unsynced_count;
} attendance_stats_t;
```

## 4.7 初始化与配置

```c
esp_err_t storage_init(void);
esp_err_t storage_spiffs_init(void);
esp_err_t storage_attendance_config_load(attendance_config_t *out);
const char *storage_checkin_status_str(checkin_status_t status);
```

## 4.8 学生接口

```c
esp_err_t storage_student_add(const student_profile_t *student);

esp_err_t storage_student_find_by_face_id(
    int face_id,
    student_profile_t *out);

esp_err_t storage_student_find_by_student_id(
    const char *student_id,
    student_profile_t *out);

int storage_student_list(student_profile_t *out,
                         int max_count);
```

## 4.9 签到写入与查询

```c
esp_err_t storage_checkin_add(checkin_record_t *record);

esp_err_t storage_append_checkin(
    const checkin_record_t *record);

esp_err_t storage_get_last_checkin_for_student(
    const char *student_id,
    checkin_record_t *out);

esp_err_t storage_get_checkin_for_student_session(
    const char *student_id,
    uint32_t session_id,
    checkin_record_t *out);
```

正式课表去重使用：

```text
student_id + session_id
```

## 4.10 当天记录分页

```c
int storage_checkin_list_page_today(
    checkin_record_t *out,
    int max_count,
    uint32_t offset_from_newest,
    uint32_t *total_count_out);
```

语义：

- 只筛选当前本地日期；
- 最新记录优先；
- `offset_from_newest=0` 返回第一页；
- 不把全量 JSONL 一次性加载到 LVGL RAM。

## 4.11 当天统计

```c
esp_err_t storage_get_stats_today(
    attendance_stats_t *out);
```

缺勤计算规则：

1. 只处理当天课表；
2. 只对 `end_minute < current_minute` 的已结束 Session 结算；
3. Session `class_id=0` 时，所有注册学生都属于应到；
4. 否则仅匹配学生 `class_id`；
5. `expected - present` 叠加到 `absent_count`。

这是一种**查询时推导**，不新增结算 Task，也不要求为每个缺勤学生写 JSONL。

## 4.12 云同步

```c
int storage_read_unuploaded_records(
    checkin_record_t *out,
    int max_count,
    uint32_t last_uploaded_id);

esp_err_t storage_get_upload_mark(
    uint32_t *record_id_out);

esp_err_t storage_set_upload_mark(
    uint32_t record_id);
```

---

# 5. Timetable API

## 5.1 资源上限

```c
#define TIMETABLE_MAX_SESSIONS      10
#define TIMETABLE_MAX_RULES         48
#define TIMETABLE_MAX_OVERRIDES     16
#define TIMETABLE_MAX_IMPORT_BYTES  8192
```

持久化：

```c
#define TIMETABLE_SEMESTER_PATH   "/spiffs/semester_schedule.json"
#define TIMETABLE_OVERRIDES_PATH  "/spiffs/schedule_overrides.json"
```

## 5.2 Session

```c
typedef struct {
    uint32_t session_id;
    uint16_t course_id;
    int class_id;
    uint8_t period;
    uint16_t open_minute;
    uint16_t start_minute;
    uint16_t late_minute;
    uint16_t end_minute;
    char course_name[TIMETABLE_COURSE_NAME_LEN];
} timetable_entry_t;
```

`class_id=0` 表示允许任意已注册学生班级。

## 5.3 当天课表

```c
typedef struct {
    char date[TIMETABLE_DATE_LEN];
    uint32_t version;
    uint8_t semester_week;
    uint8_t count;
    timetable_entry_t entries[TIMETABLE_MAX_SESSIONS];
} timetable_day_t;
```

## 5.4 元信息

```c
typedef struct {
    char semester[TIMETABLE_SEMESTER_NAME_LEN];
    char semester_start[TIMETABLE_DATE_LEN];
    uint32_t version;
    uint32_t override_version;
    uint8_t week_count;
    uint8_t rule_count;
    uint8_t override_count;
} timetable_info_t;
```

## 5.5 公开接口

```c
esp_err_t timetable_init(void);

esp_err_t timetable_import_json(
    const char *json,
    size_t len);

esp_err_t timetable_get_day(
    timetable_day_t *out);

esp_err_t timetable_get_info(
    timetable_info_t *out);

bool timetable_matches_date(
    time_t now);

esp_err_t timetable_get_current_session(
    time_t now,
    timetable_entry_t *out);

esp_err_t timetable_format_local_date(
    time_t now,
    char out[TIMETABLE_DATE_LEN]);
```

### `timetable_init`

从 SPIFFS 加载学期规则和 override。课表状态优先使用 PSRAM。

### `timetable_import_json`

支持：

```text
semester_schedule
schedule_override
schedule_override_reset
```

同一学期完整课表按 `version` 去重；override 使用独立单调版本序列。

### `timetable_get_day`

按需生成当天 Session：

```text
current date
   ↓
semester week
   ↓
weekday + week_mask filter
   ↓
apply override
   ↓
sort by time
   ↓
cache <=10 sessions
```

没有周期性 scheduler task。

### `timetable_get_current_session`

匹配：

```text
open_minute <= now_minute <= end_minute
```

无当前课程返回 `ESP_ERR_NOT_FOUND`；墙钟无效返回 `ESP_ERR_INVALID_STATE`。

---

# 6. Face Service API

## 6.1 注册失败原因

```c
typedef enum {
    FACE_ENROLL_FAIL_NONE = 0,
    FACE_ENROLL_FAIL_DUPLICATE_STUDENT,
    FACE_ENROLL_FAIL_DUPLICATE_FACE,
    FACE_ENROLL_FAIL_NO_FACE,
    FACE_ENROLL_FAIL_MULTI_FACE,
    FACE_ENROLL_FAIL_MODEL,
    FACE_ENROLL_FAIL_STORAGE,
    FACE_ENROLL_FAIL_BUSY,
} face_enroll_fail_reason_t;
```

## 6.2 签到结果

```c
typedef enum {
    FACE_CHECKIN_RESULT_NONE = 0,
    FACE_CHECKIN_RESULT_SAVED,
    FACE_CHECKIN_RESULT_DUPLICATE,
    FACE_CHECKIN_RESULT_BEFORE_WINDOW,
    FACE_CHECKIN_RESULT_AFTER_WINDOW,
    FACE_CHECKIN_RESULT_TIME_UNSYNCED_SAVED,
    FACE_CHECKIN_RESULT_TIME_SYNCING,
    FACE_CHECKIN_RESULT_TIME_REQUIRED,
    FACE_CHECKIN_RESULT_NO_SCHEDULE,
    FACE_CHECKIN_RESULT_NO_ACTIVE_SESSION,
    FACE_CHECKIN_RESULT_WRONG_CLASS,
    FACE_CHECKIN_RESULT_STORAGE_ERROR,
} face_checkin_result_t;
```

其中 `BEFORE_WINDOW / AFTER_WINDOW / TIME_UNSYNCED_SAVED` 为兼容历史枚举；当前 Session 模式主要使用：

- `SAVED`
- `DUPLICATE`
- `TIME_SYNCING`
- `TIME_REQUIRED`
- `NO_SCHEDULE`
- `NO_ACTIVE_SESSION`
- `WRONG_CLASS`
- `STORAGE_ERROR`

## 6.3 Face Event

```c
typedef struct {
    face_event_type_t type;
    esp_err_t err;
    face_enroll_fail_reason_t enroll_fail_reason;

    int face_id;
    float similarity;

    bool checkin_saved;
    bool checkin_time_valid;
    checkin_status_t checkin_status;
    face_checkin_result_t checkin_result;
    int64_t checkin_timestamp;

    uint32_t session_id;
    uint16_t course_id;
    uint8_t period;
    char course_name[STORAGE_COURSE_NAME_LEN];

    char student_id[STORAGE_STUDENT_ID_LEN];
    char name[STORAGE_STUDENT_NAME_LEN];
    int class_id;
} face_event_t;
```

## 6.4 公开接口

```c
esp_err_t face_service_start(app_ctx_t *ctx);

esp_err_t face_service_request_enroll(
    const face_enroll_request_t *request);

QueueHandle_t face_service_get_event_queue(void);

void face_service_set_preview_enabled(bool enabled);
bool face_service_is_preview_enabled(void);
```

## 6.5 识别签到状态机

```text
recognized
   ↓
student mapping exists?
   ↓
valid wall clock?
   ↓
today timetable exists?
   ↓
active session?
   ↓
class matches?
   ↓
cooldown?
   ↓
student_id + session_id already exists?
   ↓
NORMAL / LATE
   ↓
storage_checkin_add()
```

---

# 7. Cloud Service API

## 7.1 状态

```c
typedef struct {
    bool wifi_connected;
    bool mqtt_connected;
    bool time_synced;
    bool cloud_enabled;
    uint32_t last_uploaded_id;
    uint32_t pending_batch_count;
    esp_err_t last_error;
} cloud_service_status_t;
```

## 7.2 公开接口

```c
esp_err_t cloud_service_start(void);

esp_err_t cloud_service_get_status(
    cloud_service_status_t *out);
```

`cloud_service_start()` 创建独立低优先级任务，Wi-Fi 不在调用点同步启动。

---

# 8. Wi-Fi Provisioning API

```c
typedef enum {
    WIFI_PROV_STATE_STOPPED,
    WIFI_PROV_STATE_CONNECTING,
    WIFI_PROV_STATE_PROVISIONING,
    WIFI_PROV_STATE_CONNECTED,
} wifi_prov_state_t;
```

```c
typedef struct {
    const char *ap_ssid_prefix;
    const char *ap_password;
    uint8_t sta_max_retry;
    bool stop_ap_after_connected;
} wifi_provisioning_config_t;
```

```c
esp_err_t wifi_provisioning_start(
    const wifi_provisioning_config_t *config);

esp_err_t wifi_provisioning_get_status(
    wifi_provisioning_status_t *status);

esp_err_t wifi_provisioning_reset(void);
```

Portal HTTP server 由独立任务等待 AP netif ready 后启动，避免早期 socket listen 竞态。

---

# 9. GUI API

## 9.1 页面枚举

```c
typedef enum {
    GUI_PAGE_HOME = 0,
    GUI_PAGE_REGISTER,
    GUI_PAGE_RECORD,
    GUI_PAGE_STAT,
    GUI_PAGE_SCHEDULE,
} gui_page_id_t;
```

## 9.2 页面创建与切换

```c
lv_obj_t *create_home_page(void);
lv_obj_t *create_register_page(void);
lv_obj_t *create_record_page(void);
lv_obj_t *create_stat_page(void);
lv_obj_t *create_schedule_page(void);

void gui_show_page(gui_page_id_t page_id);
void gui_nav_event_cb(lv_event_t *event);
```

## 9.3 刷新接口

```c
void gui_home_handle_face_event(
    const face_event_t *event);

void gui_register_handle_face_event(
    const face_event_t *event);

void gui_record_refresh(void);
void gui_stat_refresh(void);
void gui_schedule_refresh(void);
```

`gui_record_refresh()` 和 `gui_stat_refresh()` 只展示当天数据。

---

# 10. MQTT 协议

## 10.1 连接参数

当前应用配置模型：

- Broker URI：来自 `attendance_config.json`
- 默认巴法云 TCP：`mqtt://bemfa.com:9501`
- MQTT：3.1.1
- Client ID：巴法云 UID
- Username / Password：不设置
- QoS：建议 1
- Retain：考勤建议 false

## 10.2 Topic

若：

```text
bemfa_topic = attendance004
```

则：

```text
attendance upload : attendance004/set
schedule command  : attendance004schedule
```

## 10.3 Attendance Payload

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

`status`：

```text
normal
late
absent
time_unsynced
```

## 10.4 Semester Schedule Command

```json
{
  "type": "semester_schedule",
  "semester": "2026-2027-1",
  "semester_start": "2026-09-07",
  "week_count": 18,
  "version": 1,
  "entries": [
    {
      "weekday": 2,
      "period": 3,
      "course_id": 104,
      "course_name": "Computer Networks",
      "class_id": 231,
      "weeks": [1, 3, 5, 7, 9, 11, 13, 15, 17],
      "open": "09:50",
      "start": "10:00",
      "late_after": "10:10",
      "end": "10:45"
    }
  ]
}
```

完整课表 `version` 必须在同一学期内单调递增。

## 10.5 Override Command

### Cancel

```json
{
  "type": "schedule_override",
  "version": 1,
  "date": "2026-09-21",
  "action": "cancel",
  "period": 1
}
```

### Replace

```json
{
  "type": "schedule_override",
  "version": 2,
  "date": "2026-09-22",
  "action": "replace",
  "period": 1,
  "course_id": 201,
  "course_name": "Temporary Lab",
  "class_id": 231,
  "open": "13:50",
  "start": "14:00",
  "late_after": "14:10",
  "end": "14:45"
}
```

### Add

```json
{
  "type": "schedule_override",
  "version": 3,
  "date": "2026-09-26",
  "action": "add",
  "period": 5,
  "course_id": 202,
  "course_name": "Make-up Class",
  "class_id": 231,
  "open": "13:20",
  "start": "13:30",
  "late_after": "13:40",
  "end": "14:15"
}
```

### Restore

```json
{
  "type": "schedule_override",
  "version": 4,
  "date": "2026-09-21",
  "action": "restore",
  "period": 1
}
```

### Reset all overrides

```json
{
  "type": "schedule_override_reset",
  "version": 5
}
```

Override 使用一条独立全局版本序列，后续命令的 `version` 必须严格大于设备已保存版本。

---

# 11. SNTP

Cloud service 在 Wi-Fi 获得 IP 后进行非阻塞校时。

约束：

- 不阻塞 Face / GUI；
- 默认先使用配置 NTP server；
- 超时后切换备用服务器；
- 系统时间有效后 `time()` 直接供 Timetable 和 Storage 使用。

无真实时间时，课表签到必须停止，不允许根据错误日期生成 Session。

---

# 12. 配置文件

示例：

```json
{
  "version": 1,
  "timezone": "CST-8",
  "ntp_server": "pool.ntp.org",
  "schedule": {
    "start": "07:30",
    "late_after": "08:00",
    "end": "09:00",
    "cooldown_sec": 60
  },
  "bemfa": {
    "enabled": true,
    "broker_uri": "mqtt://bemfa.com:9501",
    "uid": "YOUR_BEMFA_UID",
    "topic": "attendance004",
    "qos": 1,
    "retain": false,
    "sync_batch_size": 8
  }
}
```

注意：

- `schedule.start / late_after / end` 为兼容字段；
- 正式课程时间以 Semester / Session 为准；
- `cooldown_sec` 仍生效；
- `uid` 是敏感凭证。

---

# 13. 云同步一致性

应用层 QoS 1 规则：

1. 读取 `upload_id`；
2. 获取第一条 `record_id > upload_id`；
3. publish；
4. 标记该消息为 in-flight；
5. 等待对应 `MQTT_EVENT_PUBLISHED`；
6. 写入新的 `upload_id`；
7. 才允许下一条。

等待 PUBACK 期间禁止应用层再次 publish 同一记录。

---

# 14. 教师端 HTTP / 小程序

教师端可通过巴法云历史消息接口获取考勤 Topic 数据，并按 `record_id` 去重。

小程序业务原则：

- WSS 实时消息用于即时 UI；
- HTTP 历史查询作为兜底；
- retained / replay / offline backlog 不应触发“现场新签到”提醒；
- Excel 导出基于当前筛选后的标准化记录。

---

# 15. 兼容性说明

以下内容属于历史兼容，不应作为新功能设计依据：

- 单一“全局签到窗口”；
- “同一学生一天只能签到一次”；
- 无课程上下文的签到；
- 新建 `time_unsynced` 正式课程记录。

当前主线以 `student_id + session_id` 为课程级签到语义。

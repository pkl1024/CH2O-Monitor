# ESP32 甲醛监测仪 (CH2O-Monitor)

基于 ESP32 微控制器和 ZE08-CH2O 传感器的甲醛浓度监测系统，支持 WiFi 配网、Web 监控和数据上报。

## 功能特性

- 🏠 **ZE08-CH2O 甲醛监测** - 通过 UART 读取甲醛浓度（ppb/ppm/mg/m³）
- 📱 **智能 WiFi 配网** - AP 热点配网，配置持久化保存
- 🌐 **Web 数据展示** - 实时仪表板，响应式设计
- 📡 **数据上报** - 定时上报到服务器，支持批量缓存和重试
- ⏰ **NTP 时间同步** - 自动同步网络时间
- 🔋 **低功耗模式** - CPU 降频、WiFi Modem Sleep

## 硬件连接

```
┌─────────────────────────────────────────────────────────────┐
│                        ESP32 引脚连接                         │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ZE08-CH2O 甲醛传感器:                                       │
│    TX   →  GPIO 16 (RX2)                                     │
│    RX   →  GPIO 17 (TX2)                                     │
│    VCC  →  5V (必须 5V)                                       │
│    GND  →  GND                                                │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

## 快速开始

### 编译与上传

```bash
pio run --target upload          # 上传固件
pio device monitor               # 串口监控（115200 波特率）
```

### WiFi 配网

1. 设备启动后创建热点 `ESP32_CatShit`（密码：`12345678`）
2. 连接热点后自动弹出配网页面
3. 选择 WiFi 网络并输入密码
4. 配网成功后凭证自动保存

### HTTP 接口

| 路径 | 说明 |
|------|------|
| `/` | 仪表板页面 |
| `/sensor` | 传感器数据（JSON） |
| `/data` | 传感器数据（纯文本） |
| `/config` | WiFi 配置页面 |
| `/scan` | 扫描 WiFi 网络（JSON） |
| `/status` | WiFi 状态（JSON） |
| `/manage` | WiFi 管理页面 |
| `/report_config` | 数据上报配置 |
| `/report_status` | 数据上报状态 |
| `/report_now` | 立即上报测试 |

### JSON 数据格式

#### 传感器数据 (`/sensor`)

```json
{
  "ch2o": {
    "sensor": "ZE08-CH2O",
    "valid": true,
    "concentration_ppb": 50.00,
    "concentration_ppm": 0.0500,
    "concentration_mgm3": 0.0625,
    "status": "[正常] 甲醛浓度合格"
  },
  "timestamp": "00:01:23"
}
```

#### 数据上报格式 (POST 到服务器)

设备定时将缓存的数据批量上报到配置的服务器 URL，请求体格式：

```json
{
  "device_id": "esp32_cat_litter_001",
  "batch_size": 3,
  "samples": [
    {
      "timestamp": "2026-03-31 14:30:00",
      "uptime_ms": 3600000,
      "ch2o": {
        "valid": true,
        "concentration_ppb": 45.23,
        "concentration_ppm": 0.0452,
        "concentration_mgm3": 0.0552
      },
      "wifi_rssi": -58
    },
    {
      "timestamp": "2026-03-31 14:35:00",
      "uptime_ms": 3630000,
      "ch2o": {
        "valid": true,
        "concentration_ppb": 48.67,
        "concentration_ppm": 0.0487,
        "concentration_mgm3": 0.0594
      },
      "wifi_rssi": -62
    },
    {
      "timestamp": "2026-03-31 14:40:00",
      "uptime_ms": 3660000,
      "ch2o": {
        "valid": true,
        "concentration_ppb": 42.15,
        "concentration_ppm": 0.0422,
        "concentration_mgm3": 0.0515
      },
      "wifi_rssi": -55
    }
  ]
}
```

| 字段 | 说明 |
|------|------|
| `device_id` | 设备标识符 |
| `batch_size` | 本次上报的样本数量 |
| `samples[].timestamp` | 采样时间（NTP 同步） |
| `samples[].uptime_ms` | 设备运行时间（毫秒） |
| `samples[].ch2o.valid` | 传感器数据是否有效 |
| `samples[].ch2o.concentration_ppb` | 甲醛浓度 |
| `samples[].ch2o.concentration_ppm` | 甲醛浓度 |
| `samples[].ch2o.concentration_mgm3` | 甲醛浓度（mg/m³） |
| `samples[].wifi_rssi` | WiFi 信号强度 |

## 甲醛健康阈值

根据国家标准 GB/T 18883-2002《室内空气质量标准》：

| 等级 | 浓度范围 | 说明 |
|------|----------|------|
| 🟢 正常 | ≤ 0.08 mg/m³ | 甲醛浓度合格 |
| 🟡 注意 | 0.08 - 0.1 mg/m³ | 轻微超标，注意通风 |
| 🟠 警告 | 0.1 - 0.3 mg/m³ | 超标，建议加强通风 |
| 🔴 警报 | > 0.3 mg/m³ | 严重超标，请立即采取措施 |

## 配置说明

主要配置参数位于 `src/config.h`：

```cpp
// 甲醛健康阈值 (GB/T 18883-2002)
const float CH2O_THRESHOLD_NORMAL = 0.08f;   // 正常阈值 mg/m³
const float CH2O_THRESHOLD_WARNING = 0.10f;  // 注意阈值 mg/m³
const float CH2O_THRESHOLD_ALERT = 0.30f;    // 警报阈值 mg/m³

// 数据上报配置
const unsigned long REPORT_INTERVAL = 1800000;  // 上报间隔: 30分钟
const char* const DEVICE_ID = "esp32_cat_litter_001"; // 设备ID

// 节能配置
const int CPU_FREQ_MHZ = 80;  // CPU频率: 80MHz (节能模式)
```

## 仿真模式

如无硬件可启用仿真模式：

1. 在 `src/config.h` 中取消注释 `#define SIMULATION_MODE`
2. 编译上传，系统将生成模拟数据

## 文件结构

```
src/
├── main.cpp              # 主程序入口
├── config.h              # 配置参数
├── sensors/
│   └── ZE08CH2OSensor.h  # ZE08-CH2O 传感器驱动
├── wifi/
│   └── WiFiManager.h     # WiFi 配网管理
├── web/
│   └── WebServer.h       # Web 服务器
└── data/
    ├── DataReporter.h    # 数据上报模块
    └── TimeSync.h        # NTP 时间同步
```

## 传感器预热

ZE08-CH2O 传感器需要预热 **3-5 分钟** 才能获得稳定读数。

## 许可证

MIT License

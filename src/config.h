/**
 * @file config.h
 * @brief 项目配置参数 - 节能版本
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============== 仿真模式开关 ==============
// #define SIMULATION_MODE  // 如果定义了则使用模拟数据，否则使用真实硬件

// ============== 节能模式开关 ==============
#define ENABLE_POWER_SAVING   // 启用节能模式
// #define DISABLE_SERIAL         // 关闭串口调试
#define DISABLE_LED            // 关闭板载LED

// ============== AP热点配置 ==============
const char* const AP_SSID = "ESP32_CatShit";    // AP热点名称
const char* const AP_PASSWORD = "12345678";      // AP热点密码(至少8位)
const int AP_CHANNEL = 1;                          // AP信道
const unsigned long AP_TIMEOUT = 60000;           // AP热点开启1分钟后关闭
const unsigned long WIFI_CHECK_INTERVAL = 10000; // 每10秒检查WiFi连接

// ============== ZE08-CH2O甲醛传感器配置 ==============
const int ZE08_BAUDRATE = 9600;   // 串口波特率
const int ZE08_DATA_LEN = 9;       // 数据包长度
const int ZE08_RX_PIN = 16;        // RX2 引脚
const int ZE08_TX_PIN = 17;        // TX2 引脚

// 甲醛浓度转换系数
const float CH2O_PPM_TO_MG_M3 = 1.25f; // 标准温度压力下, 1 ppm CH2O = 1.25 mg/m³

// 甲醛健康阈值 (GB/T 18883-2002)
const float CH2O_THRESHOLD_NORMAL = 0.08f;   // 正常阈值 mg/m³
const float CH2O_THRESHOLD_WARNING = 0.10f;  // 注意阈值 mg/m³
const float CH2O_THRESHOLD_ALERT = 0.30f;     // 警报阈值 mg/m³

// ============== Web服务器配置 ==============
const int WEB_SERVER_PORT = 80;

// ============== 串口配置 ==============
#ifndef DISABLE_SERIAL
const int SERIAL_BAUDRATE = 115200;
#endif

// ============== 数据上报配置 ==============
const unsigned long REPORT_INTERVAL = 1800000;  // 上报间隔: 30分钟 (1800000ms)
const unsigned long CACHE_SAMPLE_INTERVAL = 5000; // 缓存采样间隔: 5秒 (5000ms)
const int MAX_CACHED_SAMPLES = 200; // 最大缓存样本数 (约16分钟数据)
const unsigned long RETRY_INTERVAL = 60000; // 重试间隔: 60秒 (60000ms)
const char* const REPORT_PATH = "/api/collect"; // 上报API路径
const char* const DEVICE_ID = "esp32_cat_litter_001"; // 设备ID

// ============== NTP时间同步配置 ==============
const char* const NTP_SERVER = "ntp.aliyun.com";
const long GMT_OFFSET_SEC = 8 * 3600;  // 中国时区 UTC+8
const int DAYLIGHT_OFFSET_SEC = 0;

// ============== 节能配置 ==============
const unsigned long MAIN_LOOP_DELAY = 100;  // 主循环延迟: 100ms
const int CPU_FREQ_MHZ = 80;                  // CPU频率: 80MHz (节能模式)

#endif // CONFIG_H

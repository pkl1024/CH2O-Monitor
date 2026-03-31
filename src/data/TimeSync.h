/**
 * @file TimeSync.h
 * @brief NTP时间同步模块
 */

#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "../config.h"

#ifndef DISABLE_SERIAL
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif

/**
 * @brief 时间同步类
 */
class TimeSync {
public:
    /**
     * @brief 构造函数
     */
    TimeSync() : time_synced_(false), last_sync_attempt_(0), sync_interval_(3600000) {}

    /**
     * @brief 初始化时间同步
     */
    void begin() {
        DEBUG_PRINTLN("时间同步模块初始化完成");
    }

    /**
     * @brief 更新时间同步（应在loop中调用）
     */
    void update() {
        unsigned long current_time = millis();

        if (WiFi.status() == WL_CONNECTED) {
            if (!time_synced_ || (current_time - last_sync_attempt_ >= sync_interval_)) {
                syncTime();
                last_sync_attempt_ = current_time;
            }
        }
    }

    /**
     * @brief 强制立即同步时间
     */
    bool syncNow() {
        return syncTime();
    }

    /**
     * @brief 检查时间是否已同步
     */
    bool isSynced() const { return time_synced_; }

    /**
     * @brief 获取当前时间字符串
     */
    String getTimeString() {
        if (!time_synced_) {
            return "未同步";
        }
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo)) {
            return "获取时间失败";
        }
        char buffer[20];
        strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
        return buffer;
    }

    /**
     * @brief 获取完整日期时间字符串
     */
    String getDateTimeString() {
        if (!time_synced_) {
            return "1970-01-01 00:00:00";
        }
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo)) {
            return "1970-01-01 00:00:00";
        }
        char buffer[24];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
        return buffer;
    }

    /**
     * @brief 获取状态JSON
     */
    String getStatusJSON() const {
        String json = "{";
        json += "\"synced\":" + String(time_synced_ ? "true" : "false");
        if (time_synced_) {
            json += ",\"server\":\"" + String(NTP_SERVER) + "\"";
        }
        json += "}";
        return json;
    }

private:
    bool time_synced_;
    unsigned long last_sync_attempt_;
    unsigned long sync_interval_;

    /**
     * @brief 执行NTP时间同步
     */
    bool syncTime() {
        DEBUG_PRINT("[NTP] 正在同步时间... ");

        configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

        struct tm timeinfo;
        int retry_count = 0;
        const int max_retries = 10;

        while (!getLocalTime(&timeinfo) && retry_count < max_retries) {
            delay(500);
            retry_count++;
        }

        if (retry_count < max_retries) {
            time_synced_ = true;
            DEBUG_PRINTLN("成功!");
            DEBUG_PRINT("[NTP] 当前时间: ");
            DEBUG_PRINTLN(getDateTimeString());
            return true;
        } else {
            DEBUG_PRINTLN("失败!");
            return false;
        }
    }
};

#endif // TIME_SYNC_H

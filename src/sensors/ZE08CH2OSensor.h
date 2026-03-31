/**
 * @file ZE08CH2OSensor.h
 * @brief 炜盛 ZE08-CH2O 甲醛传感器驱动
 *
 * 通信协议: UART (9600, 8N1)
 * 数据包格式: 9 字节（主动上传模式）
 *  [0] 0xFF - 起始位
 *  [1] 0x17 - 气体名称(CH2O)
 *  [2] 0x04 - 单位(ppb)
 *  [3] 0x00 - 小数位数
 *  [4] HH   - 气体浓度高位 (ppb)
 *  [5] LL   - 气体浓度低位 (ppb)
 *  [6] HH   - 满量程高位
 *  [7] LL   - 满量程低位
 *  [8] CS   - 校验和 (Byte1-Byte7和取反+1)
 */

#ifndef ZE08_CH2O_SENSOR_H
#define ZE08_CH2O_SENSOR_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include "../config.h"

/**
 * @brief ZE08-CH2O 传感器数据结构
 */
struct CH2OData {
    bool valid;              // 数据是否有效
    float concentration_ppb; // 甲醛浓度 (ppb)
    float concentration_ppm; // 甲醛浓度 (ppm)
    float concentration_mg;  // 甲醛浓度 (mg/m³)
    uint8_t raw_data[ZE08_DATA_LEN]; // 原始数据
    String status;           // 健康状态描述

    CH2OData()
        : valid(false), concentration_ppb(0.0f),
          concentration_ppm(0.0f), concentration_mg(0.0f),
          status("初始化中") {
        memset(raw_data, 0, ZE08_DATA_LEN);
    }
};

/**
 * @brief ZE08-CH2O 传感器类
 */
class ZE08CH2OSensor {
public:
    /**
     * @brief 构造函数
     * @param serial 硬件串口引用（默认使用 Serial2）
     */
    ZE08CH2OSensor(HardwareSerial& serial = Serial2)
        : serial_(serial), buffer_index_(0), last_valid_time_(0) {}

    /**
     * @brief 初始化串口
     */
    void begin() {
        serial_.begin(ZE08_BAUDRATE, SERIAL_8N1, ZE08_RX_PIN, ZE08_TX_PIN);
        buffer_index_ = 0;
        last_valid_time_ = 0;
    }

    /**
     * @brief 更新传感器数据（应该在 loop 中调用）
     */
    void update() {
        static unsigned long last_read_time = 0;
        unsigned long current_time = millis();

        // 每200ms处理一次
        if (current_time - last_read_time < 200) {
            return;
        }
        last_read_time = current_time;

#ifdef SIMULATION_MODE
        readSimulatedData();
#else
        readSerialData();
#endif

        checkTimeout(current_time);
    }

    /**
     * @brief 获取传感器数据
     * @return 常量引用的传感器数据
     */
    const CH2OData& getData() const { return data_; }

    /**
     * @brief 获取 JSON 格式的数据
     * @return JSON 字符串
     */
    String getJSON() const {
        String json = "{";
        json += "\"sensor\":\"ZE08-CH2O\",";
        json += "\"valid\":" + String(data_.valid ? "true" : "false") + ",";
        json += "\"concentration_ppb\":" + String(data_.concentration_ppb, 2) + ",";
        json += "\"concentration_ppm\":" + String(data_.concentration_ppm, 4) + ",";
        json += "\"concentration_mgm3\":" + String(data_.concentration_mg, 4) + ",";
        json += "\"status\":\"" + data_.status + "\"";
        json += "}";
        return json;
    }

    /**
     * @brief 获取纯文本格式的数据
     * @return 文本字符串
     */
    String getText() const {
        String text = "--- ZE08-CH2O 甲醛监测 ---\n";
        text += "数据状态: " + String(data_.valid ? "有效" : "无效") + "\n";
        text += "甲醛浓度: " + String(data_.concentration_ppb, 2) + " ppb\n";
        text += "甲醛浓度: " + String(data_.concentration_ppm, 4) + " ppm\n";
        text += "甲醛浓度: " + String(data_.concentration_mg, 4) + " mg/m³\n";
        text += "健康状态: " + data_.status + "\n";
        return text;
    }

private:
    /**
     * @brief 从串口读取数据
     */
    void readSerialData() {
        while (serial_.available() > 0) {
            uint8_t byte = serial_.read();

            // 寻找帧头 (0xFF)
            if (buffer_index_ == 0 && byte != 0xFF) {
                continue;
            }

            buffer_[buffer_index_++] = byte;

            // 接收到完整数据包
            if (buffer_index_ >= ZE08_DATA_LEN) {
                processPacket();
                buffer_index_ = 0;
            }
        }
    }

    /**
     * @brief 处理接收到的数据包
     */
    void processPacket() {
        // 验证数据包起始字节、气体名称和单位
        if (buffer_[0] != 0xFF || buffer_[1] != 0x17 || buffer_[2] != 0x04) {
            return;
        }

        // 验证校验和
        if (!verifyChecksum(buffer_)) {
            data_.status = "校验和错误";
            data_.valid = false;
            return;
        }

        // 保存原始数据
        memcpy(data_.raw_data, buffer_, ZE08_DATA_LEN);

        // 计算甲醛浓度 (单位: ppb)
        int16_t raw_concentration = (buffer_[4] << 8) | buffer_[5];
        data_.concentration_ppb = static_cast<float>(raw_concentration);

        // 转换为 ppm (1 ppm = 1000 ppb)
        data_.concentration_ppm = data_.concentration_ppb / 1000.0f;

        // 转换为 mg/m³
        data_.concentration_mg = data_.concentration_ppm * CH2O_PPM_TO_MG_M3;

        data_.valid = true;
        last_valid_time_ = millis();

        updateStatus();
    }

    /**
     * @brief 校验和验证
     * @param data 9字节数据包
     * @return true 校验成功
     */
    bool verifyChecksum(uint8_t* data) {
        uint8_t sum = 0;
        for (int i = 0; i < 8; i++) {
            sum += data[i];
        }
        // ZE08-CH2O 使用只取反，不加1的校验和算法
        uint8_t checksum = ~sum;
        return (checksum == data[8]);
    }

    /**
     * @brief 读取模拟数据（用于测试）
     */
    void readSimulatedData() {
        static float sim_ppb = 40.0f;
        sim_ppb += random(-10, 10);
        sim_ppb = constrain(sim_ppb, 0.0f, 200.0f);

        data_.concentration_ppb = sim_ppb;
        data_.concentration_ppm = sim_ppb / 1000.0f;
        data_.concentration_mg = data_.concentration_ppm * CH2O_PPM_TO_MG_M3;
        data_.valid = true;
        last_valid_time_ = millis();

        updateStatus();
    }

    /**
     * @brief 更新健康状态描述
     */
    void updateStatus() {
        if (data_.concentration_mg <= CH2O_THRESHOLD_NORMAL) {
            data_.status = "[正常] 甲醛浓度合格";
        } else if (data_.concentration_mg <= CH2O_THRESHOLD_WARNING) {
            data_.status = "[注意] 接近限值";
        } else if (data_.concentration_mg <= CH2O_THRESHOLD_ALERT) {
            data_.status = "[警告] 轻度超标";
        } else {
            data_.status = "[警报] 严重超标";
        }
    }

    /**
     * @brief 检查数据超时
     */
    void checkTimeout(unsigned long current_time) {
        // 超时时间延长到 30 秒，避免频繁跳变
        // 一旦数据有效，即使暂时没收到新数据，也保持最后一次的值
        if (data_.valid && (current_time - last_valid_time_ > 30000)) {
            data_.status = "信号弱";
            // 不把 valid 设为 false，继续显示最后一次读数
        }
    }

    HardwareSerial& serial_;
    CH2OData data_;
    uint8_t buffer_[ZE08_DATA_LEN];
    int buffer_index_;
    unsigned long last_valid_time_;
};

#endif // ZE08_CH2O_SENSOR_H

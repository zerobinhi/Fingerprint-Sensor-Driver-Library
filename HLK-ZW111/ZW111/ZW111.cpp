#include <stdio.h>
#include <string.h>
#include <cstdint>
#include <stdarg.h> // 用于可变参数（宏定义日志需）

// ========================== 日志宏定义（适配Visual Studio） ==========================
// 模拟ESP_INFO级日志（绿色标识）
#define ESP_LOGI(tag, fmt, ...)                                         \
    do                                                                  \
    {                                                                   \
        printf("\033[32m[I][%s] " fmt "\033[0m\n", tag, ##__VA_ARGS__); \
    } while (0)
// 模拟ESP_ERROR级日志（红色标识）
#define ESP_LOGE(tag, fmt, ...)                                         \
    do                                                                  \
    {                                                                   \
        printf("\033[31m[E][%s] " fmt "\033[0m\n", tag, ##__VA_ARGS__); \
    } while (0)
// 模拟ESP_LOG_BUFFER_HEX（打印缓冲区十六进制）
static void ESP_LOG_BUFFER_HEX(const char *tag, const uint8_t *buf, size_t len)
{
    if (buf == NULL || len == 0)
    {
        ESP_LOGI(tag, "Buffer is NULL or empty");
        return;
    }
    // 开始绿色显示，包含标签和数据
    printf("\033[32m[I][%s] ", tag);
    for (size_t i = 0; i < len; i++)
    {
        printf("%02X ", buf[i]);
    }
    // 结束绿色显示
    printf("\033[0m\n");
}

// ========================== 原有宏定义（以下为你的代码） ==========================
#define esp_err_t int
#define ESP_OK 0
#define ESP_FAIL -1

// ========================== 通用宏定义 ==========================
// 功能码宏定义（LED控制）
#define BLN_BREATH 1   // 普通呼吸灯模式
#define BLN_FLASH 2    // 闪烁灯模式
#define BLN_ON 3       // 常亮模式
#define BLN_OFF 4      // 常闭模式
#define BLN_FADE_IN 5  // 渐亮模式
#define BLN_FADE_OUT 6 // 渐暗模式
#define BLN_COLORFUL 7 // 七彩循环模式

// LED颜色宏定义
#define LED_OFF 0x00   // 全灭
#define LED_BLUE 0x01  // 蓝灯
#define LED_GREEN 0x02 // 绿灯
#define LED_RED 0x04   // 红灯
#define LED_BG 0x03    // 蓝+绿灯
#define LED_BR 0x05    // 蓝+红灯
#define LED_GR 0x06    // 绿+红灯
#define LED_ALL 0x07   // 红+绿+蓝全亮

// 包标识定义
#define PACKET_CMD 0x01       // 命令包（主机发送指令）
#define PACKET_DATA_MORE 0x02 // 数据包（有后续包）
#define PACKET_DATA_LAST 0x08 // 最后一个数据包（无后续）
#define PACKET_RESPONSE 0x07  // 应答包（模块返回结果）

// 指令码定义
#define CMD_AUTO_ENROLL 0x31      // 自动注册指纹指令
#define CMD_AUTO_IDENTIFY 0x32    // 自动识别指纹指令
#define CMD_CONTROL_BLN 0x3C      // 背光灯（LED）控制指令
#define CMD_DELET_CHAR 0x0C       // 删除指定指纹指令
#define CMD_EMPTY 0x0D            // 清空所有指纹指令
#define CMD_CANCEL 0x30           // 取消当前操作指令
#define CMD_READ_INDEX_TABLE 0x1F // 读取指纹索引表指令
#define CMD_SLEEP 0x33            // 模块休眠指令

// 帧结构常量（避免硬编码，增强可维护性）
#define CHECKSUM_LEN 2                            // 校验和长度（字节）
#define CHECKSUM_START_INDEX 6                    // 校验和计算起始索引（固定，从0开始）
static const char *TAG = "SmartLock Fingerprint"; // 日志标签
const uint8_t FRAME_HEADER[2] = {0xEF, 0x01};     // 指纹模块帧头固定值

struct fingerprint_device
{
    /**
     * 0X00 刚开机的状态
     * 0X01 读索引表状态
     * 0X02 注册指纹状态
     * 0X03 删除指纹状态
     * 0X04 验证指纹状态
     * 0X0A 取消命令状态
     * 0X0B 准备关机状态
     */
    uint8_t state;
    /**
     * false 断电状态
     * true 上电状态
     */
    bool power;
    // 设备地址（4字节），默认地址0xFFFFFFFF，可修改
    uint8_t deviceAddress[4];
    // 已注册指纹ID数组，最大支持100枚（0-99），未使用位置为0xFF
    uint8_t fingerIDArray[100];
    // 当前有效指纹数量
    uint8_t fingerNumber;
};

struct fingerprint_device zw111; // 定义指纹模块数据结构变量

// ========================== 通用工具函数 ==========================
/**
 * @brief 计算数据帧的校验和
 * @param receive_data 数据帧缓冲区
 * @param data_length 数据帧总长度
 * @return uint16_t 计算得到的16位校验和（高字节在前），参数无效返回0
 */
static uint16_t calculate_checksum(const uint8_t *receive_data, uint16_t data_length)
{
    uint16_t checksum = 0;
    uint8_t checksumEndIndex = data_length - CHECKSUM_LEN - 1; // 校验和前一字节索引
    // 累加校验范围：从CHECKSUM_START_INDEX到checksumEndIndex
    for (uint8_t i = CHECKSUM_START_INDEX; i <= checksumEndIndex; i++)
    {
        checksum += receive_data[i];
    }
    return (checksum & 0xFFFF);
}

/**
 * @brief 校验指纹模块接收数据的有效性
 * @param receive_data 接收的数据包缓冲区
 * @param data_length 实际接收的字节数
 * @return esp_err_t 校验结果：ESP_OK=数据是有效的，ESP_FAIL=数据是无效的
 */
static esp_err_t verify_received_data(const uint8_t *receive_data, uint16_t data_length)
{
    // 基础合法性检查
    if (receive_data == NULL || data_length < 12)
    {
        ESP_LOGE(TAG, "校验失败：数据为空或长度不足（最小需9字节，实际%u）", data_length);
        return ESP_FAIL;
    }
    // 验证数据长度
    uint16_t expectedDataLen = (receive_data[7] << 8) | receive_data[8]; // 数据区长度
    if (expectedDataLen + 9 != data_length)
    {
        ESP_LOGE(TAG, "校验失败：长度不匹配（期望总长度%u，实际%u）", 9 + expectedDataLen, data_length);
        return ESP_FAIL;
    }
    // 验证帧头
    if (receive_data[0] != FRAME_HEADER[0] || receive_data[1] != FRAME_HEADER[1])
    {
        ESP_LOGE(TAG, "校验失败：帧头不匹配（期望%02X%02X，实际%02X%02X）", FRAME_HEADER[0], FRAME_HEADER[1], receive_data[0], receive_data[1]);
        return ESP_FAIL;
    }
    // 验证设备地址
    for (int i = 2; i < 6; i++)
    {
        if (receive_data[i] != zw111.deviceAddress[i - 2])
        {
            ESP_LOGE(TAG, "校验失败：设备地址不匹配（期望%02X%02X%02X%02X，实际%02X%02X%02X%02X）",
                     zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3],
                     receive_data[2], receive_data[3], receive_data[4], receive_data[5]);
            return ESP_FAIL;
        }
    }
    // 验证包标识
    if (receive_data[6] != PACKET_RESPONSE)
    {
        ESP_LOGE(TAG, "校验失败：包标识错误（期望应答包%02X，实际%02X）", PACKET_RESPONSE, receive_data[6]);
        return ESP_FAIL;
    }
    // 验证校验和
    uint16_t receivedChecksum = (receive_data[data_length - 2] << 8) | receive_data[data_length - 1];
    if (calculate_checksum(receive_data, data_length) != receivedChecksum)
    {
        ESP_LOGE(TAG, "校验失败：校验和不匹配（期望0x%04X，实际0x%04X）", calculate_checksum(receive_data, data_length), receivedChecksum);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "校验成功：数据有效");
    return ESP_OK;
}
// ========================== 功能函数 ==========================
/**
 * @brief 指纹模块自动注册函数
 * @param ID 指纹ID号（0-99，超出范围返回失败）
 * @param enrollTimes 录入次数（2-255，超出返回失败）
 * @param ledControl 采图背光灯控制：false=常亮；true=采图成功后熄灭
 * @param preprocess 采图预处理控制：false=不预处理；true=开启预处理
 * @param returnStatus 注册状态返回控制：false=返回状态；true=不返回状态
 * @param allowOverwrite ID覆盖控制：false=不允许覆盖；true=允许覆盖
 * @param allowDuplicate 重复注册控制：false=允许重复；true=禁止重复
 * @param requireRemove 手指离开要求：false=需离开；true=无需离开
 * @return esp_err_t 操作结果：ESP_OK=命令发送成功，ESP_FAIL=命令发送失败
 */
static esp_err_t auto_enroll(uint16_t ID, uint8_t enrollTimes,
                             bool ledControl, bool preprocess,
                             bool returnStatus, bool allowOverwrite,
                             bool allowDuplicate, bool requireRemove)
{
    // 检查ID有效性
    if (ID >= 100)
    {
        ESP_LOGE(TAG, "注册失败：ID超出范围（需0-99，当前%u）", ID);
        return ESP_FAIL;
    }
    // 检查录入次数有效性
    if (enrollTimes < 2)
    {
        ESP_LOGE(TAG, "注册失败：录入次数超出范围（需2-255，当前%u）", enrollTimes);
        return ESP_FAIL;
    }
    // 组装控制参数
    uint16_t param = 0;
    param |= (ledControl ? 1 << 0 : 0);     // bit0: 背光灯控制
    param |= (preprocess ? 1 << 1 : 0);     // bit1: 预处理控制
    param |= (returnStatus ? 1 << 2 : 0);   // bit2: 状态返回控制
    param |= (allowOverwrite ? 1 << 3 : 0); // bit3: ID覆盖控制
    param |= (allowDuplicate ? 1 << 4 : 0); // bit4: 重复注册控制
    param |= (requireRemove ? 1 << 5 : 0);  // bit5: 手指离开控制
    // 构建数据帧
    uint8_t frame[17] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // 帧头(2字节)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // 设备地址(4字节)
        PACKET_CMD,                                                                                     // 包标识(1字节)
        0x00, 0x08,                                                                                     // 数据长度(2字节，固定8字节)
        CMD_AUTO_ENROLL,                                                                                // 指令码(1字节)
        (uint8_t)(ID >> 8), (uint8_t)ID,                                                                // 指纹ID(2字节，高字节在前)
        enrollTimes,                                                                                    // 录入次数(1字节)
        (uint8_t)(param >> 8), (uint8_t)param,                                                          // 控制参数(2字节，高字节在前)
        0x00, 0x00                                                                                      // 校验和(2字节，待计算)
    };
    // 计算并填充校验和
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[15] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[16] = (uint8_t)(checksum & 0xFF); // 校验和低字节
    // 调试输出帧信息
    ESP_LOGI(TAG, "发送自动注册指令: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
    // UART发送命令
    int bytes_written = 1;
    if (bytes_written == 1)
    {
        // 发送成功
        ESP_LOGI(TAG, "自动注册指令发送成功");
        return ESP_OK;
    }
    else
    {
        // 发送失败
        ESP_LOGE(TAG, "发送失败，实际发送字节数: %d", bytes_written);
        return ESP_FAIL;
    }
}

/**
 * @brief 指纹模块自动识别函数
 * @param ID 指纹ID号：具体数值(0-99)=验证指定ID；0xFFFF=验证所有已注册指纹
 * @param scoreLevel 比对分数等级（1-5，等级越高严格度越高，默认建议2）
 * @param ledControl 采图背光灯控制：false=常亮；true=采图成功后熄灭
 * @param preprocess 采图预处理控制：false=不预处理；true=开启预处理
 * @param returnStatus 识别状态返回控制：false=返回状态；true=不返回状态
 * @return esp_err_t 操作结果：ESP_OK=命令发送成功，ESP_FAIL=参数无效或命令发送失败
 */
static esp_err_t auto_identify(uint16_t ID, uint8_t scoreLevel, bool ledControl, bool preprocess, bool returnStatus)
{
    if (scoreLevel < 1 || scoreLevel > 5)
    {
        ESP_LOGE(TAG, "自动识别失败：分数等级无效（需1-5，当前%u）", scoreLevel);
        return ESP_FAIL;
    }
    // 组装控制参数
    uint16_t param = 0;
    param |= (ledControl ? 1 << 0 : 0);   // bit0: 背光灯控制
    param |= (preprocess ? 1 << 1 : 0);   // bit1: 预处理控制
    param |= (returnStatus ? 1 << 2 : 0); // bit2: 状态返回控制
    // 构建数据帧
    uint8_t frame[17] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // 帧头(2字节)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // 设备地址(4字节)
        PACKET_CMD,                                                                                     // 包标识(1字节)
        0x00, 0x08,                                                                                     // 数据长度(2字节，固定8字节)
        CMD_AUTO_IDENTIFY,                                                                              // 指令码(1字节)
        scoreLevel,                                                                                     // 分数等级(1字节)
        (uint8_t)(ID >> 8), (uint8_t)ID,                                                                // 指纹ID(2字节，高字节在前)
        (uint8_t)(param >> 8), (uint8_t)param,                                                          // 控制参数(2字节，高字节在前)
        0x00, 0x00                                                                                      // 校验和(2字节，待计算)
    };
    // 计算并填充校验和
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[15] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[16] = (uint8_t)(checksum & 0xFF); // 校验和低字节
    // 调试输出帧信息
    ESP_LOGI(TAG, "发送自动识别指令: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
    // UART发送命令
    int bytes_written = 1;
    if (bytes_written == 1)
    {
        // 发送成功
        ESP_LOGI(TAG, "自动识别指令发送成功");
        return ESP_OK;
    }
    else
    {
        // 发送失败
        ESP_LOGE(TAG, "发送失败，实际发送字节数: %d", bytes_written);
        return ESP_FAIL;
    }
}

/**
 * @brief 指纹模块LED控制函数（支持呼吸、闪烁、开关等模式）
 * @param functionCode 功能码（1-6，参考BLN_xxx宏定义，如BLN_BREATH=呼吸灯）
 * @param startColor 起始颜色（bit0-蓝,bit1-绿,bit2-红，参考LED_xxx宏定义）
 * @param endColor 结束颜色（仅功能码1-呼吸灯有效，其他模式忽略）
 * @param cycleTimes 循环次数（仅功能码1-呼吸灯/2-闪烁灯有效，0=无限循环）
 * @return esp_err_t 操作结果：ESP_OK=命令发送成功，ESP_FAIL=参数无效或命令发送失败
 */
static esp_err_t control_led(uint8_t functionCode, uint8_t startColor,
                             uint8_t endColor, uint8_t cycleTimes)
{
    // 参数合法性检查
    if (functionCode < BLN_BREATH || functionCode > BLN_FADE_OUT)
    {
        ESP_LOGE(TAG, "LED控制失败：功能码无效（需1-6，当前%u）", functionCode);
        return ESP_FAIL;
    }
    // 过滤颜色参数无效位（仅保留低3位）
    if ((startColor & 0xF8) != 0)
    {
        ESP_LOGE(TAG, "LED控制警告：起始颜色仅低3位有效，已过滤为0x%02X\n", startColor & 0x07);
        startColor &= 0x07;
    }
    if ((endColor & 0xF8) != 0)
    {
        ESP_LOGE(TAG, "LED控制警告：结束颜色仅低3位有效，已过滤为0x%02X\n", endColor & 0x07);
        endColor &= 0x07;
    }
    // 构建数据帧
    uint8_t frame[16] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // 帧头(2字节)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // 设备地址(4字节)
        PACKET_CMD,                                                                                     // 包标识(1字节)
        0x00, 0x07,                                                                                     // 数据长度(2字节，固定7字节)
        CMD_CONTROL_BLN,                                                                                // 指令码(1字节)
        functionCode,                                                                                   // 功能码(1字节)
        startColor,                                                                                     // 起始颜色(1字节)
        endColor,                                                                                       // 结束颜色(1字节)
        cycleTimes,                                                                                     // 循环次数(1字节)
        0x00, 0x00                                                                                      // 校验和(2字节，待计算)
    };
    // 计算并填充校验和
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[14] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[15] = (uint8_t)(checksum & 0xFF); // 校验和低字节
    // 调试输出帧信息
    ESP_LOGI(TAG, "发送LED控制帧: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
    // UART发送命令
    int bytes_written = 1;
    if (bytes_written == 1)
    {
        // 发送成功
        ESP_LOGI(TAG, "LED控制指令发送成功");
        return ESP_OK;
    }
    else
    {
        // 发送失败
        ESP_LOGE(TAG, "发送失败，实际发送字节数: %d", bytes_written);
        return ESP_FAIL;
    }
}

/**
 * @brief 指纹模块LED跑马灯控制函数（七彩循环模式）
 * @param startColor 起始颜色配置（参考LED_xxx宏定义，仅低3位有效）
 * @param timeBit 呼吸周期时间参数（1-100，对应0.1秒-10秒）
 * @param cycleTimes 循环次数（0=无限循环）
 * @return esp_err_t 操作结果：ESP_OK=命令发送成功，ESP_FAIL=参数无效或命令发送失败
 */
static esp_err_t control_colorful_led(uint8_t startColor, uint8_t timeBit, uint8_t cycleTimes)
{
    // 参数合法性检查
    if (timeBit < 1 || timeBit > 100)
    {
        ESP_LOGE(TAG, "跑马灯控制失败：时间参数无效（需1-100，当前%u）", timeBit);
        return ESP_FAIL;
    }
    // 过滤颜色参数无效位
    if ((startColor & 0xF8) != 0)
    {
        ESP_LOGE(TAG, "跑马灯控制失败：起始颜色仅低3位有效，已过滤为0x%02X\n", startColor & 0x07);
        startColor &= 0x07;
    }
    // 构建数据帧
    uint8_t frame[17] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // 帧头(2字节)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // 设备地址(4字节)
        PACKET_CMD,                                                                                     // 包标识(1字节)
        0x00, 0x08,                                                                                     // 数据长度(2字节，固定8字节)
        CMD_CONTROL_BLN,                                                                                // 指令码(1字节)
        BLN_COLORFUL,                                                                                   // 功能码(1字节，七彩模式)
        startColor,                                                                                     // 起始颜色(1字节)
        0x11,                                                                                           // 占空比固定值
        cycleTimes,                                                                                     // 循环次数(1字节)
        timeBit,                                                                                        // 周期时间参数(1字节)
        0x00, 0x00                                                                                      // 校验和(2字节，待计算)
    };
    // 计算并填充校验和
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[15] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[16] = (uint8_t)(checksum & 0xFF); // 校验和低字节
    // 调试输出帧信息
    ESP_LOGI(TAG, "发送跑马灯控制帧: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
    // UART发送命令
    int bytes_written = 1;
    if (bytes_written == 1)
    {
        // 发送成功
        ESP_LOGI(TAG, "跑马灯控制指令发送成功");
        return ESP_OK;
    }
    else
    {
        // 发送失败
        ESP_LOGE(TAG, "发送失败，实际发送字节数: %d", bytes_written);
        return ESP_FAIL;
    }
}

/**
 * @brief 删除指定数量的指纹（从指定ID开始连续删除）
 * @param ID 起始指纹ID（0-99，超出范围返回失败）
 * @param count 删除数量（1-100，需确保不超出ID范围）
 * @return esp_err_t 操作结果：ESP_OK=命令发送成功，ESP_FAIL=参数无效或命令发送失败
 */
esp_err_t delete_char(uint16_t ID, uint16_t count)
{
    // 参数合法性检查
    if (ID >= 100)
    {
        // ID超出范围
        ESP_LOGE(TAG, "删除失败：起始ID超出范围（需0-99，当前%u）", ID);
        return ESP_FAIL;
    }
    if (count == 0 || count > 100 || (ID + count) > 100)
    {
        ESP_LOGE(TAG, "删除失败：数量无效（需1-100且不超出ID范围，当前数量%u）", count);
        // 数量无效或超出ID范围
        return ESP_FAIL;
    }
    // 构建数据帧
    uint8_t frame[16] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // 帧头(2字节)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // 设备地址(4字节)
        PACKET_CMD,                                                                                     // 包标识(1字节)
        0x00, 0x07,                                                                                     // 数据长度(2字节，固定7字节)
        CMD_DELET_CHAR,                                                                                 // 指令码(1字节)
        (uint8_t)(ID >> 8), (uint8_t)ID,                                                                // 起始ID(2字节，高字节在前)
        (uint8_t)(count >> 8), (uint8_t)count,                                                          // 删除数量(2字节，高字节在前)
        0x00, 0x00                                                                                      // 校验和(2字节，待计算)
    };
    // 计算并填充校验和
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[14] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[15] = (uint8_t)(checksum & 0xFF); // 校验和低字节
    // 调试输出帧信息
    ESP_LOGI(TAG, "发送删除指纹帧: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
    // UART发送命令
    int bytes_written = 1;
    if (bytes_written == 1)
    {
        // 发送成功
        ESP_LOGI(TAG, "删除指纹指令发送成功");
        return ESP_OK;
    }
    else
    {
        // 发送失败
        ESP_LOGE(TAG, "发送失败，实际发送字节数: %d", bytes_written);
        return ESP_FAIL;
    }
}

/**
 * @brief 清空模块中所有已注册的指纹
 * @return esp_err_t 操作结果：ESP_OK=命令发送成功，ESP_FAIL=命令发送失败
 */
static esp_err_t empty()
{
    // 构建数据帧
    uint8_t frame[12] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // 帧头(2字节)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // 设备地址(4字节)
        PACKET_CMD,                                                                                     // 包标识(1字节)
        0x00, 0x03,                                                                                     // 数据长度(2字节，固定3字节)
        CMD_EMPTY,                                                                                      // 指令码(1字节)
        0x00, 0x00                                                                                      // 校验和(2字节，待计算)
    };
    // 计算并填充校验和
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[10] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[11] = (uint8_t)(checksum & 0xFF); // 校验和低字节
    // 调试输出帧信息
    ESP_LOGI(TAG, "发送清空指纹帧: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
    // UART发送命令
    int bytes_written = 1;
    if (bytes_written == 1)
    {
        // 发送成功
        ESP_LOGI(TAG, "清空指纹指令发送成功");
        return ESP_OK;
    }
    else
    {
        // 发送失败
        ESP_LOGE(TAG, "发送失败，实际发送字节数: %d", bytes_written);
        return ESP_FAIL;
    }
}

/**
 * @brief 取消模块当前正在执行的操作（如注册、识别等）
 * @return esp_err_t 操作结果：ESP_OK=命令发送成功，ESP_FAIL=命令发送失败
 */
static esp_err_t cancel()
{
    // 构建数据帧
    uint8_t frame[12] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // 帧头(2字节)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // 设备地址(4字节)
        PACKET_CMD,                                                                                     // 包标识(1字节)
        0x00, 0x03,                                                                                     // 数据长度(2字节，固定3字节)
        CMD_CANCEL,                                                                                     // 指令码(1字节)
        0x00, 0x00                                                                                      // 校验和(2字节，待计算)
    };
    // 计算并填充校验和
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[10] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[11] = (uint8_t)(checksum & 0xFF); // 校验和低字节
    // 调试输出帧信息
    ESP_LOGI(TAG, "发送取消操作帧: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
    // UART发送命令
    int bytes_written = 1;
    if (bytes_written == 1)
    {
        // 发送成功
        ESP_LOGI(TAG, "取消操作指令发送成功");
        return ESP_OK;
    }
    else
    {
        // 发送失败
        ESP_LOGE(TAG, "发送失败，实际发送字节数: %d", bytes_written);
        return ESP_FAIL;
    }
}

/**
 * @brief 控制模块进入休眠模式
 * @return esp_err_t 操作结果：ESP_OK=命令发送成功，ESP_FAIL=命令发送失败
 */
static esp_err_t sleep()
{
    // 构建数据帧
    uint8_t frame[12] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // 帧头(2字节)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // 设备地址(4字节)
        PACKET_CMD,                                                                                     // 包标识(1字节)
        0x00, 0x03,                                                                                     // 数据长度(2字节，固定3字节)
        CMD_SLEEP,                                                                                      // 指令码(1字节)
        0x00, 0x00                                                                                      // 校验和(2字节，待计算)
    };
    // 计算并填充校验和
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[10] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[11] = (uint8_t)(checksum & 0xFF); // 校验和低字节
    // 调试输出帧信息
    ESP_LOGI(TAG, "发送休眠指令帧: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
    // UART发送命令
    int bytes_written = 1;
    if (bytes_written == 1)
    {
        // 发送成功
        ESP_LOGI(TAG, "休眠指令发送成功");
        return ESP_OK;
    }
    else
    {
        // 发送失败
        ESP_LOGE(TAG, "发送失败，实际发送字节数: %d", bytes_written);
        return ESP_FAIL;
    }
}

/**
 * @brief 读取模块中的指纹索引表（获取已注册指纹ID）
 * @param page 页码（0-4，每页对应20枚指纹，共100枚）
 * @return esp_err_t 操作结果：ESP_OK=命令发送成功，ESP_FAIL=参数无效或命令发送失败
 */
static esp_err_t read_index_table(uint8_t page)
{
    // 参数合法性检查
    if (page > 4)
    {
        ESP_LOGE(TAG, "页码无效（需0-4，当前%u）", page);
        return ESP_FAIL;
    }
    // 构建数据帧
    uint8_t frame[13] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // 帧头(2字节)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // 设备地址(4字节)
        PACKET_CMD,                                                                                     // 包标识(1字节)
        0x00, 0x04,                                                                                     // 数据长度(2字节，固定4字节)
        CMD_READ_INDEX_TABLE,                                                                           // 指令码(1字节)
        page,                                                                                           // 页码(1字节)
        0x00, 0x00                                                                                      // 校验和(2字节，待计算)
    };
    // 计算并填充校验和
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[11] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[12] = (uint8_t)(checksum & 0xFF); // 校验和低字节
    // 调试输出帧信息
    ESP_LOGI(TAG, "读取索引表: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
    // UART发送命令
    int bytes_written = 1;
    if (bytes_written == 1)
    {
        // 发送成功
        ESP_LOGI(TAG, "读取索引表指令发送成功");
        return ESP_OK;
    }
    else
    {
        // 发送失败
        ESP_LOGE(TAG, "发送失败，实际发送字节数: %d", bytes_written);
        return ESP_FAIL;
    }
}

/**
 * @brief 解析读索引表命令的返回数据，提取已注册指纹ID
 * @param receive_data 接收的数据包缓冲区
 * @param data_length 实际接收的字节数（需显式传入）
 * @return esp_err_t 解析结果：ESP_OK=解析成功，ESP_FAIL=数据无效或解析失败
 */
static esp_err_t fingerprint_parse_frame(const uint8_t *receive_data, uint16_t data_length)
{
    // 初始化指纹ID数组（0xFF表示未使用）
    memset(zw111.fingerIDArray, 0xFF, sizeof(zw111.fingerIDArray));
    zw111.fingerNumber = 0;
    // 掩码数组（用于检测每个bit是否置位）
    uint8_t mask[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
    uint8_t tempCount = 0; // 临时计数变量
    // 解析数据区（索引表数据从第10字节开始，共13字节，对应104个bit，实际有效100个）
    for (uint8_t i = 10; i <= 22; i++) // i=10到22共13字节
    {
        uint8_t byteData = receive_data[i];
        if (byteData == 0)
            continue; // 跳过无指纹的字节
        // 检测当前字节的每个bit（对应8个指纹ID）
        for (uint8_t j = 0; j < 8; j++)
        {
            if (byteData & mask[j])
            {
                // 计算指纹ID：(字节偏移)×8 + bit偏移
                uint8_t fingerID = (i - 10) * 8 + j;
                if (fingerID < 100) // 仅保留有效ID（0-99）
                {
                    zw111.fingerIDArray[tempCount] = fingerID;
                    tempCount++;
                    if (tempCount >= 100)
                        break; // 达到最大容量则停止
                }
            }
        }
        if (tempCount >= 100)
            break; // 达到最大容量则停止
    }
    // 更新有效指纹数量
    zw111.fingerNumber = tempCount;
    if (zw111.fingerNumber > 0)
    {
        ESP_LOGI(TAG, "检测到%u个已注册指纹ID: ", zw111.fingerNumber);
        for (size_t i = 0; i < zw111.fingerNumber; i++)
        {
            ESP_LOGI(TAG, "%u ", zw111.fingerIDArray[i]);
        }
    }
    else
    {
        ESP_LOGI(TAG, "未检测到任何已注册指纹");
    }
    return ESP_OK;
}

/**
 * 在指纹列表中查找最小未使用序号并返回其序号
 * @return 最小未使用的指纹ID，无可用ID时返回255
 */
uint8_t get_mini_unused_id()
{
    // 特殊情况：没有任何指纹，直接返回0
    if (zw111.fingerNumber == 0)
    {
        return 0;
    }
    // 检查第一个ID是否为0，如果不是，说明0未被使用
    if (zw111.fingerIDArray[0] > 0)
    {
        return 0;
    }
    // 遍历已排序的ID数组，查找连续序列中的空缺
    for (uint8_t i = 0; i < zw111.fingerNumber - 1; i++)
    {
        // 当前ID和下一个ID之间存在空缺
        if (zw111.fingerIDArray[i + 1] > zw111.fingerIDArray[i] + 1)
        {
            return zw111.fingerIDArray[i] + 1;
        }
    }
    // 所有已有ID是连续的，返回最后一个ID的下一个值
    uint8_t last_id = zw111.fingerIDArray[zw111.fingerNumber - 1];
    if (last_id + 1 < 100) // 确保不超过最大支持的ID范围
    {
        return last_id + 1;
    }
    // 所有可能的ID都已使用
    return 255;
}

/**
 * 插入新注册的指纹ID到数组中，保持数组有序性
 * @param new_id 要插入的新指纹ID（应通过get_mini_unused_id()获取）
 * @return 成功插入返回ESP_OK，失败返回ESP_FAIL
 */
esp_err_t insert_fingerprint_id(uint8_t new_id)
{
    // 检查ID有效性
    if (new_id >= 100)
    {
        return ESP_FAIL; // ID无效
    }
    // 检查数组是否已满
    if (zw111.fingerNumber >= 100)
    {
        return ESP_FAIL; // 达到最大容量，无法插入
    }
    // 找到插入位置
    uint8_t insert_pos = 0;
    while (insert_pos < zw111.fingerNumber &&
           zw111.fingerIDArray[insert_pos] < new_id)
    {
        insert_pos++;
    }
    // 移动元素为新ID腾出位置
    for (uint8_t i = zw111.fingerNumber; i > insert_pos; i--)
    {
        zw111.fingerIDArray[i] = zw111.fingerIDArray[i - 1];
    }
    // 插入新ID
    zw111.fingerIDArray[insert_pos] = new_id;
    // 更新指纹数量
    zw111.fingerNumber++;
    ESP_LOGI(TAG, "插入指纹ID%u成功", zw111.fingerIDArray[insert_pos]);
    return ESP_OK; // 插入成功
}

/**
 * @brief 模块取消当前的操作并执行某条指令
 * @note 该函数会取消当前正在进行的指纹操作（如注册、识别等），并将状态设置为取消状态
 * @return void
 */
void cancel_current_operation_and_execute_command()
{
    zw111.state = 0x0A; // 切换为取消状态
    // 发送取消命令
    if (cancel() == ESP_OK)
    {
        ESP_LOGI(TAG, "准备取消当前操作，模块状态已切换为取消状态");
    }
    else
    {
        // 取消操作失败
        ESP_LOGE(TAG, "取消当前操作失败");
    }
}

int main()
{
    // 初始化指纹模块数据结构
    zw111.state = 0X00;                                             // 初始状态：刚开机
    zw111.power = false;                                            // 初始状态：断电
    memset(zw111.deviceAddress, 0xFF, sizeof(zw111.deviceAddress)); // 默认地址0xFFFFFFFF
    memset(zw111.fingerIDArray, 0xFF, sizeof(zw111.fingerIDArray)); // 未使用ID设为0xFF
    zw111.fingerNumber = 0;                                         // 初始无已注册指纹
    auto_enroll(10, 5, false, false, false, false, true, false);
    control_led(BLN_FLASH, LED_RED, LED_RED, 3);
    auto_identify(0xFFFF, 0x02, false, true, true);
    empty();
    cancel();
    delete_char(10, 1);
    sleep();
    read_index_table(0);
    // 测试用例：无效应答帧（长度错误）
    uint8_t shortFrame[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00};
    verify_received_data(shortFrame, sizeof(shortFrame) / sizeof(shortFrame[0])); // 应返回false
    // 测试用例：无效应答帧（帧头错误）
    uint8_t wrongHeaderFrame[] = {0xEF, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00, 0x03, 0x00, 0x00, 0x0A};
    verify_received_data(wrongHeaderFrame, sizeof(wrongHeaderFrame) / sizeof(wrongHeaderFrame[0])); // 应返回false
    // 测试用例：无效应答帧（设备地址错误）
    uint8_t wrongAddressFrame[] = {0xEF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x03, 0x00, 0x00, 0x0A};
    verify_received_data(wrongAddressFrame, sizeof(wrongAddressFrame) / sizeof(wrongAddressFrame[0])); // 应返回false
    // 测试用例：无效应答帧（包标识错误）
    uint8_t wrongPacketFrame[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x06, 0x00, 0x03, 0x00, 0x00, 0x0A};
    verify_received_data(wrongPacketFrame, sizeof(wrongPacketFrame) / sizeof(wrongPacketFrame[0])); // 应返回false
    // 测试用例：无效应答帧（数据长度错误）
    uint8_t wrongLengthFrame[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00, 0x02, 0x00, 0x00, 0x0A};
    verify_received_data(wrongLengthFrame, sizeof(wrongLengthFrame) / sizeof(wrongLengthFrame[0])); // 应返回false
    // 测试用例：无效应答帧（校验和错误）
    uint8_t invalidFrame[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00, 0x03, 0x00, 0x00, 0x0B};
    verify_received_data(invalidFrame, sizeof(invalidFrame) / sizeof(invalidFrame[0])); // 应返回false
    // 测试用例：有效应答帧（示例数据）
    uint8_t validFrame[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00, 0x03, 0x00, 0x00, 0x0A};
    verify_received_data(validFrame, sizeof(validFrame) / sizeof(validFrame[0])); // 应返回true
    // 其他测试用例可以继续添加...
    // 示例1：ID=0,1,2（第11字节为0x07，二进制00000111）
    uint8_t frame1[] = {
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00,
        0x23, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x31};
    uint16_t frame1_len = sizeof(frame1) / sizeof(frame1[0]);
    // 示例2：ID=0,1,2,7（第11字节为0x87，二进制10000111）
    uint8_t frame2[] = {
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00,
        0x23, 0x00, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0xB1};
    uint16_t frame2_len = sizeof(frame2) / sizeof(frame2[0]);
    // 示例3：ID=0,1,2,7,99（第11字节0x87，第23字节0x08）
    // 注：99 = (22-10)*8 + 3 → 第22索引字节（0x08，二进制1000）的第3位被置位
    uint8_t frame3[] = {
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00,
        0x23, 0x00, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0xB9};
    uint16_t frame3_len = sizeof(frame3) / sizeof(frame3[0]);
    fingerprint_parse_frame(frame1, frame1_len);
    fingerprint_parse_frame(frame2, frame2_len);
    fingerprint_parse_frame(frame3, frame3_len);
    control_colorful_led(LED_ALL, 10, 0); // 测试跑马灯控制
    return 0;
}
#include <iostream>
#include <cstdint> // 推荐使用C99标准整数类型，增强跨平台兼容性

// ========================== 通用宏定义 ==========================
// 功能码宏定义（LED控制）
#define BLN_BREATH 1   // 普通呼吸灯
#define BLN_FLASH 2    // 闪烁灯
#define BLN_ON 3       // 常开灯
#define BLN_OFF 4      // 常闭灯
#define BLN_FADE_IN 5  // 渐开灯
#define BLN_FADE_OUT 6 // 渐闭灯

// LED颜色宏定义
#define LED_OFF 0x00   // 全灭
#define LED_BLUE 0x01  // 蓝灯（bit0）
#define LED_GREEN 0x02 // 绿灯（bit1）
#define LED_RED 0x04   // 红灯（bit2）
#define LED_BG 0x03    // 蓝绿灯（bit0+bit1）
#define LED_BR 0x05    // 蓝红灯（bit0+bit2）
#define LED_GR 0x06    // 绿红灯（bit1+bit2）
#define LED_ALL 0x07   // 红绿蓝灯全亮（bit0+bit1+bit2）

// 包标识定义
#define PACKET_CMD 0x01       // 命令包
#define PACKET_DATA_MORE 0x02 // 数据包（有后续包）
#define PACKET_DATA_LAST 0x08 // 最后一个数据包（结束包）
#define PACKET_RESPONSE 0x07  // 应答包

// 指令码定义
#define CMD_AUTO_ENROLL 0x31 // 自动注册指令
#define CMD_CONTROL_BLN 0x3C // 背光灯控制指令
#define CMD_AUTO_IDENTIFY 0x32 // 自动识别指令

// 帧结构常量（避免硬编码，增强可维护性）
#define CHECKSUM_LEN 2         // 校验和长度（字节）
#define CHECKSUM_START_INDEX 6 // 校验和计算起始索引（固定，从0开始）

// 帧头和设备地址
uint8_t FRAME_HEADER[2] = { 0xEF, 0x01 };
uint8_t DEVICE_ADDRESS[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

// ========================== 通用工具函数 ==========================
/**
 * @brief 计算数据帧的校验和（累加和）
 * @param frame 数据帧缓冲区
 * @param frame_len 数据帧总长度
 * @return 计算得到的16位校验和（高字节在前）
 * @note 校验和范围：从第6字节（CHECKSUM_START_INDEX）到校验和前1字节
 */
uint16_t calculateChecksum(const uint8_t* frame, uint8_t frame_len)
{
    if (frame == nullptr || frame_len <= CHECKSUM_START_INDEX + CHECKSUM_LEN)
    {
        return 0; // 无效参数，返回0（实际应用可添加错误日志）
    }
    uint16_t checksum = 0;
    uint8_t checksum_end_index = frame_len - CHECKSUM_LEN - 1; // 校验和前1字节索引

    // 累加指定范围内的所有字节
    for (uint8_t i = CHECKSUM_START_INDEX; i <= checksum_end_index; i++)
    {
        checksum += frame[i];
    }

    return checksum;
}

// ========================== 功能函数 ==========================
/**
 * @brief 指纹模块自动注册函数
 * @param ID 指纹ID号（2字节，高字节在前）
 * @param enrollTimes 录入次数（0-5，超出范围返回失败，0和1效果相同）
 * @param ledControl 采图背光灯控制（bit0）：false=常亮；true=采图成功后熄灭
 * @param preprocess 采图预处理控制（bit1）：false=不预处理；true=开启预处理
 * @param returnStatus 注册状态返回控制（bit2）：false=返回状态；true=不返回状态
 * @param allowOverwrite ID覆盖控制（bit3）：false=不允许覆盖；true=允许覆盖
 * @param allowDuplicate 重复注册控制（bit4）：false=允许重复；true=禁止重复
 * @param requireRemove 手指离开要求（bit5）：false=需离开；true=无需离开
 * @return 操作是否成功（参数有效且帧组装成功返回true）
 */
bool PS_AutoEnroll(uint16_t ID, uint8_t enrollTimes,
    bool ledControl, bool preprocess,
    bool returnStatus, bool allowOverwrite,
    bool allowDuplicate, bool requireRemove)
{
    // 参数合法性检查
    if (enrollTimes > 5)
    {
        printf("错误: 录入次数必须在0-5之间\n");
        return false;
    }

    // 组装参数（param，bit0-bit5）
    uint16_t param = 0;
    param |= (ledControl ? 1 << 0 : 0);     // bit0: 背光灯控制
    param |= (preprocess ? 1 << 1 : 0);     // bit1: 预处理控制
    param |= (returnStatus ? 1 << 2 : 0);   // bit2: 状态返回控制
    param |= (allowOverwrite ? 1 << 3 : 0); // bit3: ID覆盖控制
    param |= (allowDuplicate ? 1 << 4 : 0); // bit4: 重复注册控制
    param |= (requireRemove ? 1 << 5 : 0);  // bit5: 手指离开控制

    // 计算数据帧长度：帧头(6) + 命令码(2) + 数据长度和指令(2) + ID(2) + 录入次数(1) + 参数(2) + 校验和(2) = 17
    uint8_t frame[17] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                           // 包头(2字节)
        DEVICE_ADDRESS[0], DEVICE_ADDRESS[1], DEVICE_ADDRESS[2], DEVICE_ADDRESS[3], // 设备地址(4字节)
        PACKET_CMD,                                                                 // 包标识(1字节)
        0x00, 0x08,                                                                 // 数据长度(2字节)
        CMD_AUTO_ENROLL,                                                            // 指令(1字节)
        (uint8_t)(ID >> 8), (uint8_t)ID,                                            // ID(高字节在前)(2字节)
        enrollTimes,                                                                // 录入次数(1字节)
        (uint8_t)(param >> 8), (uint8_t)param,                                      // 参数(param)，高字节在前(2字节)
        0x00, 0x00                                                                  // 校验和(2字节)将在后面计算
    };

    // 计算并填充校验和（使用通用函数）
    uint16_t checksum = calculateChecksum(frame, 17);
    frame[15] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[16] = (uint8_t)(checksum & 0xFF); // 校验和低字节

    // 调试输出（格式化显示）
    printf("发送自动注册帧: ");
    for (uint8_t i = 0; i < 17; i++)
    {
        printf("%02X ", frame[i]);
    }
    printf("\n");

    // 实际应用中添加帧发送逻辑（如UART发送）
    // return UART_Send(frame, frame_len);

    return true;
}
/**
 * @brief 指纹模块自动识别函数
 * @param ID 指纹ID号（2字节，高字节在前）
 *           - 具体数值（如1对应0x0001）：验证指定ID的指纹
 *           - 0xFFFF：验证所有已注册的指纹
 * @param scoreLevel 分数等级，系统根据该值设定比对阀值（1-28,默认为0x12）
 * @param ledControl 采图背光灯控制（bit0）：false=常亮；true=采图成功后熄灭
 * @param preprocess 采图预处理控制（bit1）：false=不预处理；true=开启预处理
 * @param returnStatus 识别状态返回控制（bit2）：false=返回状态；true=不返回状态
 * @return 操作是否成功（参数有效且帧组装成功返回true）
 */
bool PS_Autoldentify(uint16_t ID, uint8_t scoreLevel, bool ledControl, bool preprocess, bool returnStatus)
{
    // 组装参数（PR，bit0-bit2）
    uint16_t param = 0;
    param |= (ledControl ? 1 << 0 : 0);     // bit0: 背光灯控制
    param |= (preprocess ? 1 << 1 : 0);     // bit1: 预处理控制
    param |= (returnStatus ? 1 << 2 : 0);   // bit2: 状态返回控制

    // 构建数据帧（共15字节）
    uint8_t frame[17] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                           // 包头(2字节)
        DEVICE_ADDRESS[0], DEVICE_ADDRESS[1], DEVICE_ADDRESS[2], DEVICE_ADDRESS[3], // 设备地址(4字节)
        PACKET_CMD,                                                                 // 包标识(1字节，SC=命令包)
        0x00, 0x08,                                                                 // 数据长度(2字节)
        CMD_AUTO_IDENTIFY,                                                          // 指令码(PS_Autoldentify)
		scoreLevel,                                                                 // 分数等级(1字节，0x12为默认值)
        (uint8_t)(ID >> 8), (uint8_t)ID,                                            // ID(高字节在前)(2字节)
        (uint8_t)(param >> 8), (uint8_t)param,                                      // 参数(PR)，高字节在前(2字节)
        0x00, 0x00                                                                  // 校验和(2字节)将在后面计算
    };

    // 计算并填充校验和
    uint16_t checksum = calculateChecksum(frame, 15);
    frame[15] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[16] = (uint8_t)(checksum & 0xFF); // 校验和低字节

    // 调试输出
    printf("发送自动识别帧: ");
    for (uint8_t i = 0; i < 17; i++)
    {
        printf("%02X ", frame[i]);
    }
    printf("\n");

    // 实际应用中添加帧发送逻辑
    // return UART_Send(frame, 15);

    return true;
}
/**
 * @brief 指纹模块LED控制函数
 * @param functionCode 功能码（参考BLN_xxx宏定义，1-6有效）
 * @param startColor 起始颜色（bit0-蓝，bit1-绿，bit2-红，0x00-全灭，0x07-全亮）
 * @param endColor 结束颜色（仅功能码1-普通呼吸灯有效，其他功能无效）
 * @param cycleTimes 循环次数（仅功能码1-呼吸灯/2-闪烁灯有效，0=无限循环）
 * @return 操作是否成功（参数有效且帧组装成功返回true）
 */
bool PS_ControlBLN(uint8_t functionCode, uint8_t startColor,
    uint8_t endColor, uint8_t cycleTimes)
{
    // 参数合法性检查
    if (functionCode < BLN_BREATH || functionCode > BLN_FADE_OUT)
    {
        printf("错误: 功能码必须在1-6之间（参考BLN_xxx宏定义）\n");
        return false;
    }

    // 过滤颜色参数的无效位（仅保留低3位）
    if ((startColor & 0xF8) != 0)
    {
        printf("警告: 起始颜色仅低3位有效，已自动过滤\n");
        startColor &= 0x07;
    }
    if ((endColor & 0xF8) != 0)
    {
        printf("警告: 结束颜色仅低3位有效，已自动过滤\n");
        endColor &= 0x07;
    }

    uint8_t frame[16] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                           // 包头(2字节)
        DEVICE_ADDRESS[0], DEVICE_ADDRESS[1], DEVICE_ADDRESS[2], DEVICE_ADDRESS[3], // 设备地址(4字节)
        PACKET_CMD,                                                                 // 包标识(1字节)
        0x00, 0x07,                                                                 // 数据长度(2字节)
        CMD_CONTROL_BLN,                                                            // 指令(1字节)
        functionCode,                                                               // 功能码FC(1字节)
        startColor,                                                                 // 起始颜色ST(1字节)
        endColor,                                                                   // 结束颜色ED(1字节)
        cycleTimes,                                                                 // 循环次数TS(1字节)
        0x00, 0x00                                                                  // 校验和(2字节)将在后面计算
    };

    // 计算并填充校验和（调用通用函数）
    uint16_t checksum = calculateChecksum(frame, 16);
    frame[14] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[15] = (uint8_t)(checksum & 0xFF); // 校验和低字节

    // 调试输出（格式化显示）
    printf("发送LED控制帧: ");
    for (uint8_t i = 0; i < 16; i++)
    {
        printf("%02X ", frame[i]);
    }
    printf("\n");

    // 实际应用中添加帧发送逻辑（如UART发送）
    // return UART_Send(frame, frame_len);

    return true;
}

int main()
{
    // 测试用例
    PS_AutoEnroll(10, 5, false, false, false, true, false, false);
    PS_ControlBLN(BLN_FLASH, LED_ALL, LED_ALL, 3);
	PS_Autoldentify(0xFFFF, 0x12, false, false, false);
    return 0;
}
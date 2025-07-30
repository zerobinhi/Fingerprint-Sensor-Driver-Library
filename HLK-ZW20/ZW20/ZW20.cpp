#include <stdio.h>
#include <string.h>
#include <cstdint> // 推荐使用C99标准整数类型，增强跨平台兼容性
#define esp_err_t bool

// ========================== 通用宏定义 ==========================
// 功能码宏定义（LED控制）
#define BLN_BREATH 1   // 普通呼吸灯
#define BLN_FLASH 2    // 闪烁灯
#define BLN_ON 3       // 常开灯
#define BLN_OFF 4      // 常闭灯
#define BLN_FADE_IN 5  // 渐开灯
#define BLN_FADE_OUT 6 // 渐闭灯
#define BLN_COLORFUL 7 // 七彩灯

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
#define CMD_AUTO_ENROLL 0x31      // 自动注册指令
#define CMD_CONTROL_BLN 0x3C      // 背光灯控制指令
#define CMD_AUTO_IDENTIFY 0x32    // 自动识别指令
#define CMD_DELET_CHAR 0x0C       // 删除指纹指令
#define CMD_EMPTY 0x0D            // 清空指纹指令
#define CMD_HAND_SHAKE 0x35       // 握手指令（可选）
#define CMD_CANCEL 0x30           // 取消指令
#define CMD_READ_INDEX_TABLE 0x1F // 读索引表指令
#define CMD_SLEEP 0x33            // 休眠指令

// 帧结构常量（避免硬编码，增强可维护性）
#define CHECKSUM_LEN 2         // 校验和长度（字节）
#define CHECKSUM_START_INDEX 6 // 校验和计算起始索引（固定，从0开始）

const uint8_t FRAME_HEADER[2] = { 0xEF, 0x01 };          // 帧头
uint8_t g_deviceAddress[4] = { 0xFF, 0xFF, 0xFF, 0xFF }; // 设备地址
uint8_t g_fingerIDArray[100] = { 0xFF };                 // 指纹模块最大容量为100枚指纹
uint8_t g_fingerNumber = 0;                            // 有效指纹数量

// ========================== 通用工具函数 ==========================
/**
 * @brief 校验指纹模块接收数据的有效性（重点验证校验和）
 * @param recvData 接收的数据包缓冲区
 * @param dataLen 实际接收的字节数（必须显式传入，不能用strlen计算）
 * @return 校验结果：true=有效数据，false=无效数据
 */
esp_err_t verify_received_data(const uint8_t* recvData, uint16_t dataLen)
{
	// 基础合法性检查
	if (recvData == nullptr || dataLen < 12) // 最小应答帧长度为12字节
	{
		printf("校验失败：数据为空或长度不足, 最小长度为12字节，当前长度=%d\n", dataLen);
		return false;
	}

	// 验证帧头
	if (recvData[0] != FRAME_HEADER[0] || recvData[1] != FRAME_HEADER[1])
	{
		printf("校验失败：帧头不正确, 应为%02X%02X，实际为%02X%02X\n", FRAME_HEADER[0], FRAME_HEADER[1], recvData[0], recvData[1]);
		return false;
	}
	// 验证设备地址
	for (int i = 2; i < 6; i++)
	{
		if (recvData[i] != g_deviceAddress[i - 2])
		{
			printf("校验失败：设备地址不匹配, 应为%02X%02X%02X%02X，实际为%02X%02X%02X%02X\n",
				g_deviceAddress[0], g_deviceAddress[1], g_deviceAddress[2], g_deviceAddress[3],
				recvData[2], recvData[3], recvData[4], recvData[5]);
			return false;
		}
	}
	// 验证应答包
	if (recvData[6] != PACKET_RESPONSE)
	{
		printf("校验失败：包标识不正确，应为%02X，实际为%02X\n", PACKET_RESPONSE, recvData[6]);
		return false;
	}
	// 验证长度
	uint16_t expectedDataLen = (recvData[7] << 8) | recvData[8]; // 数据长度（高字节在前）
	if (expectedDataLen + 9 != dataLen)                          // 包头(2) + 设备地址(4) + 包标识(1) + 数据长度(2) + 校验和(2)
	{
		printf("校验失败：数据长度不匹配（期望=%d，实际=%d）\n", expectedDataLen + 9, dataLen);
		return false;
	}

	// 提取校验和（最后2字节，高字节在前）
	uint16_t receivedChecksum = (recvData[dataLen - 2] << 8) | recvData[dataLen - 1];

	// 计算校验范围数据的累加和（包标识+数据长度+指令结果）
	// 校验范围：从索引6（包标识）到索引dataLen-3（校验和前1字节）
	uint16_t calculatedSum = 0;
	for (uint16_t i = 6; i <= dataLen - 3; i++)
	{
		calculatedSum += recvData[i];
	}

	// 对比校验结果
	if (calculatedSum == receivedChecksum)
	{
		printf("校验成功：校验和匹配\n");
		return true;
	}
	else
	{
		printf("校验失败：校验和不匹配\n");
		return false;
	}
}
/**
 * @brief 计算数据帧的校验和（累加和）
 * @param recvData 数据帧缓冲区
 * @param dataLen 数据帧总长度
 * @return 计算得到的16位校验和（高字节在前）
 * @note 校验和范围：从第6字节（CHECKSUM_START_INDEX）到校验和前1字节
 */
uint16_t calculate_checksum(const uint8_t* recvData, uint16_t dataLen)
{
	if (recvData == nullptr || dataLen <= CHECKSUM_START_INDEX + CHECKSUM_LEN)
	{
		return 0; // 无效参数，返回0（实际应用可添加错误日志）
	}
	uint16_t checksum = 0;
	uint8_t checksum_end_index = dataLen - CHECKSUM_LEN - 1; // 校验和前1字节索引

	// 累加指定范围内的所有字节
	for (uint8_t i = CHECKSUM_START_INDEX; i <= checksum_end_index; i++)
	{
		checksum += recvData[i];
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
esp_err_t auto_enroll(uint16_t ID, uint8_t enrollTimes,
	bool ledControl, bool preprocess,
	bool returnStatus, bool allowOverwrite,
	bool allowDuplicate, bool requireRemove)
{
	// 参数合法性检查
	if (ID >= 100)
	{
		printf("错误: 指纹ID号必须在0-99之间\n");
		return false;
	}
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
		FRAME_HEADER[0], FRAME_HEADER[1],                                               // 包头(2字节)
		g_deviceAddress[0], g_deviceAddress[1], g_deviceAddress[2], g_deviceAddress[3], // 设备地址(4字节)
		PACKET_CMD,                                                                     // 包标识(1字节)
		0x00, 0x08,                                                                     // 数据长度(2字节)
		CMD_AUTO_ENROLL,                                                                // 指令(1字节)
		(uint8_t)(ID >> 8), (uint8_t)ID,                                                // ID(高字节在前)(2字节)
		enrollTimes,                                                                    // 录入次数(1字节)
		(uint8_t)(param >> 8), (uint8_t)param,                                          // 参数(param)，高字节在前(2字节)
		0x00, 0x00                                                                      // 校验和(2字节)将在后面计算
	};

	// 计算并填充校验和（使用通用函数）
	uint16_t checksum = calculate_checksum(frame, sizeof(frame));
	frame[15] = (uint8_t)(checksum >> 8);   // 校验和高字节
	frame[16] = (uint8_t)(checksum & 0xFF); // 校验和低字节

	// 调试输出（格式化显示）
	printf("发送自动注册帧: ");
	for (uint8_t i = 0; i < sizeof(frame); i++)
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
esp_err_t auto_identify(uint16_t ID, uint8_t scoreLevel, bool ledControl, bool preprocess, bool returnStatus)
{
	// 组装参数（PR，bit0-bit2）
	uint16_t param = 0;
	param |= (ledControl ? 1 << 0 : 0);   // bit0: 背光灯控制
	param |= (preprocess ? 1 << 1 : 0);   // bit1: 预处理控制
	param |= (returnStatus ? 1 << 2 : 0); // bit2: 状态返回控制

	// 构建数据帧（共15字节）
	uint8_t frame[17] = {
		FRAME_HEADER[0], FRAME_HEADER[1],                                               // 包头(2字节)
		g_deviceAddress[0], g_deviceAddress[1], g_deviceAddress[2], g_deviceAddress[3], // 设备地址(4字节)
		PACKET_CMD,                                                                     // 包标识(1字节，SC=命令包)
		0x00, 0x08,                                                                     // 数据长度(2字节)
		CMD_AUTO_IDENTIFY,                                                              // 指令码(PS_Autoldentify)
		scoreLevel,                                                                     // 分数等级(1字节，0x12为默认值)
		(uint8_t)(ID >> 8), (uint8_t)ID,                                                // ID(高字节在前)(2字节)
		(uint8_t)(param >> 8), (uint8_t)param,                                          // 参数(PR)，高字节在前(2字节)
		0x00, 0x00                                                                      // 校验和(2字节)将在后面计算
	};

	// 计算并填充校验和
	uint16_t checksum = calculate_checksum(frame, sizeof(frame));
	frame[15] = (uint8_t)(checksum >> 8);   // 校验和高字节
	frame[16] = (uint8_t)(checksum & 0xFF); // 校验和低字节

	// 调试输出
	printf("发送自动识别帧: ");
	for (uint8_t i = 0; i < sizeof(frame); i++)
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
 * @param functionCode 功能码（参考BLN_xxx宏定义，1-7有效）
 * @param startColor 起始颜色（bit0-蓝，bit1-绿，bit2-红，0x00-全灭，0x07-全亮）
 * @param endColor 结束颜色（仅功能码1-普通呼吸灯有效，其他功能无效）
 * @param cycleTimes 循环次数（仅功能码1-呼吸灯/2-闪烁灯有效，0=无限循环）
 * @return 操作是否成功（参数有效且帧组装成功返回true）
 */
esp_err_t control_led(uint8_t functionCode, uint8_t startColor,
	uint8_t endColor, uint8_t cycleTimes)
{
	// 参数合法性检查
	if (functionCode < BLN_BREATH || functionCode > BLN_COLORFUL)
	{
		printf("错误: 功能码必须在1-7之间（参考BLN_xxx宏定义）\n");
		return false;
	}
	if (functionCode != BLN_COLORFUL)
	{
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
			FRAME_HEADER[0], FRAME_HEADER[1],                                               // 包头(2字节)
			g_deviceAddress[0], g_deviceAddress[1], g_deviceAddress[2], g_deviceAddress[3], // 设备地址(4字节)
			PACKET_CMD,                                                                     // 包标识(1字节)
			0x00, 0x07,                                                                     // 数据长度(2字节)
			CMD_CONTROL_BLN,                                                                // 指令(1字节)
			functionCode,                                                                   // 功能码FC(1字节)
			startColor,                                                                     // 起始颜色ST(1字节)
			endColor,                                                                       // 结束颜色ED(1字节)
			cycleTimes,                                                                     // 循环次数TS(1字节)
			0x00, 0x00                                                                      // 校验和(2字节)将在后面计算
		};

		// 计算并填充校验和（调用通用函数）
		uint16_t checksum = calculate_checksum(frame, sizeof(frame));
		frame[14] = (uint8_t)(checksum >> 8);   // 校验和高字节
		frame[15] = (uint8_t)(checksum & 0xFF); // 校验和低字节

		// 调试输出（格式化显示）
		printf("发送LED控制帧: ");
		for (uint8_t i = 0; i < sizeof(frame); i++)
		{
			printf("%02X ", frame[i]);
		}
		printf("\n");
	}
	else
	{

	}
	// 实际应用中添加帧发送逻辑（如UART发送）
	// return UART_Send(frame, frame_len);

	return true;
}
/**
 * @brief 删除一定数量的指纹
 * @param ID：指纹号
 * @param count：删除数量
 * @return 操作是否成功（参数有效且帧组装成功返回true）
 */
esp_err_t delet_char(uint16_t ID, uint16_t count)
{
	// 参数合法性检查
	if (ID >= 100)
	{
		printf("错误: 指纹ID号必须在0-99之间\n");
		return false;
	}
	if (count == 0 || count > 5)
	{
		printf("错误: 删除数量必须在1-100之间\n");
		return false;
	}

	uint8_t frame[16] = {
		FRAME_HEADER[0], FRAME_HEADER[1],                                               // 包头(2字节)
		g_deviceAddress[0], g_deviceAddress[1], g_deviceAddress[2], g_deviceAddress[3], // 设备地址(4字节)
		PACKET_CMD,                                                                     // 包标识(1字节)
		0x00, 0x07,                                                                     // 数据长度(2字节)
		CMD_DELET_CHAR,                                                                 // 指令(1字节)
		(uint8_t)(ID >> 8), (uint8_t)ID,                                                // ID(高字节在前)(2字节)
		(uint8_t)(count >> 8), (uint8_t)count,                                          // 删除数量(2字节)
		0x00, 0x00                                                                      // 校验和(2字节)将在后面计算
	};

	// 计算并填充校验和（调用通用函数）
	uint16_t checksum = calculate_checksum(frame, sizeof(frame));
	frame[14] = (uint8_t)(checksum >> 8);   // 校验和高字节
	frame[15] = (uint8_t)(checksum & 0xFF); // 校验和低字节

	// 调试输出（格式化显示）
	printf("删除指纹: ");
	for (uint8_t i = 0; i < sizeof(frame); i++)
	{
		printf("%02X ", frame[i]);
	}
	printf("\n");

	// 实际应用中添加帧发送逻辑（如UART发送）
	// return UART_Send(frame, frame_len);

	return true;
}

/**
 * @brief 清空所有指纹
 * @param 无参数
 * @return 操作是否成功（参数有效且帧组装成功返回true）
 */
esp_err_t empty()
{
	uint8_t frame[12] = {
		FRAME_HEADER[0], FRAME_HEADER[1],                                               // 包头(2字节)
		g_deviceAddress[0], g_deviceAddress[1], g_deviceAddress[2], g_deviceAddress[3], // 设备地址(4字节)
		PACKET_CMD,                                                                     // 包标识(1字节)
		0x00, 0x03,                                                                     // 数据长度(2字节)
		CMD_EMPTY,                                                                      // 指令(1字节)
		0x00, 0x00                                                                      // 校验和(2字节)将在后面计算
	};

	// 计算并填充校验和（调用通用函数）
	uint16_t checksum = calculate_checksum(frame, sizeof(frame));
	frame[10] = (uint8_t)(checksum >> 8);   // 校验和高字节
	frame[11] = (uint8_t)(checksum & 0xFF); // 校验和低字节

	// 调试输出（格式化显示）
	printf("清空所有指纹控制帧: ");
	for (uint8_t i = 0; i < sizeof(frame); i++)
	{
		printf("%02X ", frame[i]);
	}
	printf("\n");

	return true;
}

/**
 * @brief 取消指令
 * @param 无参数
 * @return 操作是否成功（参数有效且帧组装成功返回true）
 */
esp_err_t cancel()
{
	uint8_t frame[12] = {
		FRAME_HEADER[0], FRAME_HEADER[1],                                               // 包头(2字节)
		g_deviceAddress[0], g_deviceAddress[1], g_deviceAddress[2], g_deviceAddress[3], // 设备地址(4字节)
		PACKET_CMD,                                                                     // 包标识(1字节)
		0x00, 0x03,                                                                     // 数据长度(2字节)
		CMD_CANCEL,                                                                     // 指令(1字节)
		0x00, 0x00                                                                      // 校验和(2字节)将在后面计算
	};

	// 计算并填充校验和（调用通用函数）
	uint16_t checksum = calculate_checksum(frame, sizeof(frame));
	frame[10] = (uint8_t)(checksum >> 8);   // 校验和高字节
	frame[11] = (uint8_t)(checksum & 0xFF); // 校验和低字节

	// 调试输出（格式化显示）
	printf("取消指令: ");
	for (uint8_t i = 0; i < sizeof(frame); i++)
	{
		printf("%02X ", frame[i]);
	}
	printf("\n");

	return true;
}
/**
 * @brief 休眠指令
 * @param 无参数
 * @return 操作是否成功（参数有效且帧组装成功返回true）
 */
esp_err_t sleep()
{
	uint8_t frame[12] = {
		FRAME_HEADER[0], FRAME_HEADER[1],                                               // 包头(2字节)
		g_deviceAddress[0], g_deviceAddress[1], g_deviceAddress[2], g_deviceAddress[3], // 设备地址(4字节)
		PACKET_CMD,                                                                     // 包标识(1字节)
		0x00, 0x03,                                                                     // 数据长度(2字节)
		CMD_SLEEP,                                                                      // 指令(1字节)
		0x00, 0x00                                                                      // 校验和(2字节)将在后面计算
	};

	// 计算并填充校验和（调用通用函数）
	uint16_t checksum = calculate_checksum(frame, sizeof(frame));
	frame[10] = (uint8_t)(checksum >> 8);   // 校验和高字节
	frame[11] = (uint8_t)(checksum & 0xFF); // 校验和低字节

	// 调试输出（格式化显示）
	printf("休眠指令: ");
	for (uint8_t i = 0; i < sizeof(frame); i++)
	{
		printf("%02X ", frame[i]);
	}
	printf("\n");

	return true;
}
/**
 * @brief 读索引表
 * @param page 页码（0-4）
 * @return 操作是否成功（参数有效且帧组装成功返回true）
 */
esp_err_t read_index_table(uint8_t page)
{
	uint8_t frame[13] = {
		FRAME_HEADER[0], FRAME_HEADER[1],                                               // 包头(2字节)
		g_deviceAddress[0], g_deviceAddress[1], g_deviceAddress[2], g_deviceAddress[3], // 设备地址(4字节)
		PACKET_CMD,                                                                     // 包标识(1字节)
		0x00, 0x04,                                                                     // 数据长度(2字节)
		CMD_READ_INDEX_TABLE,                                                           // 指令(1字节)
		page,                                                                           // 页码(1字节)
		0x00, 0x00                                                                      // 校验和(2字节)将在后面计算
	};

	// 计算并填充校验和（调用通用函数）
	uint16_t checksum = calculate_checksum(frame, sizeof(frame));
	frame[11] = (uint8_t)(checksum >> 8);   // 校验和高字节
	frame[12] = (uint8_t)(checksum & 0xFF); // 校验和低字节

	// 调试输出（格式化显示）
	printf("读索引表指令: ");
	for (uint8_t i = 0; i < sizeof(frame); i++)
	{
		printf("%02X ", frame[i]);
	}
	printf("\n");

	return true;
}

/**
 * @brief 解析读索引表命令返回的数据帧，提取指纹ID信息
 * @param recvData 接收的数据包缓冲区
 * @param dataLen 实际接收的字节数（必须显式传入，不能用strlen计算）
 * @return 操作是否成功（参数有效且解析成功返回ESP_OK，否则返回对应错误码）
 */
esp_err_t fingerprint_parse_frame(const uint8_t* recvData, uint16_t dataLen)
{
	if (!verify_received_data(recvData, dataLen))
	{
		return false; // 保持你的返回值风格
	}

	memset(g_fingerIDArray, 0xFF, sizeof(g_fingerIDArray));
	g_fingerNumber = 0;

	// 优化的解析部分
	uint8_t mask[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
	uint8_t temp_num = 0; // 局部变量暂存计数

	for (uint8_t i = 10; i <= 22; i++)
	{
		uint8_t byte = recvData[i];
		if (byte == 0)
			continue; // 跳过全0字节

		for (uint8_t j = 0; j < 8; j++)
		{
			if (byte & mask[j])
			{
				g_fingerIDArray[temp_num] = (i - 10) * 8 + j;
				temp_num++;
				if (temp_num >= 100)
					goto end_parse; // 满了直接跳出
			}
		}
	}
end_parse:
	g_fingerNumber = temp_num; // 同步计数

	// 打印部分不变
	if (g_fingerNumber > 0)
	{
		printf("检测到%d个指纹ID: ", g_fingerNumber);
		for (size_t i = 0; i < g_fingerNumber; i++)
		{
			printf("%d ", g_fingerIDArray[i]);
		}
		printf("\n");
	}
	else
	{
		printf("未检测到任何指纹\n");
	}

	return true;
}
int main()
{
#if 1
	auto_enroll(10, 5, false, false, false, true, false, false);
	control_led(BLN_FLASH, LED_ALL, LED_ALL, 3);
	auto_identify(0xFFFF, 0x12, false, false, false);
	empty();
	cancel();
	delet_char(11, 3);
	sleep();
	read_index_table(0);

	// 测试用例：无效应答帧（长度错误）
	uint8_t shortFrame[] = { 0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00 };
	verify_received_data(shortFrame, sizeof(shortFrame) / sizeof(shortFrame[0])); // 应返回false
	// 测试用例：无效应答帧（帧头错误）
	uint8_t wrongHeaderFrame[] = { 0xEF, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00, 0x03, 0x00, 0x00, 0x0A };
	verify_received_data(wrongHeaderFrame, sizeof(wrongHeaderFrame) / sizeof(wrongHeaderFrame[0])); // 应返回false
	// 测试用例：无效应答帧（设备地址错误）
	uint8_t wrongAddressFrame[] = { 0xEF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x03, 0x00, 0x00, 0x0A };
	verify_received_data(wrongAddressFrame, sizeof(wrongAddressFrame) / sizeof(wrongAddressFrame[0])); // 应返回false
	// 测试用例：无效应答帧（包标识错误）
	uint8_t wrongPacketFrame[] = { 0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x06, 0x00, 0x03, 0x00, 0x00, 0x0A };
	verify_received_data(wrongPacketFrame, sizeof(wrongPacketFrame) / sizeof(wrongPacketFrame[0])); // 应返回false
	// 测试用例：无效应答帧（数据长度错误）
	uint8_t wrongLengthFrame[] = { 0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00, 0x02, 0x00, 0x00, 0x0A };
	verify_received_data(wrongLengthFrame, sizeof(wrongLengthFrame) / sizeof(wrongLengthFrame[0])); // 应返回false
	// 测试用例：无效应答帧（校验和错误）
	uint8_t invalidFrame[] = { 0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00, 0x03, 0x00, 0x00, 0x0B };
	verify_received_data(invalidFrame, sizeof(invalidFrame) / sizeof(invalidFrame[0])); // 应返回false
	// 测试用例：有效应答帧（示例数据）
	uint8_t validFrame[] = { 0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00, 0x03, 0x00, 0x00, 0x0A };
	verify_received_data(validFrame, sizeof(validFrame) / sizeof(validFrame[0])); // 应返回true
	// 其他测试用例可以继续添加...
#else
	// 示例1：ID=0,1,2（第11字节为0x07，二进制00000111）
	uint8_t frame1[] = {
		0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00,
		0x23, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x31 };
	uint16_t frame1_len = sizeof(frame1) / sizeof(frame1[0]);

	// 示例2：ID=0,1,2,7（第11字节为0x87，二进制10000111）
	uint8_t frame2[] = {
		0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00,
		0x23, 0x00, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0xB1 };
	uint16_t frame2_len = sizeof(frame2) / sizeof(frame2[0]);

	// 示例3：ID=0,1,2,7,99（第11字节0x87，第23字节0x08）
	// 注：99 = (22-10)*8 + 3 → 第22索引字节（0x08，二进制1000）的第3位被置位
	uint8_t frame3[] = {
		0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00,
		0x23, 0x00, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0xB9 };
	uint16_t frame3_len = sizeof(frame3) / sizeof(frame3[0]);
	fingerprint_parse_frame(frame1, frame1_len);
	fingerprint_parse_frame(frame2, frame2_len);
	fingerprint_parse_frame(frame3, frame3_len);

#endif
	return 0;
}
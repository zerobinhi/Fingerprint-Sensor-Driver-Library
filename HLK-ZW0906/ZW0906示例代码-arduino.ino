#include <SoftwareSerial.h>

//注意：指纹ZW0906的VDD_3.3V需要单独供电，如用串口工具，不能用arduino板子供电.

// 定义指纹模组的UART引脚
#define FINGERPRINT_RX 3
#define FINGERPRINT_TX 2

// 创建软串口对象
SoftwareSerial fingerprintSerial(FINGERPRINT_RX, FINGERPRINT_TX);

// 定义指令包格式
const uint8_t HEADER_HIGH = 0xEF;
const uint8_t HEADER_LOW = 0x01;
const uint32_t DEVICE_ADDRESS = 0xFFFFFFFF;

// 定义指令码
const uint8_t CMD_GET_IMAGE = 0x01;  // 获取图像
const uint8_t CMD_GEN_CHAR = 0x02;   // 生成特征
const uint8_t CMD_MATCH = 0x03;   // 精确比对指纹
const uint8_t CMD_SEARCH = 0x04;   // 搜索指纹
const uint8_t CMD_REG_MODEL = 0x05;  // 合并特征
const uint8_t CMD_STORE_CHAR = 0x06; // 存储模板
const uint8_t CMD_CLEAR_LIB = 0x0D;   // 清空指纹库
const uint8_t CMD_READ_SYSPARA = 0x0F; // 读模组基本参数

// 定义缓冲区ID
uint8_t BUFFER_ID = 0;

// 定义模板存储位置
const uint16_t TEMPLATE_ID = 1;

// 全局变量
bool isInit = false;

// 函数声明
void command_use(void);
int read_FP_info(void);
void sendCommand(uint8_t cmd, uint8_t param1 = 0, uint16_t param2 = 0);
void sendCommand1(uint8_t cmd, uint8_t param1, uint16_t param2, uint16_t param3);
bool receiveResponse();
void printResponse(uint8_t *response, uint8_t length);

void printHex(uint8_t* data, uint8_t len) {
  for(uint8_t i=0; i<len; i++){
    if(data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
}

void setup() {
  // 初始化串口
  Serial.begin(57600);
  fingerprintSerial.begin(57600);
}

void loop() {
  if(!isInit) { //上电打印模组基本参数
    isInit = true;
    read_FP_info();

    //命令使用说明
    command_use();
  }

  //接收指令
  run_command();

  delay(100);
}

void command_use(void)
{
  Serial.println("------------------------------------------");
  Serial.println("command input:");
  Serial.println("\"1\" means \"register fingerprint.\"");
  Serial.println("\"2\" means \"search fingerprint.\"");
  Serial.println("\"3\" means \"clear fingerprint.\"");
  Serial.println("------------------------------------------");
}

int run_command(void)
{
  // 当串口有数据可读时
  if (Serial.available() > 0) {
    // 读取直到换行符（自动处理回车+换行）
    String command = Serial.readStringUntil('\n');
    command.trim();  // 去除首尾空白字符（包括换行和回车）

    // 判断命令并执行对应操作
     if (command == "1") {
      Serial.println("[INFO] registering...");
      if(register_FP()) {
        Serial.println("FP registration process complete!");
      }
      else {
        Serial.println("FP registration process fail! Please 'register' again!!!");
      }
    } 
    else if (command == "2") {
      Serial.println("[INFO] searching...");
      if(search_FP()) {
        Serial.println("FP match succ!");
      }
      else {
        Serial.println("FP match fail! Please 'search' again!!!");
      }
    }
    else if (command == "3") {
      Serial.println("[INFO] clearing...");
      if(clear_FP_all_lib()) {
        Serial.println("FP clear FP all lib succ!");
      }
      else {
        Serial.println("FP clear FP all lib fail! Please 'clear' again!!!");
      }
    } else {
      // 未知命令处理
      Serial.print("[ERROR] unknown cmd: ");
      Serial.println(command);
    }
  }
}

//读模组基本参数
int read_FP_info(void)
{
  uint8_t response[32];
  uint8_t index = 0;
  uint32_t startTime = millis();

  Serial.println("FPM info:");
  send_cmd(CMD_READ_SYSPARA);
  
  // 等待响应包
  while (millis() - startTime < 2000) {
    if (fingerprintSerial.available()) {
      response[index++] = fingerprintSerial.read();
      if (index >= 28) break;
    }
  }

  // 打印响应包
  // printResponse(response, index);

  // 检查确认码
  if (index >= 28 && response[9] == 0x00) {
    u16 register_cnt = (u16)(response[10]<<8) | response[11];
    u16 fp_temp_size = (u16)(response[12]<<8) | response[13];
    u16 fp_lib_size  = (u16)(response[14]<<8) | response[15];
    u16 score_level  = (u16)(response[16]<<8) | response[17];
    u32 device_addr  = (u32)(response[18]<<24) | (response[19]<<16)| (response[20]<<8) | response[21];
    u16 data_pack_size = (u16)(response[22]<<8) | response[23];
    if(0 == data_pack_size) {
      data_pack_size = 32;
    }
    else if(1 == data_pack_size) {
      data_pack_size = 64;
    }
    else if(2 == data_pack_size) {
      data_pack_size = 128;
    }
    else if(3 == data_pack_size) {
      data_pack_size = 256;
    }
    u16 baud_set = (u16)(response[24]<<8) | response[25];
    
    Serial.print("register cnt:");
    Serial.println(register_cnt);
    Serial.print("temp size:0x");
    Serial.println(fp_temp_size,HEX);
    Serial.print("lib size:");
    Serial.println(fp_lib_size);
    Serial.print("level:");
    Serial.println(score_level);
    Serial.print("devece address:0x");
    Serial.println(device_addr,HEX);
    Serial.print("data size:");
    Serial.println(data_pack_size);
    Serial.print("baud:");
    Serial.println(baud_set*9600);
    return 1;  // 成功
  } else {
    return 0; // 失败
  }
}

//注册指纹
int register_FP(void)
{
  BUFFER_ID = 1;
  while(BUFFER_ID <= 5) {
    // 步骤1：获取图像
    send_cmd(CMD_GET_IMAGE);
    // 等待指纹模组响应
    if (receiveResponse()) {
    } else {
      delay(1000);
      continue;
    }
  
    // 步骤2：生成特征
    send_cmd2(CMD_GEN_CHAR, BUFFER_ID);
    if (receiveResponse()) {
      BUFFER_ID++;
    } else {
      continue;
    }
  }

  // 步骤3：合并特征
  send_cmd(CMD_REG_MODEL);
  if (receiveResponse()) {

  } else {
    return 0;
  }

  // 步骤4：存储模板
  sendCommand(CMD_STORE_CHAR, BUFFER_ID, TEMPLATE_ID);
  if (receiveResponse()) {
    
  } else {
    return 0;
  }

  return 1;
}

//搜索指纹
int search_FP(void)
{
  int serch_cnt = 0; 
  BUFFER_ID = 1;
  while(serch_cnt <= 5) {
    // 步骤1：获取图像
    send_cmd(CMD_GET_IMAGE);
  
    // 等待指纹模组响应
    if (receiveResponse()) {

    } else {
      delay(1000);
      continue;
    }
  
    // 步骤2：生成特征
    send_cmd2(CMD_GEN_CHAR, BUFFER_ID);
    if (receiveResponse()) {
      break;
    } else {
      serch_cnt++;
      delay(500);
      continue;
    }
  }

  // 步骤3：搜索指纹
  BUFFER_ID = 1;
  sendCommand1(CMD_SEARCH, BUFFER_ID, 1, 1);
  if (receiveResponse()) {
    return 1;
  }
  return 0;
}

//清空指纹库
int clear_FP_all_lib(void)
{
  send_cmd(CMD_CLEAR_LIB);
  if (receiveResponse()) {
    return 1;
  }
  return 0;
}

// 发送指令包
void send_cmd(uint8_t cmd) {
  uint8_t packet[12];
  uint16_t length=3;
  uint16_t checksum =  1+length+cmd;

  // 构建指令包
  packet[0] = HEADER_HIGH;
  packet[1] = HEADER_LOW;
  packet[2] = (DEVICE_ADDRESS >> 24) & 0xFF;
  packet[3] = (DEVICE_ADDRESS >> 16) & 0xFF;
  packet[4] = (DEVICE_ADDRESS >> 8) & 0xFF;
  packet[5] = DEVICE_ADDRESS & 0xFF;
  packet[6] = 0x01;  // 包标识：命令包
  packet[7] = (length >> 8) & 0xFF;  // 包长度高字节
  packet[8] = length & 0xFF;  // 包长度低字节
  packet[9] = cmd;
  
  packet[10] = (checksum >> 8) & 0xFF;
  packet[11] = checksum & 0xFF;

  // Serial.println("send1:");
  // printHex(packet,(2+4+3+length));

  // 发送指令包
  for (int i = 0; i < (2+4+3+length); i++) {
    fingerprintSerial.write(packet[i]);
  }
  
}
// 发送指令包
void send_cmd2(uint8_t cmd, uint8_t param1 ) {
  uint8_t packet[13];
  uint16_t length=4;
  uint16_t checksum =  1+length+cmd + param1;

  // 构建指令包
  packet[0] = HEADER_HIGH;
  packet[1] = HEADER_LOW;
  packet[2] = (DEVICE_ADDRESS >> 24) & 0xFF;
  packet[3] = (DEVICE_ADDRESS >> 16) & 0xFF;
  packet[4] = (DEVICE_ADDRESS >> 8) & 0xFF;
  packet[5] = DEVICE_ADDRESS & 0xFF;
  packet[6] = 0x01;  // 包标识：命令包
  packet[7] = (length >> 8) & 0xFF;  // 包长度高字节
  packet[8] = length & 0xFF;  // 包长度低字节
  packet[9] = cmd;
  packet[10] = param1;
  packet[11] = (checksum >> 8) & 0xFF;
  packet[12] = checksum & 0xFF;

  // Serial.println("send2:");
  // printHex(packet,(2+4+3+length));

  // 发送指令包
  for (int i = 0; i < (2+4+3+length); i++) {
    fingerprintSerial.write(packet[i]);
  }

}

// 发送指令包
void sendCommand(uint8_t cmd, uint8_t param1, uint16_t param2) {
  uint8_t packet[15];
  uint16_t length=6;
  uint16_t checksum =  1+length+cmd + param1 + (param2 >> 8) + (param2 & 0xFF);

  // 构建指令包
  packet[0] = HEADER_HIGH;
  packet[1] = HEADER_LOW;
  packet[2] = (DEVICE_ADDRESS >> 24) & 0xFF;
  packet[3] = (DEVICE_ADDRESS >> 16) & 0xFF;
  packet[4] = (DEVICE_ADDRESS >> 8) & 0xFF;
  packet[5] = DEVICE_ADDRESS & 0xFF;
  packet[6] = 0x01;  // 包标识：命令包
  packet[7] = (length >> 8) & 0xFF;  // 包长度高字节
  packet[8] = length & 0xFF;  // 包长度低字节
  packet[9] = cmd;
  packet[10] = param1;
  packet[11] = (param2 >> 8) & 0xFF;
  packet[12] = param2 & 0xFF;
  packet[13] = (checksum >> 8) & 0xFF;
  packet[14] = checksum & 0xFF;

  // Serial.println("send:");
  // printHex(packet,(2+4+3+length));

  // 发送指令包
  for (int i = 0; i < (2+4+3+length); i++) {
    fingerprintSerial.write(packet[i]);
  }
}

// 发送指令包
void sendCommand1(uint8_t cmd, uint8_t param1, uint16_t param2, uint16_t param3) {
  uint8_t packet[17];
  uint16_t length=8;
  uint16_t checksum =  1+length+cmd + param1 + (param2 >> 8) + (param2 & 0xFF) + (param3 >> 8) + (param3 & 0xFF);

  // 构建指令包
  packet[0] = HEADER_HIGH;
  packet[1] = HEADER_LOW;
  packet[2] = (DEVICE_ADDRESS >> 24) & 0xFF;
  packet[3] = (DEVICE_ADDRESS >> 16) & 0xFF;
  packet[4] = (DEVICE_ADDRESS >> 8) & 0xFF;
  packet[5] = DEVICE_ADDRESS & 0xFF;
  packet[6] = 0x01;  // 包标识：命令包
  packet[7] = (length >> 8) & 0xFF;  // 包长度高字节
  packet[8] = length & 0xFF;  // 包长度低字节
  packet[9] = cmd;
  packet[10] = param1;
  packet[11] = (param2 >> 8) & 0xFF;
  packet[12] = param2 & 0xFF;
  packet[13] = (param3 >> 8) & 0xFF;
  packet[14] = param3 & 0xFF;
  packet[15] = (checksum >> 8) & 0xFF;
  packet[16] = checksum & 0xFF;

  // Serial.println("send:");
  // printHex(packet,(2+4+3+length));

  // 发送指令包
  for (int i = 0; i < (2+4+3+length); i++) {
    fingerprintSerial.write(packet[i]);
  }
}

// 接收响应包
bool receiveResponse() {
  uint8_t response[50];
  uint8_t index = 0;
  uint32_t startTime = millis();

  // 等待响应包
  while (millis() - startTime < 200) {
    if (fingerprintSerial.available()) {
      response[index++] = fingerprintSerial.read();
    }
  }

  // 打印响应包
  // printResponse(response, index);

  // 检查确认码
  if (index >= 12 && response[9] == 0x00) {
    return true;  // 成功
  } else {
    return false; // 失败
  }
}

// 打印响应包
void printResponse(uint8_t *response, uint8_t length) {
  Serial.print("Response:");
  for (int i = 0; i < length; i++) {
    if(response[i] < 0x10) Serial.print('0');
    Serial.print(response[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

#include <SoftwareSerial.h>

// RS485 통신 핀 설정 (ESP32와 맞춤)
#define RS485_TX 9    // 송신 핀
#define RS485_RX 8    // 수신 핀  
#define RS485_DE_RE 2 // DE/RE 제어 핀

// SoftwareSerial 객체 생성
SoftwareSerial rs485(RS485_RX, RS485_TX); // RX, TX 순서

// Modbus holding 레지스터 배열 (64워드 = 32 float)
uint16_t holdingRegs[64];

// Modbus 통신 버퍼
byte rxBuffer[256];
int rxIndex = 0;

// float → 2워드 변환 (워드 내 빅엔디언, 워드 간 리틀엔디언)
void floatToRegs(float val, int idx) {
  // 배열 범위 검사 (64개 레지스터이므로 인덱스 62까지 가능)
  if (idx < 0 || idx > 62) return; // 0~62까지 허용 (레지스터 0~63 사용)
  
  union {
    float f;
    uint8_t b[4];
  } u;
  u.f = val;
  // 워드 간 리틀엔디언: [word1][word0]
  // 워드 내 빅엔디언: 상위바이트 먼저
  holdingRegs[idx]   = (u.b[2] << 8) | u.b[3]; // word1 (빅엔디언)
  holdingRegs[idx+1] = (u.b[0] << 8) | u.b[1]; // word0 (빅엔디언)
}

// CRC16 계산
uint16_t calculateCRC16(byte* data, int length) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < length; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc = crc >> 1;
      }
    }
  }
  return crc;
}

// Modbus 응답 전송
void sendModbusResponse(byte slaveId, byte functionCode, byte* data, int dataLength) {
  digitalWrite(RS485_DE_RE, HIGH); // 송신 모드
  delay(2); // 송신 모드 전환 대기
  
  // Modbus 응답 형식: [SlaveID][FunctionCode][ByteCount][Data...][CRC_L][CRC_H]
  rs485.write(slaveId);
  delayMicroseconds(100); // 작은 지연
  rs485.write(functionCode);
  delayMicroseconds(100);
  rs485.write(dataLength);
  delayMicroseconds(100);
  
  for (int i = 0; i < dataLength; i++) {
    rs485.write(data[i]);
    delayMicroseconds(100); // 각 바이트 사이에 작은 지연
  }
  
  // CRC 계산 (SlaveID + FunctionCode + ByteCount + Data)
  byte crcData[dataLength + 3];
  crcData[0] = slaveId;
  crcData[1] = functionCode;
  crcData[2] = dataLength;
  for (int i = 0; i < dataLength; i++) {
    crcData[3 + i] = data[i];
  }
  
  uint16_t crc = calculateCRC16(crcData, dataLength + 3);
  rs485.write(crc & 0xFF);        // CRC Low
  delayMicroseconds(100);
  rs485.write((crc >> 8) & 0xFF); // CRC High
  
  // 디버깅: 전송된 바이트 출력
  Serial.print("전송 바이트: ");
  Serial.print("0x"); Serial.print(slaveId, HEX); Serial.print(" ");
  Serial.print("0x"); Serial.print(functionCode, HEX); Serial.print(" ");
  Serial.print("0x"); Serial.print(dataLength, HEX); Serial.print(" ");
  for (int i = 0; i < dataLength; i++) {
    Serial.print("0x");
    if (data[i] < 0x10) Serial.print("0");
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.print("0x"); if ((crc & 0xFF) < 0x10) Serial.print("0"); Serial.print(crc & 0xFF, HEX); Serial.print(" ");
  Serial.print("0x"); if (((crc >> 8) & 0xFF) < 0x10) Serial.print("0"); Serial.print((crc >> 8) & 0xFF, HEX);
  Serial.println();
  
  delay(5); // 응답 완료 대기
  digitalWrite(RS485_DE_RE, LOW); // 수신 모드
  delay(50); // 더 긴 지연 (ESP32가 데이터를 완전히 받을 수 있도록)
}

// Modbus 요청 처리
void processModbusRequest() {
  if (rxIndex < 4) return; // 최소 길이 확인
  
  byte slaveId = rxBuffer[0];
  byte functionCode = rxBuffer[1];
  
  // CRC 검증
  uint16_t receivedCRC = (rxBuffer[rxIndex-1] << 8) | rxBuffer[rxIndex-2];
  uint16_t calculatedCRC = calculateCRC16(rxBuffer, rxIndex-2);
  
  if (receivedCRC != calculatedCRC) {
    Serial.println("CRC Error");
    return;
  }
  
  // Holding Register 읽기 (Function Code 03)
  if (functionCode == 0x03 && slaveId == 1) {
    uint16_t startAddr = (rxBuffer[2] << 8) | rxBuffer[3];
    uint16_t registerCount = (rxBuffer[4] << 8) | rxBuffer[5];
    
    Serial.print("Modbus 요청: 시작주소=0x");
    Serial.print(startAddr, HEX);
    Serial.print(", 레지스터수=");
    Serial.println(registerCount);
    
    if (startAddr == 0x00CB && registerCount == 64) { // 시작주소 0x00CB, 64개 레지스터
      // 응답 데이터 준비 (64 레지스터 = 128 바이트)
      byte responseData[128];
      
      for (int i = 0; i < 64; i++) {
        responseData[i*2] = (holdingRegs[i] >> 8) & 0xFF;     // High byte
        responseData[i*2 + 1] = holdingRegs[i] & 0xFF;        // Low byte
      }
      
      Serial.println("=== Modbus 응답 전송 시작 ===");
      Serial.print("Slave ID: 1, Function: 0x03, Data Length: 128");
      Serial.println();
      
      sendModbusResponse(1, 0x03, responseData, 128);
      Serial.println("=== Modbus 응답 전송 완료 ===");
      
      // 디버깅: 전송된 데이터 출력
      Serial.print("전송 데이터: ");
      for (int i = 0; i < 64; i++) {
        Serial.print("0x");
        if (holdingRegs[i] < 0x1000) Serial.print("0");
        if (holdingRegs[i] < 0x100) Serial.print("0");
        if (holdingRegs[i] < 0x10) Serial.print("0");
        Serial.print(holdingRegs[i], HEX);
        Serial.print(" ");
      }
      Serial.println();
    } else {
      Serial.println("잘못된 레지스터 주소 또는 개수");
    }
  }
}

void setup() {
  Serial.begin(9600); // 디버깅용 시리얼 (그대로 유지)
  rs485.begin(4800); // SoftwareSerial을 4800 baud로 변경
  
  // 랜덤 시드 초기화 (analogRead 대신 millis 사용)
  randomSeed(millis());
  
  // RS485 통신 핀 설정
  pinMode(RS485_TX, OUTPUT);     // 송신 핀
  pinMode(RS485_RX, INPUT);      // 수신 핀
  pinMode(RS485_DE_RE, OUTPUT);  // DE/RE 제어 핀
  digitalWrite(RS485_DE_RE, LOW); // 수신 모드로 시작
  
  // holding 레지스터 초기화
  for (int i = 0; i < 64; i++) {
    holdingRegs[i] = 0;
  }
  
  Serial.println("Modbus 서버 시작 (ID: 1)");
  Serial.println("RS485 핀: TX=9, RX=8, DE/RE=2");
  Serial.println("통신 속도: 4800 baud");
}

void loop() {
  // 32개 센서값 생성 (랜덤) - 실제 장비 프로토콜 대응
  float sensorData[32];
  
  // 기본 6개 센서 (기존과 동일)
  sensorData[0] = random(200, 350) / 10.0;   // 온도 (20.0~35.0°C)
  sensorData[1] = random(300, 900) / 10.0;   // 습도 (30~90%)
  sensorData[2] = random(0, 1000);           // 조도 (0~1000 Lux)
  sensorData[3] = random(0, 150) / 10.0;     // 풍속 (0~15.0 m/s)
  sensorData[4] = random(0, 360);            // 풍향 (0~359°)
  sensorData[5] = random(0, 2);              // 강우센서 (0 or 1)
  
  // 추가 26개 센서 (확장된 가상 센서)
  for (int i = 6; i < 32; i++) {
    switch (i % 6) {
      case 0: sensorData[i] = random(150, 400) / 10.0; break;  // 온도 변형
      case 1: sensorData[i] = random(200, 950) / 10.0; break;  // 습도 변형
      case 2: sensorData[i] = random(0, 1500); break;          // 조도 변형
      case 3: sensorData[i] = random(0, 200) / 10.0; break;    // 풍속 변형
      case 4: sensorData[i] = random(0, 360); break;           // 풍향 변형
      case 5: sensorData[i] = random(0, 2); break;             // 디지털 센서
    }
  }

  // holding 레지스터에 저장 (32개 float → 64개 레지스터)
  for (int i = 0; i < 32; i++) {
    floatToRegs(sensorData[i], i * 2);  // 각 float는 2개 레지스터 사용
  }

  // Modbus 요청 수신 및 처리
  while (rs485.available()) { // SoftwareSerial 사용
    byte data = rs485.read();
    if (rxIndex < 256) {
      rxBuffer[rxIndex++] = data;
    }
    
    // 3.5 문자 시간 대기 (4800 baud에서 약 7ms)
    unsigned long startTime = millis();
    while (millis() - startTime < 7) {
      if (rs485.available()) {
        data = rs485.read();
        if (rxIndex < 256) {
          rxBuffer[rxIndex++] = data;
        }
      }
    }
    
    // 받은 데이터 디버깅
    if (rxIndex > 0) {
      Serial.print("받은 데이터 (");
      Serial.print(rxIndex);
      Serial.print("바이트): ");
      for (int i = 0; i < rxIndex; i++) {
        Serial.print("0x");
        if (rxBuffer[i] < 0x10) Serial.print("0");
        Serial.print(rxBuffer[i], HEX);
        Serial.print(" ");
      }
      Serial.println();
    }
    
    // 요청 처리
    processModbusRequest();
    rxIndex = 0; // 버퍼 리셋
  }

  // 현재 값 출력 (디버깅용 - 첫 6개만)
  Serial.print("Temp: ");
  Serial.print(sensorData[0], 1);
  Serial.print("°C, Hum: ");
  Serial.print(sensorData[1], 1);
  Serial.print("%, Solar: ");
  Serial.print(sensorData[2], 0);
  Serial.print(" Lux, Wind: ");
  Serial.print(sensorData[3], 1);
  Serial.print(" m/s, Dir: ");
  Serial.print(sensorData[4], 0);
  Serial.print("°, Rain: ");
  Serial.println(sensorData[5]);
  
  delay(1000); // 1초 대기
} 
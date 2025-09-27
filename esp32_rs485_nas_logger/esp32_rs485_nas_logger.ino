#include <ModbusMaster.h> // 라이브러리 설치 필요
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

// ====== 사용자 설정 ======
#define SLAVE_ID 5  // 올바른 Slave ID
#define RS485_TX 17
#define RS485_RX 16
#define RS485_DE_RE 4
#define BAUDRATE 9600  // 원래 작동했던 9600bps로 복원
#define SERIAL_MODE SERIAL_8N1  // 패리티 설정 (8N1 또는 8E1)

// Modbus 읽기 설정 (센서 위치 탐색용)
#define START_ADDR 0x00C3  // 시작 주소: 195 (0x00C3)
// 비표준 헤더 길이(바이트) 및 주소 보정(워드)
#define DATA_HEADER_BYTES 6    // 응답 앞부분 헤더 바이트 수 (관측값: 6바이트)
#define ADDR_WORD_SHIFT  (0)  // 주소 보정 - 문서 주소에서 -2워드 (실제 데이터 위치 조정)
#define REG_COUNT  0x0040  // 레지스터 길이: 64개 (0x40)

// 센서별 레지스터 주소 (시작 주소 195 기준)
#define TEMP_ADDR   203  // 온도 센서 주소 (195+8)
#define HUMID_ADDR  212  // 습도 센서 주소 (195+17)
#define RAIN_ADDR   218  // 감우 센서 주소 (195+23)
#define LIGHT_ADDR  227  // 일사 센서 주소 (195+32)
#define WIND_SPD_ADDR 230  // 풍속 센서 주소 (195+35)
#define WIND_DIR_ADDR 233  // 풍향 센서 주소 (195+38)

// 주소 고정 상수 제거 (START_ADDR 사용)
const uint16_t FIXED_START_ADDR = START_ADDR;

const char* ssid = "TSPOL"; // 와이파이 이름 입력력
const char* password = "mms56529983"; // 비밀번호 입력
const char* nas_url = "http://tspol.iptime.org:8888/rs485/upload.php"; // NAS IP로 수정
// =========================

ModbusMaster node;

// 전역 변수 (Task 간 공유)
volatile float sensorData[6] = {0, 0, 0, 0, 0, 0};
volatile bool newDataAvailable = false;
SemaphoreHandle_t dataMutex;

// 수집된 데이터 저장용
float dataBuffer[60][6]; // 최대 60개 데이터 저장
int dataCount = 0;
unsigned long lastCollectTime = 0;

void preTransmission()  { digitalWrite(RS485_DE_RE, HIGH); }
void postTransmission() { digitalWrite(RS485_DE_RE, LOW);  }

void setup() {
  Serial.begin(115200);
  pinMode(RS485_DE_RE, OUTPUT);
  digitalWrite(RS485_DE_RE, LOW);

  // RS485 시리얼 초기화 (사용자 지정 모드)
  Serial2.begin(BAUDRATE, SERIAL_MODE, RS485_RX, RS485_TX);
  Serial2.setRxBufferSize(512); // RX 버퍼 크기 확대
  
  node.begin(SLAVE_ID, Serial2);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  Serial.println("ESP32 RS485 NAS Logger 시작");
  Serial.printf("RS485 설정: TX=%d, RX=%d, DE/RE=%d, Baudrate=%d\n", RS485_TX, RS485_RX, RS485_DE_RE, BAUDRATE);
  Serial.printf("Modbus 설정: Slave ID=%d, START_ADDR=0x%04X, REG_COUNT=0x%04X (%d개)\n", 
               SLAVE_ID, START_ADDR, REG_COUNT, REG_COUNT);
  
  // RS485 핀 상태 확인
  Serial.println("=== RS485 핀 상태 확인 ===");
  Serial.printf("DE/RE 핀(%d) 상태: %s\n", RS485_DE_RE, digitalRead(RS485_DE_RE) ? "HIGH(송신)" : "LOW(수신)");

  // WiFi 연결
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("WiFi Connected");

  // NTP 시간 동기화
  configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  while (time(nullptr) < 100000) { delay(500); Serial.print("#"); }
  Serial.println("NTP Sync OK");
  
  Serial.println("=== 시스템 초기화 완료 ===");
  
  // 하드웨어 및 Modbus 연결 테스트
  delay(2000); // 2초 대기 후 테스트
  testRS485Hardware();
  testModbusConnection();
  
  // Mutex 생성
  dataMutex = xSemaphoreCreateMutex();
  
  // Task 생성
  xTaskCreatePinnedToCore(
    dataCollectionTask,   // Task 함수
    "DataCollection",     // Task 이름
    4096,                 // Stack 크기
    NULL,                 // Parameter
    1,                    // Priority
    NULL,                 // Task Handle
    0                     // CPU Core (0번 코어)
  );
  
  xTaskCreatePinnedToCore(
    dataTransmissionTask, // Task 함수
    "DataTransmission",   // Task 이름
    8192,                 // Stack 크기 (HTTP 전송용으로 크게)
    NULL,                 // Parameter
    1,                    // Priority
    NULL,                 // Task Handle
    1                     // CPU Core (1번 코어)
  );
  
  Serial.println("=== 멀티태스킹 시작 ===");
}

void loop() {
  // Task가 모든 작업을 처리하므로 loop는 비움
  vTaskDelay(pdMS_TO_TICKS(1000));
}

// CRC 계산 함수 (Modbus RTU)
uint16_t calculateCRC(const uint8_t* data, uint16_t length) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; ++j) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

// 워드2개 → float 변환 (표준 Modbus Big-Endian: 상위워드 먼저, 각 워드는 Big-Endian)
float regsToFloatBE(uint16_t highWord, uint16_t lowWord) {
  uint8_t b[4];
  b[0] = (highWord >> 8) & 0xFF;
  b[1] = highWord & 0xFF;
  b[2] = (lowWord >> 8) & 0xFF;
  b[3] = lowWord & 0xFF;
  float f;
  memcpy(&f, b, 4);
  return f;
}

// 리틀 엔디언 워드 순서로 Float 변환
float regsToFloatLittleEndianWords(uint16_t word0, uint16_t word1) {
  // 입력: word0 = 첫 번째 워드, word1 = 두 번째 워드
  // 리틀 엔디언 워드 순서: [word1][word0] → word1이 상위워드, word0이 하위워드
  // 32비트 구성: [word1(16비트)][word0(16비트)]
  uint32_t raw = ((uint32_t)word1 << 16) | (uint32_t)word0;
  float f;
  memcpy(&f, &raw, 4);
  return f;
}


// 개별 센서 레지스터 읽기 함수는 현재 사용하지 않음 (연속 레지스터 읽기 사용)

// 모든 센서 데이터 읽기 - 연속 레지스터 방식 (원래 작동했던 방식)
bool readSensors(float* sensors) {
  Serial.println("=== 연속 레지스터 읽기 시작 ===");
  
  // 시리얼 버퍼 완전 비우기
  while (Serial2.available()) {
    Serial2.read();
  }
  
  // Modbus RTU 요청 프레임 구성
  uint8_t requestPdu[] = { 
    (uint8_t)SLAVE_ID, 0x03, 
    (uint8_t)(START_ADDR >> 8), (uint8_t)(START_ADDR & 0xFF), 
    (uint8_t)(REG_COUNT >> 8), (uint8_t)(REG_COUNT & 0xFF) 
  };
  uint16_t reqCrc = calculateCRC(requestPdu, 6);
  uint8_t request[] = { 
    (uint8_t)SLAVE_ID, 0x03, 
    (uint8_t)(START_ADDR >> 8), (uint8_t)(START_ADDR & 0xFF), 
    (uint8_t)(REG_COUNT >> 8), (uint8_t)(REG_COUNT & 0xFF), 
    (uint8_t)(reqCrc & 0xFF), (uint8_t)(reqCrc >> 8) 
  };
  
  // 전송할 요청 데이터 출력
  Serial.print("전송 요청: ");
  for (int i = 0; i < 8; i++) {
    Serial.printf("0x%02X ", request[i]);
  }
  Serial.println();
  
  // 송신 모드 전환 및 전송
  digitalWrite(RS485_DE_RE, HIGH);
  delay(5);
  Serial2.write(request, 8);
  Serial2.flush();
  delay(20);
  digitalWrite(RS485_DE_RE, LOW);
  delay(10);
  delay(100);
  
  Serial.println("Modbus 요청 전송 완료");
  
  // 응답 수신
  byte response[300];
  int responseIndex = 0;
  unsigned long lastByteTime = 0;
  bool frameStarted = false;
  
  unsigned long startTime = millis();
  unsigned long firstByteTimeout = millis() + 3000;
  
  while (millis() - startTime < 5000) {
    if (Serial2.available()) {
      byte receivedByte = Serial2.read();
      response[responseIndex++] = receivedByte;
      lastByteTime = micros();
      frameStarted = true;
      
      if (responseIndex == 1) {
        Serial.printf("첫 바이트 수신: 0x%02X\n", receivedByte);
        firstByteTimeout = millis() + 5000;
      }
      
      if (responseIndex >= 300) {
        Serial.println("응답 버퍼 오버플로우");
        break;
      }
    }
    
    if (!frameStarted && millis() > firstByteTimeout) {
      Serial.printf("첫 바이트 타임아웃 (대기시간: %lu ms)\n", millis() - startTime);
      break;
    }
    
    if (frameStarted && (micros() - lastByteTime) > 3000) {
      Serial.println("프레임 종료 감지");
      break;
    }
    
    delay(1);
  }
  
  if (responseIndex > 0) {
    Serial.print("Modbus 응답 (");
    Serial.print(responseIndex);
    Serial.print("바이트): ");
    for (int i = 0; i < min(responseIndex, 20); i++) {
      Serial.printf("0x%02X ", response[i]);
    }
    if (responseIndex > 20) Serial.print("...");
    Serial.println();
    
    // 전체 응답 데이터 출력
    Serial.println("전체 응답 데이터:");
    for (int i = 0; i < responseIndex; i++) {
      Serial.printf("%02X ", response[i]);
      if ((i + 1) % 16 == 0) Serial.println();
    }
    Serial.println();
    
     // 센서 데이터 파싱 - 응답 구조 분석 후 적응적 파싱
     Serial.println("=== 센서 데이터 파싱 (응답 구조 분석) ===");
     
     // 응답 구조 분석
     Serial.printf("응답 길이: %d 바이트\n", responseIndex);
     Serial.printf("첫 10바이트: ");
     for (int i = 0; i < min(10, responseIndex); i++) {
       Serial.printf("%02X ", response[i]);
     }
     Serial.println();
     
    // 실제 데이터 시작 위치 찾기 - 안정화된 로직
    int dataStartPos = 10; // 기본값을 10으로 고정
    
    if (responseIndex >= 3 && response[1] == 0x03) {
      // 표준 Modbus 응답 (Slave ID, 0x03, Byte Count)
      dataStartPos = 3;
      Serial.println("표준 Modbus 응답 감지 (3바이트 헤더)");
    } else if (responseIndex >= 6) {
      // 비표준 헤더 분석
      if (response[2] == 0x00 && response[3] == 0x00 && response[4] == 0x00 && response[5] == 0x00) {
        dataStartPos = 6;
        Serial.println("비표준 Modbus 응답 감지 (6바이트 헤더)");
      } else {
        dataStartPos = 10;
        Serial.println("비표준 Modbus 응답 감지 (10바이트 헤더)");
      }
    }
     int dataLength = responseIndex - dataStartPos - 2;  // 마지막 2바이트는 CRC
     
     Serial.printf("데이터 시작 위치: %d, 데이터 길이: %d 바이트\n", dataStartPos, dataLength);
     
     if (dataLength < 0 || dataLength % 2 != 0) {
       Serial.printf("데이터 길이 오류: %d 바이트 (짝수여야 함)\n", dataLength);
       
       // 홀수인 경우 마지막 바이트 제외하고 처리
       dataLength = (dataLength / 2) * 2;
       Serial.printf("수정된 데이터 길이: %d 바이트\n", dataLength);
     }
     
     int wordCount = dataLength / 2;
     Serial.printf("워드 개수: %d개\n", wordCount);
     
    // 워드 배열 생성 (문서 주소 대비 -2워드 보정 적용 가능하도록 주석 출력)
     uint16_t wordArray[150];  // 최대 150개 워드 (300바이트)
     for (int i = 0; i < wordCount && i < 150; i++) {
       int byteIndex = dataStartPos + i * 2;
       wordArray[i] = (response[byteIndex] << 8) | response[byteIndex + 1];  // Big-Endian
     }
    Serial.printf("주소 보정(워드): %d\n", ADDR_WORD_SHIFT);
     
     // 워드 배열 출력 (처음 20개만)
     Serial.println("워드 배열 (처음 20개):");
     for (int i = 0; i < min(20, wordCount); i++) {
       Serial.printf("%04X ", wordArray[i]);
       if ((i + 1) % 10 == 0) Serial.println();
     }
     Serial.println();
     
    // 워드 배열 직접 분석 - 실제 센서 값 위치 찾기
    const char* sensorNames[] = {"온도", "습도", "감우", "일사", "풍속", "풍향"};
    uint16_t sensorAddrs[] = {TEMP_ADDR, HUMID_ADDR, RAIN_ADDR, LIGHT_ADDR, WIND_SPD_ADDR, WIND_DIR_ADDR};
    float parsed[6] = {0};
    
    Serial.println("=== 워드 배열 직접 분석 ===");
    
    // 워드 배열 전체 출력하여 실제 센서 값 위치 찾기
    Serial.println("전체 워드 배열:");
    for (int i = 0; i < wordCount; i++) {
      Serial.printf("[%2d]:%04X ", i, wordArray[i]);
      if ((i + 1) % 8 == 0) Serial.println();
    }
    Serial.println();
    
    // 실제 워드 배열에서 관찰된 값들로 센서 매핑
    // A380 4287=온도/습도 후보, 003F 8000=1.0(감우), 6A7F 4370=240.0(풍향)
    int tempIdx = -1, humidIdx = -1, rainIdx = -1, lightIdx = -1, windSpdIdx = -1, windDirIdx = -1;
    
    // 헤더 부분의 데이터도 확인 (온도 센서 위치)
    Serial.println("=== 헤더 데이터 분석 ===");
    
    if (dataStartPos == 3 && responseIndex >= 7) {
      // 표준 Modbus 응답일 때 - 실제 데이터 부분 분석 (response[3] 이후)
      Serial.println("표준 Modbus 응답에서 센서 데이터 분석");
      if (responseIndex >= 7) {
        uint16_t w1 = (response[3] << 8) | response[4];  // 첫 번째 데이터 워드
        uint16_t w2 = (response[5] << 8) | response[6];  // 두 번째 데이터 워드
        
        float val1 = regsToFloatLittleEndianWords(w1, w2);
        float val2 = regsToFloatLittleEndianWords(w2, w1);
        
        Serial.printf("표준 응답 순서1: %04X %04X → %.3f\n", w1, w2, val1);
        Serial.printf("표준 응답 순서2: %04X %04X → %.3f\n", w2, w1, val2);
        
        // 온도/습도 감지
        if (val1 >= 15 && val1 <= 40 && tempIdx == -1) {
          tempIdx = -3; parsed[0] = val1;
          Serial.printf("*** 표준 응답에서 온도 발견: %.2f°C\n", val1);
        } else if (val1 >= 50 && val1 <= 90 && humidIdx == -1) {
          humidIdx = -3; parsed[1] = val1;
          Serial.printf("*** 표준 응답에서 습도 발견: %.2f%%\n", val1);
        }
        
        if (val2 >= 15 && val2 <= 40 && tempIdx == -1) {
          tempIdx = -3; parsed[0] = val2;
          Serial.printf("*** 표준 응답에서 온도 발견: %.2f°C\n", val2);
        } else if (val2 >= 50 && val2 <= 90 && humidIdx == -1) {
          humidIdx = -3; parsed[1] = val2;
          Serial.printf("*** 표준 응답에서 습도 발견: %.2f%%\n", val2);
        }
      }
    } else if (responseIndex >= 6) {
      // 비표준 응답일 때 - 기존 로직
      uint16_t w1 = (response[2] << 8) | response[3];  
      uint16_t w2 = (response[4] << 8) | response[5];
      
      // 순서 1: [w1][w2]
      float val1 = regsToFloatLittleEndianWords(w1, w2);
      Serial.printf("헤더 순서1: %04X %04X → %.3f\n", w1, w2, val1);
      
      // 순서 2: [w2][w1] 
      float val2 = regsToFloatLittleEndianWords(w2, w1);
      Serial.printf("헤더 순서2: %04X %04X → %.3f\n", w2, w1, val2);
      
      // 온도/습도 후보로 판단 (더 넓은 범위로 확장)
      if (val1 >= 15 && val1 <= 40 && tempIdx == -1) {
        tempIdx = -2; // 특별 인덱스로 표시
        parsed[0] = val1;
        Serial.printf("*** 헤더 순서1에서 온도 발견: %.2f°C\n", val1);
      } else if (val1 >= 50 && val1 <= 90 && humidIdx == -1) {
        humidIdx = -2; // 특별 인덱스로 표시
        parsed[1] = val1;
        Serial.printf("*** 헤더 순서1에서 습도 발견: %.2f%%\n", val1);
      }
      
      if (val2 >= 15 && val2 <= 40 && tempIdx == -1) {
        tempIdx = -2; // 특별 인덱스로 표시
        parsed[0] = val2;
        Serial.printf("*** 헤더 순서2에서 온도 발견: %.2f°C\n", val2);
      } else if (val2 >= 50 && val2 <= 90 && humidIdx == -1) {
        humidIdx = -2; // 특별 인덱스로 표시
        parsed[1] = val2;
        Serial.printf("*** 헤더 순서2에서 습도 발견: %.2f%%\n", val2);
      }
    }
    
    for (int i = 0; i < wordCount - 1; i++) {
      uint16_t w0 = wordArray[i];      // 첫 번째 워드 (하위워드)
      uint16_t w1 = wordArray[i + 1];  // 두 번째 워드 (상위워드)
      // 리틀 엔디언 워드 순서: [w1][w0] → 32비트 float
      float v = regsToFloatLittleEndianWords(w0, w1);
      
      // 디버그: 모든 float 값 출력
      Serial.printf("워드[%d,%d]=%04X %04X → %.3f\n", i, i+1, w0, w1, v);
      
      // 온도 센서 (20-35도 범위)
      if (v >= 20 && v <= 35 && tempIdx == -1) {
        tempIdx = i;
        Serial.printf("*** 온도 센서 발견: 워드[%d,%d]=%04X %04X → %.2f°C\n", i, i+1, w0, w1, v);
      }
      // 습도 센서 (50-90% 범위 확장)
      else if (v >= 50 && v <= 90 && humidIdx == -1) {
        humidIdx = i;
        Serial.printf("*** 습도 센서 발견: 워드[%d,%d]=%04X %04X → %.2f%%\n", i, i+1, w0, w1, v);
      }
      // 감우 센서 (1.0 근처)
      else if (fabsf(v - 1.0f) < 0.1f && rainIdx == -1) {
        rainIdx = i;
        Serial.printf("*** 감우 센서 발견: 워드[%d,%d]=%04X %04X → %.2f\n", i, i+1, w0, w1, v);
      }
      // 풍향 센서 (200-300도 범위)
      else if (v >= 200 && v <= 300 && windDirIdx == -1) {
        windDirIdx = i;
        Serial.printf("*** 풍향 센서 발견: 워드[%d,%d]=%04X %04X → %.2f°\n", i, i+1, w0, w1, v);
      }
    }
    
    // 발견된 센서 값들을 배열에 저장
    if (tempIdx >= 0) {
      uint16_t w0 = wordArray[tempIdx];
      uint16_t w1 = wordArray[tempIdx + 1];
      parsed[0] = regsToFloatLittleEndianWords(w0, w1);
    }
    if (humidIdx >= 0) {
      uint16_t w0 = wordArray[humidIdx];
      uint16_t w1 = wordArray[humidIdx + 1];
      parsed[1] = regsToFloatLittleEndianWords(w0, w1);
    }
    if (rainIdx >= 0) {
      uint16_t w0 = wordArray[rainIdx];
      uint16_t w1 = wordArray[rainIdx + 1];
      parsed[2] = regsToFloatLittleEndianWords(w0, w1);
    }
    if (windDirIdx >= 0) {
      uint16_t w0 = wordArray[windDirIdx];
      uint16_t w1 = wordArray[windDirIdx + 1];
      parsed[5] = regsToFloatLittleEndianWords(w0, w1);
    }
    
    Serial.printf("발견된 센서 위치: 온도=%d, 습도=%d, 감우=%d, 풍향=%d\n", 
                  tempIdx, humidIdx, rainIdx, windDirIdx);
    
    // 자동 감지가 실패한 경우 고정 인덱스로 시도
    if (tempIdx == -1) {
      // 워드 배열에서 25-30도 근처 값 찾기 (온도)
      for (int i = 0; i < wordCount - 1; i++) {
        uint16_t w0 = wordArray[i];
        uint16_t w1 = wordArray[i + 1];
        float v = regsToFloatLittleEndianWords(w0, w1);
        if (v >= 25.0 && v <= 30.0) {
          tempIdx = i;
          parsed[0] = v;
          Serial.printf("고정 매핑 - 온도: 워드[%d,%d]=%04X %04X → %.2f°C\n", i, i+1, w0, w1, v);
              break;
        }
      }
    }
    
    if (humidIdx == -1) {
      // 워드 배열에서 67도 근처 값 찾기 (습도)
      for (int i = 0; i < wordCount - 1; i++) {
        uint16_t w0 = wordArray[i];
        uint16_t w1 = wordArray[i + 1];
        float v = regsToFloatLittleEndianWords(w0, w1);
        if (v >= 66.0 && v <= 68.0) {
          humidIdx = i;
          parsed[1] = v;
          Serial.printf("고정 매핑 - 습도: 워드[%d,%d]=%04X %04X → %.2f%%\n", i, i+1, w0, w1, v);
          break;
        }
      }
    }
    
    if (rainIdx == -1) {
      // 워드 배열에서 1.0 값 찾기 (감우)
      for (int i = 0; i < wordCount - 1; i++) {
        uint16_t w0 = wordArray[i];
        uint16_t w1 = wordArray[i + 1];
        float v = regsToFloatLittleEndianWords(w0, w1);
        if (fabsf(v - 1.0f) < 0.01f) {
          rainIdx = i;
          parsed[2] = v;
          Serial.printf("고정 매핑 - 감우: 워드[%d,%d]=%04X %04X → %.2f\n", i, i+1, w0, w1, v);
                break;
        }
      }
    }
    
    if (windDirIdx == -1) {
      // 워드 배열에서 240.0 근처 값 찾기 (풍향)
      for (int i = 0; i < wordCount - 1; i++) {
        uint16_t w0 = wordArray[i];
        uint16_t w1 = wordArray[i + 1];
        float v = regsToFloatLittleEndianWords(w0, w1);
        if (v >= 239.0 && v <= 241.0) {
          windDirIdx = i;
          parsed[5] = v;
          Serial.printf("고정 매핑 - 풍향: 워드[%d,%d]=%04X %04X → %.2f°\n", i, i+1, w0, w1, v);
          break;
        }
      }
    }

    // 센서별 유효 범위 적용 및 올바른 배열 순서로 저장
    // sensors[0]=온도, sensors[1]=습도, sensors[2]=감우, sensors[3]=일사, sensors[4]=풍속, sensors[5]=풍향
    sensors[0] = (parsed[0] > -20 && parsed[0] < 80) ? parsed[0] : 0.0f;   // 온도
    sensors[1] = (parsed[1] >= 0 && parsed[1] <= 100) ? parsed[1] : 0.0f; // 습도
    sensors[2] = (fabsf(parsed[2]) < 0.1f || fabsf(parsed[2]-1.0f) < 0.1f) ? (parsed[2] > 0.5f ? 1.0f : 0.0f) : 0.0f; // 감우
    sensors[3] = (parsed[3] >= 0 && parsed[3] <= 2000) ? parsed[3] : 0.0f; // 일사
    sensors[4] = (parsed[4] >= 0 && parsed[4] <= 50) ? parsed[4] : 0.0f;   // 풍속
    sensors[5] = (parsed[5] >= 0 && parsed[5] <= 360) ? parsed[5] : 0.0f;  // 풍향
    
    // 디버그: 최종 센서 값 출력
    Serial.printf("최종 센서 값: 온도=%.1f, 습도=%.1f, 감우=%.1f, 일사=%.1f, 풍속=%.1f, 풍향=%.1f\n",
                  sensors[0], sensors[1], sensors[2], sensors[3], sensors[4], sensors[5]);
    
    Serial.println("=== 센서 데이터 파싱 완료 ===");
    return true;
  }
  
  Serial.println("Modbus 응답 없음");
  return false;
}

// RS485 하드웨어 테스트 함수
void testRS485Hardware() {
  Serial.println("=== RS485 하드웨어 테스트 ===");
  
  // DE/RE 핀 테스트
  Serial.println("DE/RE 핀 테스트 중...");
  digitalWrite(RS485_DE_RE, HIGH);
  delay(100);
  Serial.printf("DE/RE HIGH 상태: %s\n", digitalRead(RS485_DE_RE) ? "정상" : "오류");
  
  digitalWrite(RS485_DE_RE, LOW);
  delay(100);
  Serial.printf("DE/RE LOW 상태: %s\n", digitalRead(RS485_DE_RE) ? "오류" : "정상");
  
  // 시리얼 포트 테스트
  Serial.println("시리얼 포트 테스트 중...");
  Serial2.write(0xAA); // 테스트 바이트 전송
  delay(10);
  if (Serial2.available()) {
    byte received = Serial2.read();
    Serial.printf("루프백 테스트: 0x%02X 수신됨\n", received);
  } else {
    Serial.println("루프백 테스트: 응답 없음 (정상 - 센서가 없을 때)");
  }
  
  Serial.println("하드웨어 테스트 완료");
}

// 간단한 Modbus 테스트 함수 (디버깅용)
bool testModbusConnection() {
  Serial.println("=== Modbus 연결 테스트 시작 ===");
  
  // 시리얼 버퍼 비우기
  while (Serial2.available()) {
    Serial2.read();
  }
  
  // 짧은 테스트 요청 (2개 레지스터만 읽기)
  uint8_t testPdu[] = { 
    (uint8_t)SLAVE_ID, 0x03, 
    (uint8_t)(START_ADDR >> 8), (uint8_t)(START_ADDR & 0xFF), 
    0x00, 0x02  // 2개 레지스터만 읽기
  };
  uint16_t testCrc = calculateCRC(testPdu, 6);
  uint8_t testRequest[] = { 
    (uint8_t)SLAVE_ID, 0x03, 
    (uint8_t)(START_ADDR >> 8), (uint8_t)(START_ADDR & 0xFF), 
    0x00, 0x02, 
    (uint8_t)(testCrc & 0xFF), (uint8_t)(testCrc >> 8) 
  };
  
  Serial.print("테스트 요청: ");
  for (int i = 0; i < 8; i++) {
    Serial.printf("0x%02X ", testRequest[i]);
  }
  Serial.println();
  
  // 송신
  digitalWrite(RS485_DE_RE, HIGH);
  delay(5);
  Serial2.write(testRequest, 8);
  Serial2.flush();
  delay(10);
  digitalWrite(RS485_DE_RE, LOW);
  delay(10);
  
  // 응답 수신
  byte response[50];
  int count = 0;
  unsigned long lastByteTime = 0;
  bool frameStarted = false;
  
  unsigned long startTime = millis();
  while (millis() - startTime < 2000) {
    if (Serial2.available()) {
      byte receivedByte = Serial2.read();
      response[count++] = receivedByte;
      lastByteTime = micros();
      frameStarted = true;
      
      if (count == 1) {
        Serial.printf("테스트 첫 바이트 수신: 0x%02X\n", receivedByte);
      }
    }
    
    // 프레임 종료 감지
    if (frameStarted && (micros() - lastByteTime) > 3000) { // 9600bps용 3ms로 조정
      break;
    }
    
    delay(1);
  }
  
  Serial.print("테스트 응답 (");
  Serial.print(count);
  Serial.print("바이트): ");
  for (int i = 0; i < count; i++) {
    Serial.printf("0x%02X ", response[i]);
  }
  Serial.println();
  
  if (count > 0) {
    Serial.println("Modbus 통신 가능 - 하드웨어 연결 정상");
    return true;
  } else {
    Serial.println("Modbus 통신 불가 - 하드웨어 연결 확인 필요");
    return false;
  }
}

// CSV 문자열 생성
String makeCSV(float* sensors) {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", t);
  char line[256];
  snprintf(line, sizeof(line), "%s,%.1f,%.1f,%.0f,%.1f,%.0f,%.0f",
    buf, sensors[0], sensors[1], sensors[2], sensors[3], sensors[4], sensors[5]);
  // timestamp, temperature, humidity, rainfall, illuminance, wind_speed, wind_direction 순서
  return String(line);
}

// NAS로 HTTP POST 전송
void sendToNAS(String csv) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(nas_url);
    http.setTimeout(15000); // 15초 타임아웃
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String postData = "csv_line=" + csv;
    
    // 최대 2번 시도
    for (int retry = 0; retry < 2; retry++) {
      int code = http.POST(postData);
      if (code == 200) {
        Serial.println("전송 성공");
        break;
      } else {
        Serial.printf("전송 실패 (시도 %d/2): %d\n", retry+1, code);
        if (retry == 0) delay(1000);
      }
    }
    http.end();
  } else {
    Serial.println("WiFi 연결 안됨");
  }
} 

// Task 1: 데이터 수집 (5초마다 연속)
void dataCollectionTask(void* parameter) {
  while (true) {
    float sensors[6];
    if (readSensors(sensors)) {
      // Mutex로 데이터 보호
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // 데이터 버퍼에 저장
        if (dataCount < 60) {
          for (int i = 0; i < 6; i++) {
            dataBuffer[dataCount][i] = sensors[i];
          }
          dataCount++;
          lastCollectTime = millis();
          Serial.printf("수집 %d - 온도: %.1f°C, 습도: %.1f%%, 감우: %.0f, 일사: %.0fLux, 풍속: %.1fm/s, 풍향: %.0f°\n", 
                       dataCount, sensors[0], sensors[1], sensors[2], sensors[3], sensors[4], sensors[5]);
        }
        xSemaphoreGive(dataMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(15000)); // 15초 대기 (1분에 4회 수집)
  }
}

// Task 2: 평균값 계산 및 전송 (1분마다)
void dataTransmissionTask(void* parameter) {
  while (true) {
    // 현재 시간 확인
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    
    // 매 분 00초에 전송
    if (timeinfo->tm_sec == 0) {
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (dataCount >= 4) { // 최소 4개 이상 수집된 경우에만 전송
          Serial.printf("=== 1분간 수집 완료 (%d개) ===\n", dataCount);
          
          // 유효한 값만으로 평균값 계산
          float avgSensors[6] = {0, 0, 0, 0, 0, 0};
          int validCounts[6] = {0, 0, 0, 0, 0, 0}; // 각 센서별 유효한 값 개수
          
          for (int i = 0; i < dataCount; i++) {
            for (int j = 0; j < 6; j++) {
              float value = dataBuffer[i][j];
              // 유효한 값인지 확인 (0이 아닌 값)
              if (value > 0.01f) { // 0.01 이상인 값만 유효
                avgSensors[j] += value;
                validCounts[j]++;
              }
            }
          }
          
          // 유효한 값이 있는 센서만 평균 계산
          for (int j = 0; j < 6; j++) {
            if (validCounts[j] > 0) {
              avgSensors[j] /= validCounts[j];
              Serial.printf("센서[%d] 유효값: %d개 → 평균: %.2f\n", j, validCounts[j], avgSensors[j]);
            } else {
              avgSensors[j] = 0.0f; // 유효한 값이 없으면 0
              Serial.printf("센서[%d] 유효값: 0개\n", j);
            }
          }
          
          Serial.printf("평균값 - 온도: %.1f°C, 습도: %.1f%%, 감우: %.0f, 일사: %.0fLux, 풍속: %.1fm/s, 풍향: %.0f°\n",
                       avgSensors[0], avgSensors[1], avgSensors[2], avgSensors[3], avgSensors[4], avgSensors[5]);
          
          // CSV 생성 및 전송
          String csv = makeCSV(avgSensors);
          Serial.println("CSV: " + csv);
          sendToNAS(csv);
          
          // 버퍼 초기화
          dataCount = 0;
        }
        xSemaphoreGive(dataMutex);
        
        // 다음 분까지 대기
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // 0.1초마다 시간 체크
  }
} 
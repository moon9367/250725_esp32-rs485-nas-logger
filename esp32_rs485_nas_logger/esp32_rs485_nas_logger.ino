#include <ModbusMaster.h> // 라이브러리 설치 필요
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

// ====== 사용자 설정 ======
#define SLAVE_ID 5
#define RS485_TX 17
#define RS485_RX 16
#define RS485_DE_RE 4
#define BAUDRATE 9600  // 테스트용: 9600bps로 변경
#define SERIAL_MODE SERIAL_8N1  // 패리티 설정 (8N1 또는 8E1)

// Modbus 읽기 시작 주소와 레지스터 개수(사용자 설정)
#define START_ADDR 0x00CB  // 시작 주소: 203 (0x00CB)
#define REG_COUNT  0x0038  // 모든 센서: 56개 레지스터

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
  Serial.printf("Modbus 설정: Slave ID=%d, START_ADDR=0x%04X, REG_COUNT=0x%04X\n", SLAVE_ID, START_ADDR, REG_COUNT);

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
  
  // Modbus 연결 테스트
  delay(2000); // 2초 대기 후 테스트
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

// Modbus로 센서 데이터 읽기 - 견고한 프레임 처리
bool readSensors(float* sensors) {
  Serial.println("Modbus 요청 시작...");
  
  // 시리얼 버퍼 완전 비우기
  while (Serial2.available()) {
    Serial2.read();
  }
  
  // Modbus RTU 요청 프레임 구성 (동적 CRC)
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
  delay(2); // 송신 모드 전환 대기
  Serial2.write(request, 8);
  Serial2.flush();
  delay(10); // 전송 완료 대기
  digitalWrite(RS485_DE_RE, LOW);
  delay(5); // 수신 모드 전환 대기
  
  // RS485 라인 안정화 및 센서 응답 준비 시간
  delay(50);
  
  Serial.println("Modbus 요청 전송 완료");
  
  // 응답 수신 - 갭 타임아웃 기반 프레임 종료 감지
  byte response[300]; // 충분한 버퍼 크기
  int responseIndex = 0;
  unsigned long lastByteTime = 0;
  bool frameStarted = false;
  
  // 기대 응답 길이 계산
  const int expectedTotal = 5 + (2 * REG_COUNT); // addr(1)+func(1)+byteCount(1)+data(2*REG_COUNT)+CRC(2)
  
  unsigned long startTime = millis();
  unsigned long firstByteTimeout = millis() + 1000; // 첫 바이트는 1초 내에 와야 함
  
  while (millis() - startTime < 5000) { // 5초로 증가
    if (Serial2.available()) {
      byte receivedByte = Serial2.read();
      response[responseIndex++] = receivedByte;
      lastByteTime = micros();
      frameStarted = true;
      
      if (responseIndex == 1) {
        Serial.printf("첫 바이트 수신: 0x%02X\n", receivedByte);
        firstByteTimeout = millis() + 2000; // 첫 바이트 받으면 추가 2초 대기
      }
      
      // 버퍼 오버플로우 방지
      if (responseIndex >= 300) {
        Serial.println("응답 버퍼 오버플로우");
        break;
      }
    }
    
    // 첫 바이트 타임아웃 체크
    if (!frameStarted && millis() > firstByteTimeout) {
      Serial.printf("첫 바이트 타임아웃 (대기시간: %lu ms)\n", millis() - startTime);
      break;
    }
    
    // 프레임 종료 감지 (3.5문자 시간 대기)
    if (frameStarted && (micros() - lastByteTime) > 3000) { // 9600bps용 3ms로 조정
      Serial.println("프레임 종료 감지");
      break;
    }
    
    delay(1); // CPU 부하 방지
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
    
    // 이상한 응답 필터링 (너무 짧거나 잘못된 응답)
    if (responseIndex < 5) {
      Serial.println("응답이 너무 짧음 - 무시");
      return false;
    }
    
    // 전체 응답 데이터 출력 (디버깅용)
    Serial.println("전체 응답 데이터:");
    for (int i = 0; i < responseIndex; i++) {
      Serial.printf("%02X ", response[i]);
      if ((i + 1) % 16 == 0) Serial.println();
    }
    Serial.println();
    
    // 센서 데이터 직접 파싱 (비표준 프로토콜 대응)
    int startIndex = -1;
    
    // 선행 0x00들을 건너뛰고 실제 데이터 시작점 찾기
    for (int i = 0; i < responseIndex - 4; i++) {
      if (response[i] != 0x00) {
        startIndex = i;
        Serial.printf("센서 데이터 시작 위치 발견: %d (첫 바이트: 0x%02X)\n", startIndex, response[i]);
        break;
      }
    }
    
    // 표준 Modbus 프레임도 시도
    if (startIndex == -1) {
      for (int i = 0; i <= responseIndex - 5; i++) {
        if (response[i] == (uint8_t)SLAVE_ID && response[i+1] == 0x03) {
          uint8_t byteCount = response[i+2];
          int total = 3 + byteCount + 2;
          
          if (i + total <= responseIndex) {
            uint16_t calcCrc = calculateCRC(&response[i], 3 + byteCount);
            uint16_t recvCrc = (uint16_t)response[i + 3 + byteCount] | 
                              ((uint16_t)response[i + 3 + byteCount + 1] << 8);
            
            if (calcCrc == recvCrc) {
              startIndex = i;
              Serial.printf("표준 Modbus 프레임 발견: 위치 %d, byteCount=%d\n", startIndex, byteCount);
              break;
            }
          }
        }
      }
    }
    
    if (startIndex >= 0) {
      Serial.printf("Modbus 응답 시작 위치: %d\n", startIndex);
      Serial.printf("시작 위치 데이터: 0x%02X 0x%02X 0x%02X\n", 
                   response[startIndex], response[startIndex+1], response[startIndex+2]);
      
      // 센서 데이터 파싱 (비표준 프로토콜)
      Serial.println("센서 데이터 파싱 시작...");
      
      // 표준 Modbus인지 확인
      if (response[startIndex] == (uint8_t)SLAVE_ID && response[startIndex+1] == 0x03) {
        // 표준 Modbus 파싱 - 모든 센서
        uint8_t byteCount = response[startIndex+2];
        if (byteCount >= 4) {
          Serial.printf("바이트 카운트: %d, 예상 센서 개수: %d\n", byteCount, byteCount/4);
          
          // 모든 센서 데이터 파싱 (4바이트씩)
          int sensorCount = min(byteCount/4, 6); // 최대 6개 센서
          for (int i = 0; i < sensorCount; i++) {
            int base = startIndex + 3 + i * 4;
            uint16_t highWord = ((uint16_t)response[base] << 8) | response[base + 1];
            uint16_t lowWord = ((uint16_t)response[base + 2] << 8) | response[base + 3];
            
            // 다양한 방법으로 테스트
            float value1 = regsToFloatBE(highWord, lowWord);
            float value2 = regsToFloatBE(lowWord, highWord);
            uint16_t swappedHigh = ((highWord & 0xFF) << 8) | ((highWord >> 8) & 0xFF);
            uint16_t swappedLow = ((lowWord & 0xFF) << 8) | ((lowWord >> 8) & 0xFF);
            float value3 = regsToFloatBE(swappedHigh, swappedLow);
            float value4 = regsToFloatBE(swappedLow, swappedHigh);
            
            // 원시 바이트로 직접 해석
            uint32_t rawData = ((uint32_t)response[base] << 24) | ((uint32_t)response[base+1] << 16) | 
                              ((uint32_t)response[base+2] << 8) | response[base+3];
            float value5 = *((float*)&rawData);
            
            Serial.printf("센서[%d] 원본: 0x%04X 0x%04X\n", i, highWord, lowWord);
            Serial.printf("  방법1: %.2f, 방법2: %.2f, 방법3: %.2f, 방법4: %.2f, 방법5: %.2f\n", 
                         value1, value2, value3, value4, value5);
            
            // 온도 센서는 합리적인 범위(0~50도)에서 선택
            float temperatures[] = {value1, value2, value3, value4, value5};
            float value = value3; // 기본값
            for (int j = 0; j < 5; j++) {
              if (temperatures[j] >= 0 && temperatures[j] <= 50) {
                value = temperatures[j];
                Serial.printf("  선택된 방법: %d (%.2f)\n", j+1, value);
                break;
              }
            }
            
            sensors[i] = value;
          }
        }
      } else {
        // 비표준 프로토콜 - 직접 데이터 파싱
        // 응답에서 연속된 float 데이터 추출 (4바이트씩)
        int dataStart = startIndex;
        int availableBytes = responseIndex - dataStart;
        
        Serial.printf("비표준 프로토콜 파싱: 시작위치=%d, 사용가능바이트=%d\n", dataStart, availableBytes);
        
        // 모든 센서 파싱 (비표준 프로토콜)
        int sensorCount = min(availableBytes/4, 6); // 최대 6개 센서
        Serial.printf("비표준 프로토콜 파싱: 시작위치=%d, 사용가능바이트=%d, 센서개수=%d\n", 
                     dataStart, availableBytes, sensorCount);
        
        for (int i = 0; i < sensorCount; i++) {
          int base = dataStart + i * 4;
          uint16_t highWord = ((uint16_t)response[base] << 8) | response[base + 1];
          uint16_t lowWord = ((uint16_t)response[base + 2] << 8) | response[base + 3];
          
          // 다양한 방법으로 테스트
          float value1 = regsToFloatBE(highWord, lowWord);
          float value2 = regsToFloatBE(lowWord, highWord);
          uint16_t swappedHigh = ((highWord & 0xFF) << 8) | ((highWord >> 8) & 0xFF);
          uint16_t swappedLow = ((lowWord & 0xFF) << 8) | ((lowWord >> 8) & 0xFF);
          float value3 = regsToFloatBE(swappedHigh, swappedLow);
          float value4 = regsToFloatBE(swappedLow, swappedHigh);
          
          // 원시 바이트로 직접 해석
          uint32_t rawData = ((uint32_t)response[base] << 24) | ((uint32_t)response[base+1] << 16) | 
                            ((uint32_t)response[base+2] << 8) | response[base+3];
          float value5 = *((float*)&rawData);
          
          Serial.printf("센서[%d](비표준) 원본: 0x%04X 0x%04X\n", i, highWord, lowWord);
          Serial.printf("  방법1: %.2f, 방법2: %.2f, 방법3: %.2f, 방법4: %.2f, 방법5: %.2f\n", 
                       value1, value2, value3, value4, value5);
          
          // 온도 센서는 합리적인 범위(0~50도)에서 선택
          float temperatures[] = {value1, value2, value3, value4, value5};
          float value = value3; // 기본값
          for (int j = 0; j < 5; j++) {
            if (temperatures[j] >= 0 && temperatures[j] <= 50) {
              value = temperatures[j];
              Serial.printf("  선택된 방법: %d (%.2f)\n", j+1, value);
              break;
            }
          }
          
          sensors[i] = value;
        }
      }
      
      Serial.println("센서 데이터 파싱 성공!");
      return true;
    } else {
      Serial.println("유효한 데이터 프레임을 찾을 수 없음");
      if (responseIndex > 2) {
        Serial.printf("첫 3바이트: 0x%02X 0x%02X 0x%02X\n", response[0], response[1], response[2]);
      }
      Serial.println("비표준 프로토콜 파싱 시도 중...");
      
      // 강제로 12번째 위치부터 파싱 시도 (실제 데이터 위치)
      if (responseIndex >= 16) {
        int forceStart = 12;
        Serial.printf("강제 파싱 시도: 위치 %d부터\n", forceStart);
        
        // 모든 센서 강제 파싱
        int sensorCount = min((responseIndex - forceStart)/4, 6); // 최대 6개 센서
        Serial.printf("강제 파싱 시도: 위치 %d부터, 센서개수=%d\n", forceStart, sensorCount);
        
        for (int i = 0; i < sensorCount; i++) {
          int base = forceStart + i * 4;
          if ((base + 3) <= responseIndex) {
            uint16_t highWord = ((uint16_t)response[base] << 8) | response[base + 1];
            uint16_t lowWord = ((uint16_t)response[base + 2] << 8) | response[base + 3];
            
            // 다양한 방법으로 테스트
            float value1 = regsToFloatBE(highWord, lowWord);
            float value2 = regsToFloatBE(lowWord, highWord);
            uint16_t swappedHigh = ((highWord & 0xFF) << 8) | ((highWord >> 8) & 0xFF);
            uint16_t swappedLow = ((lowWord & 0xFF) << 8) | ((lowWord >> 8) & 0xFF);
            float value3 = regsToFloatBE(swappedHigh, swappedLow);
            float value4 = regsToFloatBE(swappedLow, swappedHigh);
            
            // 원시 바이트로 직접 해석
            uint32_t rawData = ((uint32_t)response[base] << 24) | ((uint32_t)response[base+1] << 16) | 
                              ((uint32_t)response[base+2] << 8) | response[base+3];
            float value5 = *((float*)&rawData);
            
            Serial.printf("센서[%d](강제) 원본: 0x%04X 0x%04X\n", i, highWord, lowWord);
            Serial.printf("  방법1: %.2f, 방법2: %.2f, 방법3: %.2f, 방법4: %.2f, 방법5: %.2f\n", 
                         value1, value2, value3, value4, value5);
            
            // 온도 센서는 합리적인 범위(0~50도)에서 선택
            float temperatures[] = {value1, value2, value3, value4, value5};
            float value = value3; // 기본값
            for (int j = 0; j < 5; j++) {
              if (temperatures[j] >= 0 && temperatures[j] <= 50) {
                value = temperatures[j];
                Serial.printf("  선택된 방법: %d (%.2f)\n", j+1, value);
                break;
              }
            }
            
            sensors[i] = value;
          }
        }
        Serial.println("강제 파싱 완료!");
        return true;
      }
    }
  } else {
    Serial.println("Modbus 응답 없음");
  }
  
  return false;
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
  snprintf(line, sizeof(line), "%s,%.1f,%.1f,%.1f,%.0f,%.1f,%.0f",
    buf, sensors[0], sensors[1], sensors[2], sensors[3], sensors[4], sensors[5]);
  // timestamp, temperature, humidity, rainfall, solar, wind_speed, wind_direction 순서
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
          Serial.printf("수집 %d - 온도: %.1f°C, 습도: %.1f%%, 강우: %.1fmm, 일사: %.0fW/m², 풍속: %.1fm/s, 풍향: %.0f°\n", 
                       dataCount, sensors[0], sensors[1], sensors[2], sensors[3], sensors[4], sensors[5]);
        }
        xSemaphoreGive(dataMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5000)); // 5초 대기 (원래 성공했던 설정)
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
        if (dataCount > 0) {
          Serial.printf("=== 1분간 수집 완료 (%d개) ===\n", dataCount);
          
          // 평균값 계산 (모든 센서)
          float avgSensors[6] = {0, 0, 0, 0, 0, 0};
          for (int i = 0; i < dataCount; i++) {
            for (int j = 0; j < 6; j++) {
              avgSensors[j] += dataBuffer[i][j];
            }
          }
          for (int j = 0; j < 6; j++) {
            avgSensors[j] /= dataCount;
          }
          
          Serial.printf("평균값 - 온도: %.1f°C, 습도: %.1f%%, 강우: %.1fmm, 일사: %.0fW/m², 풍속: %.1fm/s, 풍향: %.0f°\n",
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
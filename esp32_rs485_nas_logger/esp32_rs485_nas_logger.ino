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

// 센서 레지스터 주소 설정 (190부터 96개 레지스터 읽기)
#define REG_START_ADDR 191  // 전체 레지스터 시작 주소
#define REG_COUNT 96         // 전체 레지스터 길이 (96개)

// 각 센서의 정확한 레지스터 주소 (실제 데이터 위치 기반)
#define TEMP_REG_ADDR 203    // 온도 레지스터 주소 (203 또는 192)
#define HUMID_REG_ADDR 212   // 습도 레지스터 주소 (212 또는 201)
#define RAIN_REG_ADDR 218   // 감우 레지스터 주소 (실제 위치)
#define SOLAR_REG_ADDR 227   // 일사 레지스터 주소
#define WIND_DIR_REG_ADDR 230 // 풍향 레지스터 주소
#define WIND_SPEED_REG_ADDR 233 // 풍속 레지스터 주소


const char* ssid = "TSPOL"; // 와이파이 이름 입력력
const char* password = "mms56529983"; // 비밀번호 입력
const char* nas_url = "http://tspol.iptime.org:8888/rs485/upload.php"; // NAS IP로 수정
// =========================

ModbusMaster node;

// 전역 변수 (모든 센서 데이터)
volatile float temperature = 0.0f;
volatile float humidity = 0.0f;
volatile float rain = 0.0f;
volatile float solar = 0.0f;
volatile float windSpeed = 0.0f;
volatile float windDirection = 0.0f;
volatile bool newDataAvailable = false;
SemaphoreHandle_t dataMutex;

// 수집된 데이터 저장용 (모든 센서)
float tempBuffer[60]; // 최대 60개 온도 데이터 저장
float humidBuffer[60]; // 최대 60개 습도 데이터 저장
float rainBuffer[60]; // 최대 60개 감우 데이터 저장
float solarBuffer[60]; // 최대 60개 일사 데이터 저장
float windSpeedBuffer[60]; // 최대 60개 풍속 데이터 저장
float windDirBuffer[60]; // 최대 60개 풍향 데이터 저장
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

  Serial.println("ESP32 Temperature Logger Start");
  Serial.printf("RS485: TX=%d, RX=%d, DE/RE=%d, Baud=%d\n", RS485_TX, RS485_RX, RS485_DE_RE, BAUDRATE);
  Serial.printf("Modbus: Slave ID=%d\n", SLAVE_ID);
  Serial.printf("  TEMP_ADDR=%d, HUMID_ADDR=%d\n", TEMP_REG_ADDR, HUMID_REG_ADDR);
  Serial.printf("  RAIN_ADDR=%d, SOLAR_ADDR=%d\n", RAIN_REG_ADDR, SOLAR_REG_ADDR);
  Serial.printf("  WIND_SPEED_ADDR=%d, WIND_DIR_ADDR=%d\n", WIND_SPEED_REG_ADDR, WIND_DIR_REG_ADDR);
  
  // RS485 설정 완료
  Serial.println("RS485 setup complete");

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
  
  Serial.println("=== System initialization complete ===");
  
  // 시스템 준비 완료
  delay(2000);
  
  
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
  
  Serial.println("=== Multi-tasking start ===");
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

// IEEE 754 Float 변환 함수 (요구사항 준수)
// 2바이트(16비트)씩 끊어 → 1 워드로 인식
// 한 워드 내 바이트 순서는 "빅 엔디언" → 0x71 0xD0 → 0x71D0
// 워드 2개씩 묶어서 → 4바이트(32비트)
// 워드 간 순서는 "리틀 엔디언" → 워드[1] + 워드[0]
// 각 4바이트를 IEEE 754 float (32비트 부동소수점) 으로 해석
float wordsToFloat(uint16_t word0, uint16_t word1) {
  // word0, word1은 이미 빅 엔디언으로 해석된 워드
  // 리틀 엔디언 워드 순서: [word1][word0] → word1이 상위워드, word0이 하위워드
  uint32_t raw = ((uint32_t)word1 << 16) | (uint32_t)word0;
  float f;
  memcpy(&f, &raw, 4);
  return f;
}


// 개별 센서 레지스터 읽기 함수는 현재 사용하지 않음 (연속 레지스터 읽기 사용)

// 온도 센서 데이터 읽기
bool readTemperature(float* temp) {
  return readSensorFromRegister(temp, TEMP_REG_ADDR, "Temperature", 15.0f, 45.0f);
}

// 습도 센서 데이터 읽기
bool readHumidity(float* humid) {
  return readSensorFromRegister(humid, HUMID_REG_ADDR, "Humidity", 30.0f, 90.0f);
}

// 감우 센서 데이터 읽기
bool readRain(float* rainValue) {
  // 218번 레지스터 먼저 시도
  if (readSensorFromRegister(rainValue, 218, "Rain", 0.0f, 100.0f)) {
    return true;
  }
  // 218번에 없으면 207번 시도
  if (readSensorFromRegister(rainValue, 207, "Rain", 0.0f, 100.0f)) {
    return true;
  }
  // 둘 다 없으면 실패
  return false;
}

// 일사 센서 데이터 읽기  
bool readSolar(float* solarValue) {
  return readSensorFromRegister(solarValue, SOLAR_REG_ADDR, "Solar", 0.0f, 2000.0f);
}

// 풍속 센서 데이터 읽기
bool readWindSpeed(float* windSpeedValue) {
  return readSensorFromRegister(windSpeedValue, WIND_SPEED_REG_ADDR, "WindSpeed", 0.0f, 50.0f);
}

// 풍향 센서 데이터 읽기
bool readWindDirection(float* windDirValue) {
  return readSensorFromRegister(windDirValue, WIND_DIR_REG_ADDR, "WindDirection", 0.0f, 400.0f);
}

// 전체 레지스터 데이터를 한 번에 읽고 저장하는 전역 변수
float allRegisters[96]; // 96개 레지스터 데이터 저장
bool registersRead = false;

// 전체 레지스터 읽기 함수
bool readAllRegisters() {
  Serial.println("=== Reading all registers 190-285 (96 registers) ===");
  
  // 시리얼 버퍼 완전 비우기
  while (Serial2.available()) {
    Serial2.read();
  }
  
  // Modbus RTU 요청 프레임 구성 (190부터 96개 레지스터)
  uint8_t requestPdu[] = { 
    (uint8_t)SLAVE_ID, 0x03, 
    (uint8_t)(REG_START_ADDR >> 8), (uint8_t)(REG_START_ADDR & 0xFF), 
    (uint8_t)(REG_COUNT >> 8), (uint8_t)(REG_COUNT & 0xFF) 
  };
  uint16_t reqCrc = calculateCRC(requestPdu, 6);
  uint8_t request[] = { 
    (uint8_t)SLAVE_ID, 0x03, 
    (uint8_t)(REG_START_ADDR >> 8), (uint8_t)(REG_START_ADDR & 0xFF), 
    (uint8_t)(REG_COUNT >> 8), (uint8_t)(REG_COUNT & 0xFF), 
    (uint8_t)(reqCrc & 0xFF), (uint8_t)(reqCrc >> 8) 
  };
  
  // 송신 모드 전환 및 전송
  digitalWrite(RS485_DE_RE, HIGH);
  delay(5);
  Serial2.write(request, 8);
  Serial2.flush();
  delay(20);
  digitalWrite(RS485_DE_RE, LOW);
  delay(10);
  delay(100);
  
  // 응답 수신
  byte response[500];
  int responseIndex = 0;
  unsigned long lastByteTime = 0;
  bool frameStarted = false;
  
  unsigned long startTime = millis();
  unsigned long firstByteTimeout = millis() + 5000;
  
  while (millis() - startTime < 8000) {
    if (Serial2.available()) {
      byte receivedByte = Serial2.read();
      response[responseIndex++] = receivedByte;
      lastByteTime = micros();
      frameStarted = true;
      
      if (responseIndex == 1) {
        firstByteTimeout = millis() + 8000;
      }
      
      if (responseIndex >= 500) {
        break;
      }
    }
    
    if (!frameStarted && millis() > firstByteTimeout) {
      break;
    }
    
    if (frameStarted && (micros() - lastByteTime) > 5000) {
      break;
    }
    
    delay(1);
  }
  
  if (responseIndex > 0) {
    // 데이터 파싱
    int dataStartPos = 3; // 표준 Modbus 응답
    if (responseIndex >= 3 && response[1] == 0x03) {
      // 표준 응답
    } else {
      dataStartPos = 6; // 비표준 응답
    }
    
    int dataLength = responseIndex - dataStartPos - 2;  // 마지막 2바이트는 CRC
    
    // 모든 레지스터 값을 Float로 변환하여 저장 및 출력
    Serial.println("=== All Float values from registers 190-285 ===");
    for (int i = 0; i < 96 && i * 4 + 4 <= dataLength; i++) {
      uint16_t word0 = (response[dataStartPos + i * 4] << 8) | response[dataStartPos + i * 4 + 1];
      uint16_t word1 = (response[dataStartPos + i * 4 + 2] << 8) | response[dataStartPos + i * 4 + 3];
      
      float value = wordsToFloat(word0, word1);
      allRegisters[i] = value;
      
      int regNum = REG_START_ADDR + i;
      Serial.printf("Reg[%d]=%04X %04X → %.2f", regNum, word0, word1, value);
      
      // 센서 범위 표시
      if (value != 0.0f && value > -1000.0f && value < 10000.0f) {
        if (value >= 15.0f && value <= 45.0f) {
          Serial.print(" (TEMP!)");
        } else if (value >= 30.0f && value <= 90.0f) {
          Serial.print(" (HUMID!)");
        } else if (value >= 0.0f && value <= 100.0f) {
          Serial.print(" (RAIN!)");
        } else if (value >= 0.0f && value <= 2000.0f) {
          Serial.print(" (SOLAR!)");
        } else if (value >= 0.0f && value <= 50.0f) {
          Serial.print(" (WIND_SPEED!)");
        } else if (value >= 0.0f && value <= 400.0f) {
          Serial.print(" (WIND_DIR!)");
        } else {
          Serial.print(" (OTHER)");
        }
      }
      Serial.println();
    }
    
    registersRead = true;
    Serial.println("=== All registers read complete ===");
    return true;
  }
  
  Serial.println("No register data received");
  return false;
}

// 저장된 레지스터 데이터에서 특정 센서 값 읽기
bool readSensorFromRegister(float* sensorValue, int regAddr, const char* sensorName, float minValue, float maxValue) {
  Serial.printf("=== %s sensor read start (Reg[%d]) ===\n", sensorName, regAddr);
  
  // 레지스터 데이터가 읽혀졌는지 확인
  if (!registersRead) {
    Serial.printf("Register data not available for %s\n", sensorName);
    return false;
  }
  
  // 절대 주소에서 상대 위치 계산 (regAddr - REG_START_ADDR)
  int regOffset = regAddr - REG_START_ADDR;
  if (regOffset >= 0 && regOffset < 96) {
    float value = allRegisters[regOffset];
    Serial.printf("Reg[%d] → %.2f", regAddr, value);
    
    // 센서별 범위 검사
    if (value >= minValue && value <= maxValue && value != 0.0f) {
      *sensorValue = value;
      Serial.printf(" (%s!)", sensorName);
      Serial.println();
      Serial.printf("%s read success: %.2f\n", sensorName, *sensorValue);
      return true;
    } else {
      Serial.printf(" (out_of_range_%.1f~%.1f)", minValue, maxValue);
      Serial.println();
    }
  }
  
  Serial.printf("No %s data found\n", sensorName);
  return false;
}

// 기존 함수 (사용하지 않음)
bool readSensorData(float* sensorValue, uint16_t startAddr, uint16_t regCount, const char* sensorName, float minValue, float maxValue) {
  Serial.printf("=== %s sensor read start ===\n", sensorName);
  
  // 시리얼 버퍼 완전 비우기
  while (Serial2.available()) {
    Serial2.read();
  }
  
  // Modbus RTU 요청 프레임 구성
  uint8_t requestPdu[] = { 
    (uint8_t)SLAVE_ID, 0x03, 
    (uint8_t)(startAddr >> 8), (uint8_t)(startAddr & 0xFF), 
    (uint8_t)(regCount >> 8), (uint8_t)(regCount & 0xFF) 
  };
  uint16_t reqCrc = calculateCRC(requestPdu, 6);
  uint8_t request[] = { 
    (uint8_t)SLAVE_ID, 0x03, 
    (uint8_t)(startAddr >> 8), (uint8_t)(startAddr & 0xFF), 
    (uint8_t)(regCount >> 8), (uint8_t)(regCount & 0xFF), 
    (uint8_t)(reqCrc & 0xFF), (uint8_t)(reqCrc >> 8) 
  };
  
  // 전송할 요청 데이터 출력
  Serial.printf("Send %s request: ", sensorName);
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
  
  Serial.printf("%s Modbus request sent\n", sensorName);
  
  // 응답 수신
  byte response[500];
  int responseIndex = 0;
  unsigned long lastByteTime = 0;
  bool frameStarted = false;
  
  unsigned long startTime = millis();
  unsigned long firstByteTimeout = millis() + 5000;
  
  Serial.printf("Waiting for %s response...\n", sensorName);
  
  while (millis() - startTime < 8000) {
    if (Serial2.available()) {
      byte receivedByte = Serial2.read();
      response[responseIndex++] = receivedByte;
      lastByteTime = micros();
      frameStarted = true;
      
      if (responseIndex == 1) {
        Serial.printf("First %s byte received: 0x%02X\n", sensorName, receivedByte);
        firstByteTimeout = millis() + 8000;
      }
      
      if (responseIndex >= 500) {
        Serial.printf("%s response buffer overflow - data may be truncated\n", sensorName);
        break;
      }
    }
    
    if (!frameStarted && millis() > firstByteTimeout) {
      Serial.printf("%s first byte timeout (wait time: %lu ms)\n", sensorName, millis() - startTime);
      break;
    }
    
    if (frameStarted && (micros() - lastByteTime) > 5000) {
      Serial.printf("%s frame end detected (5ms no response)\n", sensorName);
      break;
    }
    
    delay(1);
  }
  
  Serial.printf("%s response receive complete: %d bytes (receive time: %lu ms)\n", sensorName, responseIndex, millis() - startTime);
  
  if (responseIndex > 0) {
    // 데이터 파싱
    int dataStartPos = 3; // 표준 Modbus 응답
    if (responseIndex >= 3 && response[1] == 0x03) {
      // 표준 응답
    } else {
      dataStartPos = 6; // 비표준 응답
    }
    
    int dataLength = responseIndex - dataStartPos - 2;  // 마지막 2바이트는 CRC
    
    if (dataLength < 4) {
      Serial.printf("%s insufficient data length (minimum 4 bytes required)\n", sensorName);
      return false;
    }
    
    // 첫 번째 워드 쌍만 Float로 변환하여 센서 값 찾기
    Serial.printf("=== %s first word pair Float conversion ===\n", sensorName);
    float foundValue = 0.0f;
    bool valueFound = false;
    
    // 첫 번째 워드 쌍만 읽기 (Word[0,1])
    if (dataLength >= 4) {
      uint16_t word0 = (response[dataStartPos + 0] << 8) | response[dataStartPos + 1];
      uint16_t word1 = (response[dataStartPos + 2] << 8) | response[dataStartPos + 3];
      
      float value = wordsToFloat(word0, word1);
      Serial.printf("Word[0,1]=%04X %04X → %.2f", word0, word1, value);
      
      // 센서별 범위 검사 - 현재 센서 범위만 확인
      if (value >= minValue && value <= maxValue && value != 0.0f) {
        foundValue = value;
        valueFound = true;
        Serial.printf(" (%s!)", sensorName);
      } else {
        // 유효한 범위의 값만 표시 (디버깅용)
        if (value != 0.0f && value > -1000.0f && value < 10000.0f) {
          Serial.printf(" (out_of_range_%.1f~%.1f)", minValue, maxValue);
        }
      }
      Serial.println();
    }
    
    if (valueFound) {
      *sensorValue = foundValue;
      Serial.printf("%s read success: %.2f\n", sensorName, *sensorValue);
      return true;
    } else {
      Serial.printf("%s value not found (%.1f~%.1f range)\n", sensorName, minValue, maxValue);
      *sensorValue = 0.0f;
      return false;
    }
  }
  
  Serial.printf("No %s Modbus response\n", sensorName);
  return false;
}

// CSV 문자열 생성 (모든 센서)
String makeCSV(float temp, float humid, float rain, float solar, float windSpeed, float windDir) {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", t);
  char line[256];
  snprintf(line, sizeof(line), "%s,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f",
    buf, temp, humid, rain, solar, windSpeed, windDir);
  // timestamp, temperature, humidity, rain, solar, windSpeed, windDirection 순서
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
        Serial.println("Upload success");
        break;
      } else {
        Serial.printf("Upload failed (attempt %d/2): %d\n", retry+1, code);
        if (retry == 0) delay(1000);
      }
    }
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
} 


// Task 1: All sensor data collection (every 10 seconds)
void dataCollectionTask(void* parameter) {
  while (true) {
    // 먼저 전체 레지스터 읽기
    readAllRegisters();
    
    float temp, humid, rain, solar, windSpeed, windDir;
    
    // 모든 센서 데이터 읽기 (저장된 레지스터 데이터에서)
    bool tempSuccess = readTemperature(&temp);
    bool humidSuccess = readHumidity(&humid);
    bool rainSuccess = readRain(&rain);
    bool solarSuccess = readSolar(&solar);
    bool windSpeedSuccess = readWindSpeed(&windSpeed);
    bool windDirSuccess = readWindDirection(&windDir);
    
    // 최소 하나 이상의 센서에서 데이터를 읽었으면 저장
    if (tempSuccess || humidSuccess || rainSuccess || solarSuccess || windSpeedSuccess || windDirSuccess) {
      // Mutex로 데이터 보호
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // 데이터 버퍼에 저장
        if (dataCount < 60) {
          tempBuffer[dataCount] = tempSuccess ? temp : 0.0f;
          humidBuffer[dataCount] = humidSuccess ? humid : 0.0f;
          rainBuffer[dataCount] = rainSuccess ? rain : 0.0f;
          solarBuffer[dataCount] = solarSuccess ? solar : 0.0f;
          windSpeedBuffer[dataCount] = windSpeedSuccess ? windSpeed : 0.0f;
          windDirBuffer[dataCount] = windDirSuccess ? windDir : 0.0f;
          dataCount++;
          lastCollectTime = millis();
          Serial.printf("Collect %d - Temp: %.1fC, Humid: %.1f%%, Rain: %.1f, Solar: %.1f, WindSpeed: %.1f, WindDir: %.1f\n", 
                       dataCount, temp, humid, rain, solar, windSpeed, windDir);
        }
        xSemaphoreGive(dataMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10000)); // 10초 대기
  }
}

// Task 2: Average calculation and upload (every minute)
void dataTransmissionTask(void* parameter) {
  while (true) {
    // 현재 시간 확인
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    
    // 매 분 00초에 전송
    if (timeinfo->tm_sec == 0) {
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (dataCount >= 3) { // 최소 3개 이상 수집된 경우에만 전송
          Serial.printf("=== 1min collection complete (%d samples) ===\n", dataCount);
          
          // 모든 센서의 유효한 값만으로 평균값 계산
          float avgTemp = 0.0f, avgHumid = 0.0f, avgRain = 0.0f, avgSolar = 0.0f, avgWindSpeed = 0.0f, avgWindDir = 0.0f;
          int tempValidCount = 0, humidValidCount = 0, rainValidCount = 0, solarValidCount = 0, windSpeedValidCount = 0, windDirValidCount = 0;
          
          for (int i = 0; i < dataCount; i++) {
            float tempValue = tempBuffer[i];
            float humidValue = humidBuffer[i];
            float rainValue = rainBuffer[i];
            float solarValue = solarBuffer[i];
            float windSpeedValue = windSpeedBuffer[i];
            float windDirValue = windDirBuffer[i];
            
            // 각 센서별 유효성 확인 및 합계 계산
            if (tempValue > 0.01f) {
              avgTemp += tempValue;
              tempValidCount++;
            }
            
            if (humidValue > 0.01f) {
              avgHumid += humidValue;
              humidValidCount++;
            }
            
            if (rainValue > 0.01f) {
              avgRain += rainValue;
              rainValidCount++;
            }
            
            if (solarValue > 0.01f) {
              avgSolar += solarValue;
              solarValidCount++;
            }
            
            if (windSpeedValue > 0.01f) {
              avgWindSpeed += windSpeedValue;
              windSpeedValidCount++;
            }
            
            if (windDirValue > 0.01f) {
              avgWindDir += windDirValue;
              windDirValidCount++;
            }
          }
          
          // 유효한 값이 있으면 평균 계산
          if (tempValidCount > 0 || humidValidCount > 0 || rainValidCount > 0 || 
              solarValidCount > 0 || windSpeedValidCount > 0 || windDirValidCount > 0) {
            
            if (tempValidCount > 0) avgTemp /= tempValidCount;
            if (humidValidCount > 0) avgHumid /= humidValidCount;
            if (rainValidCount > 0) avgRain /= rainValidCount;
            if (solarValidCount > 0) avgSolar /= solarValidCount;
            if (windSpeedValidCount > 0) avgWindSpeed /= windSpeedValidCount;
            if (windDirValidCount > 0) avgWindDir /= windDirValidCount;
            
            Serial.printf("Valid samples - Temp: %d → %.2fC, Humid: %d → %.2f%%, Rain: %d → %.2f\n", 
                         tempValidCount, avgTemp, humidValidCount, avgHumid, rainValidCount, avgRain);
            Serial.printf("                   Solar: %d → %.2f, WindSpeed: %d → %.2f, WindDir: %d → %.2f\n",
                         solarValidCount, avgSolar, windSpeedValidCount, avgWindSpeed, windDirValidCount, avgWindDir);
            
            // CSV 생성 및 전송
            String csv = makeCSV(avgTemp, avgHumid, avgRain, avgSolar, avgWindSpeed, avgWindDir);
            Serial.println("CSV: " + csv);
            sendToNAS(csv);
          } else {
            Serial.println("No valid sensor data");
          }
          
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
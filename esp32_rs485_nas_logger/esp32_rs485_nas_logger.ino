#include <ModbusMaster.h> // 라이브러리 설치 필요
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

// ====== 사용자 설정 ======
#define SLAVE_ID 1
#define RS485_TX 17
#define RS485_RX 16
#define RS485_DE_RE 4
#define BAUDRATE 4800  // 9600에서 4800으로 변경

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

  // RS485 시리얼 초기화
  Serial2.begin(BAUDRATE, SERIAL_8N2, RS485_RX, RS485_TX);
  node.begin(SLAVE_ID, Serial2);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  Serial.println("ESP32 RS485 NAS Logger 시작");
  Serial.printf("RS485 설정: TX=%d, RX=%d, DE/RE=%d, Baudrate=%d\n", RS485_TX, RS485_RX, RS485_DE_RE, BAUDRATE);
  Serial.printf("Modbus 설정: Slave ID=%d\n", SLAVE_ID);

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

// Modbus로 64워드(32 float) 읽기 - 실제 장비 프로토콜 대응
bool readSensors(float* sensors) {
  Serial.println("Modbus 요청 시작...");
  
  // 시리얼 버퍼 비우기
  while (Serial2.available()) {
    Serial2.read();
  }
  
  // 직접 Modbus RTU 요청 전송 (실제 장비 프로토콜)
  // 01 03 00 CB 00 40 35 C4 (CRC 수정됨)
  byte request[] = {0x01, 0x03, 0x00, 0xCB, 0x00, 0x40, 0x35, 0xC4}; // 시작주소 0x00CB, 64개 레지스터
  
  digitalWrite(RS485_DE_RE, HIGH); // 송신 모드
  delay(1);
  Serial2.write(request, 8);
  Serial2.flush();
  delay(2);
  digitalWrite(RS485_DE_RE, LOW); // 수신 모드
  
  Serial.println("Modbus 요청 전송 완료");
  
  // 응답 대기 및 수신
  delay(200); // 더 긴 응답 대기 (200ms)
  
  byte response[150]; // 133바이트 응답 + 여유분
  int responseIndex = 0;
  
  // 반복적으로 응답 읽기 (최대 1000ms 동안)
  unsigned long startTime = millis();
  while (millis() - startTime < 1000) {
    while (Serial2.available() && responseIndex < 150) {
      response[responseIndex++] = Serial2.read();
    }
    
    // 충분한 데이터를 받았으면 종료 (133바이트 기대)
    if (responseIndex >= 133) {
      break;
    }
    
    delay(20); // 20ms 대기 후 다시 확인
  }
  
  if (responseIndex > 0) {
    Serial.print("Modbus 응답 (");
    Serial.print(responseIndex);
    Serial.print("바이트): ");
    for (int i = 0; i < min(responseIndex, 20); i++) { // 처음 20바이트만 출력
      Serial.printf("0x%02X ", response[i]);
    }
    if (responseIndex > 20) Serial.print("...");
    Serial.println();
    
    // Modbus 응답 시작 위치 찾기 (0x01 0x03 패턴 검색)
    int startIndex = -1;
    for (int i = 0; i < responseIndex - 1; i++) {
      if (response[i] == 0x01 && response[i+1] == 0x03) {
        startIndex = i;
        break;
      }
    }
    
    if (startIndex >= 0 && (responseIndex - startIndex) >= 133) {
      Serial.printf("Modbus 응답 시작 위치 발견: %d\n", startIndex);
      
      // 올바른 위치에서 응답 파싱
      if (response[startIndex] == 0x01 && response[startIndex+1] == 0x03 && response[startIndex+2] == 0x80) {
        Serial.println("Modbus 응답 파싱 성공!");
        
        // 첫 6개 센서만 변환 (기존 호환성 유지)
        for (int i = 0; i < 6; i++) {
          uint16_t reg1 = (response[startIndex+3 + i*4] << 8) | response[startIndex+4 + i*4];
          uint16_t reg2 = (response[startIndex+5 + i*4] << 8) | response[startIndex+6 + i*4];
          sensors[i] = regsToFloat(reg1, reg2);
          Serial.printf("센서[%d]: %.2f (0x%04X, 0x%04X)\n", i, sensors[i], reg1, reg2);
        }
        return true;
      }
    } else {
      Serial.println("Modbus 응답 파싱 실패: 0x01 0x03 0x80 패턴을 찾을 수 없음");
      if (responseIndex > 2) {
        Serial.printf("첫 3바이트: 0x%02X 0x%02X 0x%02X\n", response[0], response[1], response[2]);
      }
    }
  } else {
    Serial.println("Modbus 응답 없음");
  }
  
  return false;
}

// 워드2개 → float 변환 (워드 내 빅엔디언, 워드 간 리틀엔디언)
float regsToFloat(uint16_t word1, uint16_t word0) {
  uint8_t b[4];
  b[0] = word0 >> 8; b[1] = word0 & 0xFF;
  b[2] = word1 >> 8; b[3] = word1 & 0xFF;
  float f;
  memcpy(&f, b, 4);
  return f;
}

// CSV 문자열 생성
String makeCSV(float* s) {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", t);
  char line[128];
  snprintf(line, sizeof(line), "%s,%.1f,%.1f,%.0f,%.0f,%.1f,%.0f",
    buf, s[0], s[1], s[2], s[4], s[3], s[5]);
  // temp,hum,solar,wind_dir,wind_spd,rain 순서
  return String(line);
}

// NAS로 HTTP POST 전송
void sendToNAS(String csv) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(nas_url);
    http.setTimeout(15000); // 15초 타임아웃 (기본 5초에서 증가)
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
        if (retry == 0) delay(1000); // 1초 대기 후 재시도
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
          Serial.printf("수집 %d - 온도: %.1f°C, 습도: %.1f%%, 강우: %.0f\n", 
                       dataCount, sensors[0], sensors[1], sensors[5]);
        }
        xSemaphoreGive(dataMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5000)); // 5초 대기
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
          
          // 평균값 계산
          float avg[6] = {0, 0, 0, 0, 0, 0};
          for (int i = 0; i < dataCount; i++) {
            for (int j = 0; j < 6; j++) {
              avg[j] += dataBuffer[i][j];
            }
          }
          for (int j = 0; j < 6; j++) {
            avg[j] /= dataCount;
          }
          
          Serial.printf("평균값 - 온도: %.1f°C, 습도: %.1f%%, 조도: %.0f Lux, 풍속: %.1f m/s, 풍향: %.0f°, 강우: %.1f\n",
                       avg[0], avg[1], avg[2], avg[3], avg[4], avg[5]);
          
          // CSV 생성 및 전송
          String csv = makeCSV(avg);
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
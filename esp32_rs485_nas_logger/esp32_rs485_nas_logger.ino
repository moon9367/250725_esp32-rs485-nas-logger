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
}

void loop() {
  // Arduino에서 보내는 테스트 메시지 확인 (디버깅용)
  if (Serial2.available()) {
    String receivedData = Serial2.readStringUntil('\n');
    if (receivedData.startsWith("TEST_MESSAGE")) {
      Serial.print("Arduino에서 받은 메시지: ");
      Serial.println(receivedData);
    }
  }
  
  // Modbus 통신 (10초마다)
  static unsigned long lastModbusTime = 0;
  if (millis() - lastModbusTime > 10000) { // 10초마다
    Serial.println("=== Modbus 통신 시작 ===");
    
    float sum[6] = {0, 0, 0, 0, 0, 0};
    int count = 0;
    for (int i = 0; i < 6; i++) { // 6초 동안 1초마다 측정 (빠른 테스트)
      float sensors[6];
      if (readSensors(sensors)) {
        for (int j = 0; j < 6; j++) sum[j] += sensors[j];
        count++;
      } else {
        Serial.println("Modbus read fail");
      }
      delay(1000); // 1초 대기
    }
    // 평균값 계산
    float avg[6];
    if (count > 0) {
      for (int j = 0; j < 6; j++) avg[j] = sum[j] / count;
    } else {
      for (int j = 0; j < 6; j++) avg[j] = 0;
    }
    String csv = makeCSV(avg);
    Serial.println(csv);
    sendToNAS(csv);
    
    lastModbusTime = millis();
    Serial.println("=== Modbus 통신 완료 ===");
  }
  
  delay(1000); // 1초 대기
}

// Modbus로 12워드(6 float) 읽기
bool readSensors(float* sensors) {
  Serial.println("Modbus 요청 시작...");
  
  // 시리얼 버퍼 비우기
  while (Serial2.available()) {
    Serial2.read();
  }
  
  // 직접 Modbus RTU 요청 전송
  byte request[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x45, 0xCF}; // CRC는 미리 계산된 값
  
  digitalWrite(RS485_DE_RE, HIGH); // 송신 모드
  delay(1);
  Serial2.write(request, 8);
  Serial2.flush();
  delay(2);
  digitalWrite(RS485_DE_RE, LOW); // 수신 모드
  
  Serial.println("Modbus 요청 전송 완료");
  
  // 응답 대기 및 수신
  delay(200); // 더 긴 응답 대기 (200ms)
  
  byte response[50];
  int responseIndex = 0;
  
  // 반복적으로 응답 읽기 (최대 1000ms 동안)
  unsigned long startTime = millis();
  while (millis() - startTime < 1000) {
    while (Serial2.available() && responseIndex < 50) {
      response[responseIndex++] = Serial2.read();
    }
    
    // 충분한 데이터를 받았으면 종료
    if (responseIndex >= 29) {
      break;
    }
    
    delay(20); // 20ms 대기 후 다시 확인
  }
  
  if (responseIndex > 0) {
    Serial.print("Modbus 응답 (");
    Serial.print(responseIndex);
    Serial.print("바이트): ");
    for (int i = 0; i < responseIndex; i++) {
      Serial.printf("0x%02X ", response[i]);
    }
    Serial.println();
    
    // Modbus 응답 시작 위치 찾기 (0x01 0x03 패턴 검색)
    int startIndex = -1;
    for (int i = 0; i < responseIndex - 1; i++) {
      if (response[i] == 0x01 && response[i+1] == 0x03) {
        startIndex = i;
        break;
      }
    }
    
    if (startIndex >= 0 && (responseIndex - startIndex) >= 29) {
      Serial.printf("Modbus 응답 시작 위치 발견: %d\n", startIndex);
      
      // 올바른 위치에서 응답 파싱
      if (response[startIndex] == 0x01 && response[startIndex+1] == 0x03) {
        Serial.println("Modbus 응답 파싱 성공!");
        
        // 12개 레지스터 추출 (시작위치+3부터)
        for (int i = 0; i < 12; i++) {
          uint16_t reg = (response[startIndex+3 + i*2] << 8) | response[startIndex+4 + i*2];
          Serial.printf("레지스터[%d]: 0x%04X\n", i, reg);
        }
        
        // 센서 값 변환
        for (int i = 0; i < 6; i++) {
          uint16_t reg1 = (response[startIndex+3 + i*4] << 8) | response[startIndex+4 + i*4];
          uint16_t reg2 = (response[startIndex+5 + i*4] << 8) | response[startIndex+6 + i*4];
          sensors[i] = regsToFloat(reg1, reg2);
          Serial.printf("센서[%d]: %.2f (0x%04X, 0x%04X)\n", i, sensors[i], reg1, reg2);
        }
        return true;
      }
    } else {
      Serial.println("Modbus 응답 파싱 실패: 0x01 0x03 패턴을 찾을 수 없음");
      if (responseIndex > 1) {
        Serial.printf("첫 바이트: 0x%02X, 두 번째 바이트: 0x%02X\n", response[0], response[1]);
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
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String postData = "csv_line=" + csv;
    int code = http.POST(postData);
    if (code == 200) Serial.println("전송 성공");
    else Serial.printf("전송 실패: %d\n", code);
    http.end();
  } else {
    Serial.println("WiFi 연결 안됨");
  }
} 
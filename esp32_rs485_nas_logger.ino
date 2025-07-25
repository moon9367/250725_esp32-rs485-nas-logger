#include <ModbusMaster.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

// ====== 사용자 설정 ======
#define SLAVE_ID 1
#define RS485_TX 17
#define RS485_RX 16
#define RS485_DE_RE 4
#define BAUDRATE 9600

const char* ssid = "YOUR_WIFI_SSID"; // 와이파이 이름 입력력
const char* password = "YOUR_WIFI_PASSWORD"; // 비밀번호 입력
const char* nas_url = "http://NAS_IP/upload.php"; // NAS IP로 수정
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
}

void loop() {
  float sum[6] = {0, 0, 0, 0, 0, 0};
  int count = 0;
  for (int i = 0; i < 60; i++) { // 1분 동안 1초마다 측정
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
  // 다음 1분 측정 반복
}

// Modbus로 12워드(6 float) 읽기
bool readSensors(float* sensors) {
  uint8_t result = node.readHoldingRegisters(0x0000, 12);
  if (result == node.ku8MBSuccess) {
    for (int i = 0; i < 6; i++) {
      sensors[i] = regsToFloat(node.getResponseBuffer(i*2), node.getResponseBuffer(i*2+1));
    }
    return true;
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
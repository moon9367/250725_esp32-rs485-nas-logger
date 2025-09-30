#include <ModbusMaster.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// ====== 🔧 사용자 설정 변수 (완전 변수화) ======
// 통신 기본 설정
int BAUDRATE = 9600;         // 선택: 9600, 19200, 38400, 115200 등
char PARITY = 'N';           // 선택: 'N' (None), 'E' (Even), 'O' (Odd)
int DATA_BITS = 8;           // 보통 8 (변경 가능)
int STOP_BITS = 1;           // 선택: 1 또는 2

// Modbus 기본 설정
int SLAVE_ID = 5;            // 장치 ID (1~247)
int FUNCTION_CODE = 0x03;    // 선택: 0x01, 0x02, 0x03, 0x04

// 레지스터 주소 및 개수
int TARGET_ADDRESSES[] = {203, 212, 218, 227, 230, 233};
int NUM_ADDRESSES = 6;       // 주소 개수
bool ZERO_BASED = false;     // 매뉴얼이 1-based라면 true → -1 보정

// 데이터 타입 (해석 방식)
String DATA_TYPE = "FLOAT";  // 선택: "INT16", "UINT16", "INT32", "FLOAT"

// 수집 및 전송 주기
// POLLING_INTERVAL_MS 제거됨 - DATA_COLLECTION_INTERVAL_SEC로 통합
int TIMEOUT_MS = 500;        // 응답 대기 시간
int RETRIES = 3;             // 실패 시 재시도 횟수

// 데이터 읽기 설정
int REGISTERS_PER_VALUE = 2; // Float는 2워드(4바이트), INT16은 1워드(2바이트)

// 바이트/워드 순서 설정 (일반적인 Modbus 프로그램과 동일)
bool BYTE_SWAP = false;      // 바이트 순서 바꾸기 (Big/Little Endian)
bool WORD_SWAP = false;      // 워드 순서 바꾸기 (High/Low Word First)

// 읽기 방식 설정
bool CONTINUOUS_READ = false; // 연속 레지스터 읽기 vs 개별 주소 읽기

// 데이터 수집/전송 스케줄 설정 (500MB 유심 최적화)
int DATA_COLLECTION_INTERVAL_SEC = 60; // 데이터 수집 주기 (초, 1분)
int DATA_TRANSMISSION_INTERVAL_SEC = 600; // 데이터 전송 주기 (초, 10분)
bool USE_ABSOLUTE_TIME = false; // 절대 시간 사용 (true) vs 상대 시간 (false)

// 시스템 초기화 플래그
bool ntpSyncDone = false; // NTP 동기화 완료 플래그 (한번만 실행)
bool systemReady = false; // 시스템 준비 완료 플래그
bool wifiReconnectNeeded = false; // WiFi 재연결 필요 플래그
time_t systemStartTime = 0; // 시스템 시작 시간 (실제 시간)

// 데이터 버퍼링 설정
String dataBuffer = ""; // 수집된 데이터를 저장할 버퍼
unsigned long lastCollectionTime = 0; // 마지막 데이터 수집 시간
unsigned long lastTransmissionTime = 0; // 마지막 데이터 전송 시간
int bufferCount = 0; // 현재 버퍼에 저장된 데이터 개수
int maxBufferCount = 10; // 최대 버퍼 크기 (전송 주기 / 수집 주기)

// 네트워크 설정
String WIFI_SSID = "aiseed_iot_wifi";
String WIFI_PASSWORD = "123456789#";
String NAS_URL = "http://tspol.iptime.org:8888/rs485/upload.php";



// 하드웨어 핀 설정 (고정값)
#define RS485_TX 17
#define RS485_RX 16
#define RS485_DE_RE 4

// 웹 서버 및 설정 관리
WebServer server(80);
Preferences preferences;

// 계산된 값들 (자동 계산됨)
int MIN_REGISTER_ADDR;
int MAX_REGISTER_ADDR;
int RANGE_START_ADDR;
int RANGE_COUNT;
int SERIAL_MODE;  // 계산된 시리얼 모드

// 데이터 관리 구조체
struct ModbusData {
  int address;
  float value;
  unsigned long timestamp;
  bool valid;
};

ModbusData filteredValues[20]; // 최대 20개 주소 지원
int validDataCount = 0;
SemaphoreHandle_t dataMutex;

// 통계
unsigned long totalRequests = 0;
unsigned long successRequests = 0;

ModbusMaster node;

void preTransmission() { digitalWrite(RS485_DE_RE, HIGH); }
void postTransmission() { digitalWrite(RS485_DE_RE, LOW); }

// ====== 🔧 설정 저장/로드 함수 ======

void saveAllSettings() {
  preferences.begin("modbus_cfg", false);
  
  // 통신 설정
  preferences.putInt("baudrate", BAUDRATE);
  preferences.putChar("parity", PARITY);
  preferences.putInt("data_bits", DATA_BITS);
  preferences.putInt("stop_bits", STOP_BITS);
  preferences.putInt("timeout_ms", TIMEOUT_MS);
  preferences.putInt("retries", RETRIES);
  
  // Modbus 설정
  preferences.putInt("slave_id", SLAVE_ID);
  preferences.putInt("function_code", FUNCTION_CODE);
  preferences.putString("target_addresses", arrayToString(TARGET_ADDRESSES, NUM_ADDRESSES));
  preferences.putInt("num_addresses", NUM_ADDRESSES);
  preferences.putBool("zero_based", ZERO_BASED);
  preferences.putString("data_type", DATA_TYPE);
  
  // 네트워크 설정
  preferences.putString("wifi_ssid", WIFI_SSID);
  preferences.putString("wifi_password", WIFI_PASSWORD);
  preferences.putString("nas_url", NAS_URL);
  
  // 타이밍 설정 (통합됨)
  
  // 데이터 읽기 설정
  preferences.putInt("registers_per_value", REGISTERS_PER_VALUE);
  preferences.putBool("byte_swap", BYTE_SWAP);
  preferences.putBool("word_swap", WORD_SWAP);
  preferences.putBool("continuous_read", CONTINUOUS_READ);
  
  // 데이터 스케줄 설정
  preferences.putInt("collection_interval", DATA_COLLECTION_INTERVAL_SEC);
  preferences.putInt("transmission_interval", DATA_TRANSMISSION_INTERVAL_SEC);
  preferences.putBool("use_absolute_time", USE_ABSOLUTE_TIME);
  
  preferences.end();
  Serial.println("✅ 모든 설정이 저장되었습니다");
}

void loadAllSettings() {
  preferences.begin("modbus_cfg", false);
  
  // 통신 설정 로드
  BAUDRATE = preferences.getInt("baudrate", 9600);
  PARITY = preferences.getChar("parity", 'N');
  DATA_BITS = preferences.getInt("data_bits", 8);
  STOP_BITS = preferences.getInt("stop_bits", 1);
  TIMEOUT_MS = preferences.getInt("timeout_ms", 500);
  RETRIES = preferences.getInt("retries", 3);
  
  // Modbus 설정 로드
  SLAVE_ID = preferences.getInt("slave_id", 1);
  FUNCTION_CODE = preferences.getInt("function_code", 0x03);
  
  String addressStr = preferences.getString("target_addresses", "203,212,218,227,230,233");
  stringToArray(addressStr, TARGET_ADDRESSES, NUM_ADDRESSES);
  
  NUM_ADDRESSES = preferences.getInt("num_addresses", 6);
  ZERO_BASED = preferences.getBool("zero_based", false);
  DATA_TYPE = preferences.getString("data_type", "FLOAT");
  
  // 네트워크 설정 로드
  WIFI_SSID = preferences.getString("wifi_ssid", "aiseed_iot_wifi");
  WIFI_PASSWORD = preferences.getString("wifi_password", "123456789#");
  NAS_URL = preferences.getString("nas_url", "http://tspol.iptime.org:8888/rs485/upload.php");
  
  // 타이밍 설정 로드 (통합됨)
  
  // 데이터 읽기 설정 로드
  REGISTERS_PER_VALUE = preferences.getInt("registers_per_value", 2);
  BYTE_SWAP = preferences.getBool("byte_swap", true);
  WORD_SWAP = preferences.getBool("word_swap", false);
  CONTINUOUS_READ = preferences.getBool("continuous_read", true);
  
  // 데이터 스케줄 설정 (사용자 설정 기본값 - 웹에서 변경 가능)
  DATA_COLLECTION_INTERVAL_SEC = preferences.getInt("collection_interval", 60); // 1분 (기본값)
  DATA_TRANSMISSION_INTERVAL_SEC = preferences.getInt("transmission_interval", 600); // 10분 (기본값)
  USE_ABSOLUTE_TIME = preferences.getBool("use_absolute_time", true); // 절대시간 기본값
  
  preferences.end();
  Serial.println("📂 모든 설정이 로드되었습니다");
  
  calculateSerialMode();
  calculateRange();
}

String arrayToString(int* arr, int count) {
  String result = "";
  for (int i = 0; i < count; i++) {
    if (i > 0) result += ",";
    result += String(arr[i]);
  }
  return result;
}

void stringToArray(String str, int* arr, int& count) {
  count = 0;
  int start = 0;
  int end = str.indexOf(',');
  
  while (end != -1 && count < 20) {
    arr[count++] = str.substring(start, end).toInt();
    start = end + 1;
    end = str.indexOf(',', start);
  }
  if (start < str.length() && count < 20) {
    arr[count++] = str.substring(start).toInt();
  }
}

void calculateSerialMode() {
  // 시리얼 모드 계산
  switch (PARITY) {
    case 'N':
      switch (STOP_BITS) {
        case 1: SERIAL_MODE = SERIAL_8N1; break;
        case 2: SERIAL_MODE = SERIAL_8N2; break;
      }
      break;
    case 'E':
      switch (STOP_BITS) {
        case 1: SERIAL_MODE = SERIAL_8E1; break;
        case 2: SERIAL_MODE = SERIAL_8E2; break;
      }
      break;
    case 'O':
      switch (STOP_BITS) {
        case 1: SERIAL_MODE = SERIAL_8O1; break;
        case 2: SERIAL_MODE = SERIAL_8O2; break;
      }
      break;
    default:
      SERIAL_MODE = SERIAL_8N1;
      break;
  }
  
  Serial.printf("📡 시리얼 모드: %d%c%d %d bps\n", DATA_BITS, PARITY, STOP_BITS, BAUDRATE);
}

void calculateRange() {
  if (NUM_ADDRESSES <= 0) return;
  
  // 원본 배열을 수정하지 않고 계산된 주소 사용
  int minAddr = TARGET_ADDRESSES[0];
  int maxAddr = TARGET_ADDRESSES[0];
  
  for (int i = 1; i < NUM_ADDRESSES; i++) {
    int addr = TARGET_ADDRESSES[i];
    
    if (addr < minAddr) {
      minAddr = addr;
    }
    if (addr > maxAddr) {
      maxAddr = addr;
    }
  }
  
  RANGE_START_ADDR = minAddr;
  RANGE_COUNT = maxAddr - minAddr + 1;
  MIN_REGISTER_ADDR = minAddr;
  MAX_REGISTER_ADDR = maxAddr;
  
  Serial.printf("📊 범위 계산: %d ~ %d (%d개 레지스터)\n", 
               RANGE_START_ADDR, MAX_REGISTER_ADDR, RANGE_COUNT);
}

// ====== 🖥️ 웹 UI 시스템 ======

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>ESP32 RS485 Modbus 스마트 설정</title>";
  html += "<style>body{font-family:Arial,sans-serif;max-width:800px;margin:0 auto;padding:20px;}";
  html += ".section{margin:20px 0;padding:15px;border:1px solid #ddd;border-radius:5px;}";
  html += ".form-group{margin:10px 0;}label{display:block;margin-bottom:5px;font-weight:bold;}";
  html += "input,select,textarea{width:100%;padding:8px;border:1px solid #ddd;border-radius:4px;}";
  html += ".btn{background:#007bff;color:white;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;margin:5px;}";
  html += ".btn:hover{background:#0056b3;}h3{color:#333;border-bottom:2px solid #007bff;}";
  html += "</style></head><body>";
  html += "<h1>🚀 ESP32 RS485 Modbus 스마트 설정</h1>";
  
  html += "<div class='section'>";
  html += "<h3>📡 통신 설정</h3><form method='POST' action='/save'>";
  
  // 볼드레이트 선택
  html += "<div class='form-group'><label>볼드레이트:</label>";
  html += "<select name='baudrate'>";
  html += "<option value='4800'" + String(BAUDRATE==4800?" selected":"") + ">4800</option>";
  html += "<option value='9600'" + String(BAUDRATE==9600?" selected":"") + ">9600</option>";
  html += "<option value='19200'" + String(BAUDRATE==19200?" selected":"") + ">19200</option>";
  html += "<option value='38400'" + String(BAUDRATE==38400?" selected":"") + ">38400</option>";
  html += "<option value='115200'" + String(BAUDRATE==115200?" selected":"") + ">115200</option></select></div>";
  
  // 패리티 선택
  html += "<div class='form-group'><label>패리티:</label>";
  html += "<select name='parity'>";
  html += "<option value='N'" + String(PARITY=='N'?" selected":"") + ">None(N)</option>";
  html += "<option value='E'" + String(PARITY=='E'?" selected":"") + ">Even(E)</option>";
  html += "<option value='O'" + String(PARITY=='O'?" selected":"") + ">Odd(O)</option></select></div>";
  
  // 스톱 비트 선택
  html += "<div class='form-group'><label>스톱 비트:</label>";
  html += "<select name='stop_bits'>";
  html += "<option value='1'" + String(STOP_BITS==1?" selected":"") + ">1비트</option>";
  html += "<option value='2'" + String(STOP_BITS==2?" selected":"") + ">2비트</option></select></div>";
  
  html += "<div class='form-group'><label>타임아웃(ms):</label>";
  html += "<input type='number' name='timeout_ms' value='" + String(TIMEOUT_MS) + "' min='100' max='5000'></div>";
  
  html += "<div class='form-group'><label>재시도 횟수:</label>";
  html += "<input type='number' name='retries' value='" + String(RETRIES) + "' min='1' max='10'></div>";
  html += "</div>";
  
  html += "<div class='section'>";
  html += "<h3>🔧 Modbus 설정</h3>";
  
  html += "<div class='form-group'><label>Slave ID:</label>";
  html += "<input type='number' name='slave_id' value='" + String(SLAVE_ID) + "' min='1' max='247'></div>";
  
  html += "<div class='form-group'><label>Function Code:</label>";
  html += "<select name='function_code'>";
  html += "<option value='1'" + String(FUNCTION_CODE==1?" selected":"") + ">0x01 Read Coils</option>";
  html += "<option value='2'" + String(FUNCTION_CODE==2?" selected":"") + ">0x02 Read Discrete Inputs</option>";
  html += "<option value='3'" + String(FUNCTION_CODE==3?" selected":"") + ">0x03 Read Holding Registers</option>";
  html += "<option value='4'" + String(FUNCTION_CODE==4?" selected":"") + ">0x04 Read Input Registers</option></select></div>";
  
  html += "<div class='form-group'><label>대상 주소 리스트 (콤마로 구분):</label>";
  html += "<input type='text' name='target_addresses' value='" + arrayToString(TARGET_ADDRESSES, NUM_ADDRESSES) + "'>";
  html += "<small>예: 203,212,218,227 (매뉴얼이 1-based인 경우 자동 보정)</small></div>";
  
  html += "<div class='form-group'><label>주소 방식:</label>";
  html += "<select name='zero_based'>";
  html += "<option value='false'" + String(!ZERO_BASED?" selected":"") + ">0-based 주소 (업체 스펙 기준)</option>";
  html += "<option value='true'" + String(ZERO_BASED?" selected":"") + ">1-based 주소 (자동 -1 보정)</option></select></div>";
  
  html += "<div class='form-group'><label>데이터 타입:</label>";
  html += "<select name='data_type' onchange='updateRegistersPerValue(this.value)'>";
  html += "<option value='INT16'" + String(DATA_TYPE=="INT16"?" selected":"") + ">INT16 (16비트 정수, 1워드)</option>";
  html += "<option value='UINT16'" + String(DATA_TYPE=="UINT16"?" selected":"") + ">UINT16 (16비트 부호없는, 1워드)</option>";
  html += "<option value='INT32'" + String(DATA_TYPE=="INT32"?" selected":"") + ">INT32 (32비트 정수, 2워드)</option>";
  html += "<option value='FLOAT'" + String(DATA_TYPE=="FLOAT"?" selected":"") + ">FLOAT (32비트 실수, 2워드)</option></select></div>";
  
  html += "<div class='form-group'><label>워드/값:</label>";
  html += "<select name='registers_per_value' id='registers_per_value'>";
  html += "<option value='1'" + String(REGISTERS_PER_VALUE==1?" selected":"") + ">1워드 (2바이트) - INT16, UINT16</option>";
  html += "<option value='2'" + String(REGISTERS_PER_VALUE==2?" selected":"") + ">2워드 (4바이트) - INT32, FLOAT</option>";
  html += "<option value='3'" + String(REGISTERS_PER_VALUE==3?" selected":"") + ">3워드 (6바이트) - 사용자 정의</option>";
  html += "<option value='4'" + String(REGISTERS_PER_VALUE==4?" selected":"") + ">4워드 (8바이트) - DOUBLE, INT64</option></select>";
  html += "<small>데이터 타입에 맞게 선택하세요</small></div>";
  
  html += "<div class='form-group'><label>바이트 순서:</label>";
  html += "<select name='byte_swap'>";
  html += "<option value='false'" + String(!BYTE_SWAP?" selected":"") + ">Big Endian (기본값)</option>";
  html += "<option value='true'" + String(BYTE_SWAP?" selected":"") + ">Little Endian</option></select></div>";
  
  html += "<div class='form-group'><label>워드 순서:</label>";
  html += "<select name='word_swap'>";
  html += "<option value='false'" + String(!WORD_SWAP?" selected":"") + ">High Word First (기본값)</option>";
  html += "<option value='true'" + String(WORD_SWAP?" selected":"") + ">Low Word First</option></select></div>";
  
  html += "<div class='form-group'><label>읽기 방식:</label>";
  html += "<select name='continuous_read'>";
  html += "<option value='true'" + String(CONTINUOUS_READ?" selected":"") + ">연속 레지스터 읽기 (빠름, 기상센서용)</option>";
  html += "<option value='false'" + String(!CONTINUOUS_READ?" selected":"") + ">개별 주소 읽기 (다른 장치 호환)</option></select></div>";
  html += "</div>";
  
  
  html += "<div class='section'>";
  html += "<h3>🌐 네트워크 설정</h3>";
  
  html += "<div class='form-group'><label>WiFi SSID:</label>";
  html += "<input type='text' name='wifi_ssid' value='" + WIFI_SSID + "'></div>";
  
  html += "<div class='form-group'><label>WiFi 비밀번호:</label>";
  html += "<input type='password' name='wifi_password' value='" + WIFI_PASSWORD + "'></div>";
  
  html += "<div class='form-group'><label>NAS URL:</label>";
  html += "<input type='url' name='nas_url' value='" + NAS_URL + "'></div>";
  html += "</div>";
  
  html += "<div class='section'>";
  html += "<h3>⚡ 타이밍 설정</h3>";
  
  html += "<div class='form-group'><label>데이터 수집 주기(초):</label>";
  html += "<input type='number' name='collection_interval' value='" + String(DATA_COLLECTION_INTERVAL_SEC) + "' min='5' max='3600'>";
  html += "<small>최소 5초 (ESP32 메모리 보호)</small></div>";
  
  html += "<div class='form-group'><label>데이터 전송 주기(초):</label>";
  html += "<input type='number' name='transmission_interval' value='" + String(DATA_TRANSMISSION_INTERVAL_SEC) + "' min='10' max='86400'></div>";
  
  html += "<div class='form-group'><label>시간 설정 방식:</label>";
  html += "<select name='time_mode'>";
  html += "<option value='false'" + String(!USE_ABSOLUTE_TIME?" selected":"") + ">상대 시간 (수집 시작 기준)</option>";
  html += "<option value='true'" + String(USE_ABSOLUTE_TIME?" selected":"") + ">절대 시간 (실제 시간)</option>";
  html += "</select></div>";
  
  html += "</div>";
  
  html += "<button type='submit' class='btn'>💾 모든 설정 저장</button>";
  html += "<button type='button' class='btn' onclick='location.reload()'>🔄 새로고침</button>";
  html += "</form>";
  
  html += "<div class='section'>";
  html += "<h3>📊 시스템 상태</h3>";
  html += "<p><strong>IP 주소:</strong> ";
  html += WiFi.localIP().toString();
  html += "</p>";
  html += "<p><strong>WiFi 상태:</strong> ";
  html += (WiFi.status() == WL_CONNECTED ? "✅ 연결됨" : "❌ 연결 안됨");
  html += "</p>";
  
  // WiFi 재연결 버튼 추가
  html += "<div style='margin: 10px 0;'>";
  html += "<button type='button' class='btn' onclick='location.href=\"/reconnect\"' style='background: #28a745;'>📡 WiFi 재연결</button>";
  html += "</div>";
  html += "<p><strong>시리얼 모드:</strong> " + String(DATA_BITS) + String(PARITY) + String(STOP_BITS) + " " + String(BAUDRATE) + " bps</p>";
  html += "<p><strong>범위:</strong> " + String(RANGE_START_ADDR) + " ~ " + String(RANGE_START_ADDR + RANGE_COUNT - 1) + "</p>";
  html += "<p><strong>데이터 수집 주기:</strong> " + String(DATA_COLLECTION_INTERVAL_SEC) + "초</p>";
  html += "<p><strong>요청 성공률:</strong> " + String(successRequests * 100.0 / (totalRequests > 0 ? totalRequests : 1), 1) + "%</p>";
  html += "</div></body></html>";
  
  server.send(200, "text/html; charset=UTF-8", html);
}

void handleSave() {
  Serial.println("🌐 웹에서 설정 변경 요청 받음");
  
  // 모든 설정값 받기
  if (server.hasArg("baudrate")) {
    int newBaudrate = server.arg("baudrate").toInt();
    if (newBaudrate != BAUDRATE) {
      Serial.println("📡 볼드레이트 변경: " + String(BAUDRATE) + " → " + String(newBaudrate));
      BAUDRATE = newBaudrate;
    }
  }
  
  if (server.hasArg("slave_id")) {
    int newSlaveId = server.arg("slave_id").toInt();
    if (newSlaveId != SLAVE_ID) {
      Serial.println("🔧 Slave ID 변경: " + String(SLAVE_ID) + " → " + String(newSlaveId));
      SLAVE_ID = newSlaveId;
    }
  }
  
  if (server.hasArg("target_addresses")) {
    String newAddresses = server.arg("target_addresses");
    Serial.println("📊 대상 주소 변경: " + newAddresses);
    stringToArray(newAddresses, TARGET_ADDRESSES, NUM_ADDRESSES);
  }
  
  bool wifiChanged = false;
  
  if (server.hasArg("wifi_ssid")) {
    String newSSID = server.arg("wifi_ssid");
    if (newSSID != WIFI_SSID) {
      Serial.println("📡 WiFi SSID 변경: " + WIFI_SSID + " → " + newSSID);
      WIFI_SSID = newSSID;
      wifiChanged = true;
    }
  }
  
  if (server.hasArg("wifi_password")) {
    String newPassword = server.arg("wifi_password");
    if (newPassword != WIFI_PASSWORD) {
      Serial.println("🔑 WiFi 비밀번호 변경됨");
      WIFI_PASSWORD = newPassword;
      wifiChanged = true;
    }
  }
  
  // WiFi 설정이 변경된 경우 재연결 플래그 설정
  if (wifiChanged) {
    Serial.println("🔄 WiFi 설정 변경 감지 - 재연결 예약됨");
    wifiReconnectNeeded = true; // loop()에서 재연결을 처리하도록 플래그 설정
  }
  
  // 기타 설정들
  if (server.hasArg("parity")) PARITY = server.arg("parity").charAt(0);
  if (server.hasArg("stop_bits")) STOP_BITS = server.arg("stop_bits").toInt();
  if (server.hasArg("timeout_ms")) TIMEOUT_MS = server.arg("timeout_ms").toInt();
  if (server.hasArg("retries")) RETRIES = server.arg("retries").toInt();
  if (server.hasArg("function_code")) FUNCTION_CODE = server.arg("function_code").toInt();
  if (server.hasArg("zero_based")) {
    ZERO_BASED = (server.arg("zero_based") == "true");
    Serial.println("🔧 주소 방식 변경: " + String(ZERO_BASED ? "1-based (-1 보정)" : "0-based (업체 스펙)"));
  }
  if (server.hasArg("data_type")) {
    String newDataType = server.arg("data_type");
    if (newDataType != DATA_TYPE) {
      Serial.println("📊 데이터 타입 변경: " + DATA_TYPE + " → " + newDataType);
      DATA_TYPE = newDataType;
    }
  }
  
  if (server.hasArg("registers_per_value")) {
    REGISTERS_PER_VALUE = server.arg("registers_per_value").toInt();
  }
  
  if (server.hasArg("byte_swap")) {
    BYTE_SWAP = (server.arg("byte_swap") == "true");
    Serial.println("🔧 바이트 순서 변경: " + String(BYTE_SWAP ? "Little Endian" : "Big Endian"));
  }
  
  if (server.hasArg("word_swap")) {
    WORD_SWAP = (server.arg("word_swap") == "true");
    Serial.println("🔧 워드 순서 변경: " + String(WORD_SWAP ? "Low Word First" : "High Word First"));
  }
  
  if (server.hasArg("continuous_read")) {
    CONTINUOUS_READ = (server.arg("continuous_read") == "true");
    Serial.println("🔧 읽기 방식 변경: " + String(CONTINUOUS_READ ? "연속 레지스터 읽기" : "개별 주소 읽기"));
  }
  
  // 데이터 스케줄 설정 처리
  if (server.hasArg("collection_interval")) {
    DATA_COLLECTION_INTERVAL_SEC = server.arg("collection_interval").toInt();
    Serial.println("🔧 데이터 수집 주기 변경: " + String(DATA_COLLECTION_INTERVAL_SEC) + "초");
  }
  
  if (server.hasArg("transmission_interval")) {
    DATA_TRANSMISSION_INTERVAL_SEC = server.arg("transmission_interval").toInt();
    Serial.println("🔧 데이터 전송 주기 변경: " + String(DATA_TRANSMISSION_INTERVAL_SEC) + "초");
  }
  
  if (server.hasArg("time_mode")) {
    USE_ABSOLUTE_TIME = (server.arg("time_mode") == "true");
    Serial.println("🔧 시간 설정 방식 변경: " + String(USE_ABSOLUTE_TIME ? "절대 시간" : "상대 시간"));
  }
  
  
  if (server.hasArg("nas_url")) NAS_URL = server.arg("nas_url");
  
  Serial.println("💾 설정 저장 중...");
  
  // 설정 저장 및 적용
  saveAllSettings();
  calculateSerialMode();
  calculateRange();
  
  Serial.println("✅ 설정 변경 완료!");
  Serial.println("🔄 재부팅 후에도 설정이 유지됩니다");
  
  server.send(200, "text/html; charset=UTF-8", "<script>alert('✅ 설정이 저장되고 적용되었습니다!'); history.back();</script>");
}

void handleReconnect() {
  Serial.println("🔄 웹에서 WiFi 재연결 요청");
  
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>WiFi 재연결</title>";
  html += "<style>body{font-family:Arial,sans-serif;max-width:600px;margin:50px auto;padding:20px;text-align:center;}";
  html += ".status{padding:20px;margin:20px 0;border-radius:5px;}";
  html += ".success{background:#d4edda;color:#155724;border:1px solid #c3e6cb;}";
  html += ".error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb;}";
  html += ".btn{background:#007bff;color:white;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;margin:10px;}";
  html += "</style></head><body>";
  
  // WiFi 재연결 시도 (기존 방식)
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASSWORD.c_str());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    html += "<div class='status success'>";
    html += "<h2>✅ WiFi 재연결 성공!</h2>";
    html += "<p>IP 주소: " + WiFi.localIP().toString() + "</p>";
    html += "</div>";
  } else {
    html += "<div class='status error'>";
    html += "<h2>❌ WiFi 재연결 실패</h2>";
    html += "<p>SSID: " + WIFI_SSID + "</p>";
    html += "</div>";
  }
  
  html += "<button class='btn' onclick='location.href=\"/\"'>🏠 메인으로 돌아가기</button>";
  html += "<button class='btn' onclick='location.reload()'>🔄 다시 시도</button>";
  html += "</body></html>";
  
  server.send(200, "text/html; charset=UTF-8", html);
}

void reconnectWiFi() {
  Serial.println("🔄 WiFi 재연결 시도...");
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASSWORD.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); 
    Serial.print(".");
  }
  Serial.println();
  Serial.println("✅ WiFi 재연결 완료");
  Serial.print("IP 주소: ");
  Serial.println(WiFi.localIP());
}

void connectWiFi() {
  Serial.println("📡 WiFi 연결 시도...");
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASSWORD.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); 
    Serial.print(".");
  }
  Serial.println();
  Serial.println("✅ WiFi 연결 완료");
  Serial.print("IP 주소: ");
  Serial.println(WiFi.localIP());
}

// 전체 레지스터 데이터를 한 번에 읽고 저장하는 전역 변수
float allRegisters[96]; // 96개 레지스터 데이터 저장
bool registersRead = false;

// 전체 레지스터 읽기 함수
bool readAllRegisters() {
  Serial.println("📊 전체 레지스터 읽기 시작...");
  
  // 시리얼 버퍼 완전 비우기
  while (Serial2.available()) {
    Serial2.read();
  }
  
  // 연속된 주소 범위 계산 (203~227)
  int startAddr = RANGE_START_ADDR;
  int regCount = RANGE_COUNT;
  
  Serial.printf("📍 범위: %d ~ %d (%d개 레지스터)\n", startAddr, startAddr + regCount - 1, regCount);
  Serial.printf("📊 데이터 타입: %s (%d워드/값)\n", DATA_TYPE.c_str(), REGISTERS_PER_VALUE);
  
  uint8_t result = node.readHoldingRegisters(startAddr, regCount);
  
  if (result == node.ku8MBSuccess) {
    Serial.println("✅ 레지스터 읽기 성공!");
    
    // 데이터 타입에 따라 올바르게 변환
    int valueCount = regCount / REGISTERS_PER_VALUE; // 값 개수 계산
    
    for (int i = 0; i < valueCount && i < 96; i++) {
      float value = 0.0;
      
      if (DATA_TYPE == "FLOAT") {
        // Float: 2워드(4바이트)를 32비트 Float로 변환
        uint16_t word0 = node.getResponseBuffer(i * 2);     // 하위 워드
        uint16_t word1 = node.getResponseBuffer(i * 2 + 1); // 상위 워드
        
        // 워드 순서 조합
        uint32_t raw;
        if (WORD_SWAP) {
          // Low Word First: [워드0][워드1]
          raw = ((uint32_t)word0 << 16) | (uint32_t)word1;
        } else {
          // High Word First: [워드1][워드0] (기본값)
          raw = ((uint32_t)word1 << 16) | (uint32_t)word0;
        }
        
        // IEEE 754 Float 변환
        memcpy(&value, &raw, 4);
        
      } else if (DATA_TYPE == "INT16") {
        // INT16: 1워드(2바이트)를 16비트 정수로 변환
        uint16_t word = node.getResponseBuffer(i);
        value = (int16_t)word; // 부호 있는 정수로 변환
        
      } else if (DATA_TYPE == "UINT16") {
        // UINT16: 1워드(2바이트)를 16비트 부호없는 정수로 변환
        uint16_t word = node.getResponseBuffer(i);
        value = word;
        
      } else if (DATA_TYPE == "INT32") {
        // INT32: 2워드(4바이트)를 32비트 정수로 변환
        uint16_t word0 = node.getResponseBuffer(i * 2);     // 하위 워드
        uint16_t word1 = node.getResponseBuffer(i * 2 + 1); // 상위 워드
        
        uint32_t combined;
        if (WORD_SWAP) {
          combined = ((uint32_t)word0 << 16) | (uint32_t)word1;
        } else {
          combined = ((uint32_t)word1 << 16) | (uint32_t)word0;
        }
        value = (int32_t)combined;
        
      } else if (REGISTERS_PER_VALUE == 3) {
        // 3워드(6바이트) 데이터 - 사용자 정의
        value = i; // 임시로 인덱스 값 사용
        
      } else if (REGISTERS_PER_VALUE == 4) {
        // 4워드(8바이트) 데이터 - DOUBLE 등
        value = i * 1.5; // 임시로 계산된 값 사용
      }
      
      allRegisters[i] = value;
      
      int regNum = startAddr + (i * REGISTERS_PER_VALUE);
      if (DATA_TYPE == "FLOAT" || DATA_TYPE == "INT32") {
        uint16_t word0 = node.getResponseBuffer(i * 2);
        uint16_t word1 = node.getResponseBuffer(i * 2 + 1);
        Serial.printf("Reg[%d-%d]: %04X %04X → %.2f\n", regNum, regNum+1, word0, word1, value);
      } else {
        Serial.printf("Reg[%d]: %.2f\n", regNum, value);
      }
    }
    
    registersRead = true;
    Serial.printf("읽기 완료 (%d개)\n", valueCount);
    return true;
  } else {
    Serial.printf("❌ 레지스터 읽기 실패 (결과: %d)\n", result);
    return false;
  }
}

// 개별 주소 읽기 함수 (다른 Modbus 장치 호환용)
bool readIndividualAddress(float* sensorValue, int regAddr) {
  Serial.printf("읽기 %d... ", regAddr);
  
  // ZERO_BASED 처리
  int addr = regAddr;
  if (ZERO_BASED) {
    addr -= 1;
  }
  
  uint8_t result = node.readHoldingRegisters(addr, REGISTERS_PER_VALUE);
  
  if (result == node.ku8MBSuccess) {
    float value = 0.0;
    
    // 응답 버퍼 크기 확인 (REGISTERS_PER_VALUE * 2바이트)
    int bufferSize = REGISTERS_PER_VALUE;
    Serial.printf("🔍 응답 버퍼 크기: %d 워드\n", bufferSize);
    
    if (DATA_TYPE == "FLOAT") {
      uint16_t word0 = node.getResponseBuffer(0);
      uint16_t word1 = node.getResponseBuffer(1);
      
      // 연속 읽기와 동일한 방식 사용 (word1이 상위, word0이 하위)
      uint32_t raw = ((uint32_t)word1 << 16) | (uint32_t)word0;  // [word1][word0]
      memcpy(&value, &raw, 4);
      
    } else if (DATA_TYPE == "INT16") {
      uint16_t word = node.getResponseBuffer(0);
      value = (int16_t)word;
      
    } else if (DATA_TYPE == "UINT16") {
      uint16_t word = node.getResponseBuffer(0);
      value = word;
      
    } else if (DATA_TYPE == "INT32") {
      uint16_t word0 = node.getResponseBuffer(0);
      uint16_t word1 = node.getResponseBuffer(1);
      
      // 개별 읽기에서는 워드 순서가 반대 (word0이 상위, word1이 하위)
      uint32_t combined = ((uint32_t)word0 << 16) | (uint32_t)word1;
      value = (int32_t)combined;
    }
    
    *sensorValue = value;
    Serial.printf("주소 %d: %04X %04X → %.2f\n", regAddr, 
                  node.getResponseBuffer(0), node.getResponseBuffer(1), value);
    return true;
  } else {
    Serial.printf("주소 %d 읽기 실패\n", regAddr);
    return false;
  }
}

// 저장된 레지스터 데이터에서 특정 센서 값 읽기
bool readSensorFromRegister(float* sensorValue, int regAddr) {
  if (!registersRead) {
    Serial.printf("❌ 레지스터 데이터 없음 (주소 %d)\n", regAddr);
    return false;
  }
  
  // Float의 경우 시작 주소를 기준으로 값 인덱스 계산
  int valueIndex;
  if (DATA_TYPE == "FLOAT") {
    // Float: 시작 주소를 기준으로 값 인덱스 계산
    valueIndex = (regAddr - RANGE_START_ADDR) / REGISTERS_PER_VALUE;
  } else {
    // INT16/UINT16: 직접 주소 사용
    int regOffset = regAddr - RANGE_START_ADDR;
    valueIndex = regOffset / REGISTERS_PER_VALUE;
  }
  
  // 디버깅: 주소 매핑 정보 출력
  Serial.printf("주소 %d → 값[%d]\n", regAddr, valueIndex);
  
  if (valueIndex >= 0 && valueIndex < 96) {
    *sensorValue = allRegisters[valueIndex];
    Serial.printf("주소 %d: %.2f\n", regAddr, *sensorValue);
    return true;
  } else {
    Serial.printf("주소 %d 범위 밖\n", regAddr);
    return false;
  }
}

void pollModbusData() {
  unsigned long currentTime = millis();
  
  // 최대 버퍼 크기 계산 (전송 주기 / 수집 주기)
  maxBufferCount = DATA_TRANSMISSION_INTERVAL_SEC / DATA_COLLECTION_INTERVAL_SEC;
  
  // 데이터 수집 주기 확인 (통합된 주기 사용)
  if (currentTime - lastCollectionTime >= (DATA_COLLECTION_INTERVAL_SEC * 1000)) {
    collectData();
    lastCollectionTime = currentTime;
  }
  
  // 데이터 전송 주기 확인
  if (currentTime - lastTransmissionTime >= (DATA_TRANSMISSION_INTERVAL_SEC * 1000)) {
    if (bufferCount > 0) {
      transmitBufferedData();
    }
    lastTransmissionTime = currentTime;
  }
}

// 새로운 스케줄링 기반 데이터 수집 함수
void collectData() {
  Serial.println("📊 데이터 수집...");
  
  String csvData = "";
  bool hasData = false;
  
  // 시간 문자열 생성
  char timeStr[32];
  if (USE_ABSOLUTE_TIME) {
    // 절대 시간 사용 (실제 시간)
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", t);
  } else {
    // 상대 시간 사용 (시스템 시작 시간 기준) - 실제 날짜/시간으로 변환
    unsigned long elapsed = millis() / 1000; // 시스템 시작 후 경과 시간 (초)
    time_t relativeTime = systemStartTime + elapsed; // 시스템 시작 시간에서 경과 시간 추가
    struct tm* t = localtime(&relativeTime);
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", t);
  }
  
  if (CONTINUOUS_READ) {
    // 연속 레지스터 읽기 방식
    if (!readAllRegisters()) {
      Serial.println("❌ 전체 레지스터 읽기 실패");
      return;
    }
    
    // 저장된 데이터에서 값 추출
    for (int i = 0; i < NUM_ADDRESSES; i++) {
      int originalAddr = TARGET_ADDRESSES[i];
      int addr = originalAddr;
      
      if (ZERO_BASED) {
        addr -= 1;
      }
      
      float sensorValue = 0.0;
      if (readSensorFromRegister(&sensorValue, addr)) {
        hasData = true;
      }
      
      if (csvData.length() > 0) csvData += ",";
      csvData += String(sensorValue, 2);
    }
  } else {
    // 개별 주소 읽기 방식
    for (int i = 0; i < NUM_ADDRESSES; i++) {
      int originalAddr = TARGET_ADDRESSES[i];
      
      float sensorValue = 0.0;
      if (readIndividualAddress(&sensorValue, originalAddr)) {
        hasData = true;
      }
      
      if (csvData.length() > 0) csvData += ",";
      csvData += String(sensorValue, 2);
      delay(100);
    }
  }
  
  if (hasData) {
    String fullData = String(timeStr) + "," + csvData;
    
    // 버퍼에 데이터 추가
    if (dataBuffer.length() > 0) dataBuffer += "\n";
    dataBuffer += fullData;
    bufferCount++;
    
    Serial.println("📝 데이터 수집 완료 (" + String(bufferCount) + "/" + String(maxBufferCount) + ")");
    Serial.println("📤 CSV: " + fullData);
  }
}

// 버퍼된 데이터 전송 함수
void transmitBufferedData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi 연결 안됨 - 데이터 전송 건너뜀");
    return;
  }
  
  Serial.println("📡 버퍼된 데이터 전송 시작 (" + String(bufferCount) + "개)");
  
  if (sendToNAS(dataBuffer)) {
    Serial.println("✅ 데이터 전송 성공");
    dataBuffer = ""; // 버퍼 클리어
    bufferCount = 0;
  } else {
    Serial.println("❌ 데이터 전송 실패 - 버퍼 유지");
  }
}

// 시리얼 명령어 처리
void processSerialCommands() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command.length() > 0) {
      Serial.println("📥 시리얼 명령어 수신: " + command);
      
      if (command.startsWith("wifi_ssid:")) {
        String newSSID = command.substring(10);
        WIFI_SSID = newSSID;
        Serial.println("🔧 WiFi SSID 변경: " + WIFI_SSID);
        wifiReconnectNeeded = true;
        Serial.println("🔄 WiFi 재연결 예약됨 (다음 루프에서 실행)");
      }
      else if (command.startsWith("wifi_password:")) {
        String newPassword = command.substring(14);
        WIFI_PASSWORD = newPassword;
        Serial.println("🔧 WiFi 비밀번호 변경: " + WIFI_PASSWORD);
        wifiReconnectNeeded = true;
        Serial.println("🔄 WiFi 재연결 예약됨 (다음 루프에서 실행)");
      }
      else if (command.startsWith("nas_url:")) {
        String newURL = command.substring(8);
        NAS_URL = newURL;
        Serial.println("🔧 NAS URL 변경: " + NAS_URL);
      }
      else if (command.startsWith("target_addresses:")) {
        String newAddresses = command.substring(17);
        stringToArray(newAddresses, TARGET_ADDRESSES, NUM_ADDRESSES);
        calculateRange();
        Serial.println("🔧 대상 주소 변경: " + newAddresses);
      }
      else if (command.startsWith("zero_based:")) {
        ZERO_BASED = (command.substring(11) == "true");
        Serial.println("🔧 주소 방식 변경: " + String(ZERO_BASED ? "1-based" : "0-based"));
      }
      else if (command.startsWith("data_type:")) {
        DATA_TYPE = command.substring(10);
        Serial.println("🔧 데이터 타입 변경: " + DATA_TYPE);
      }
      else if (command.startsWith("registers_per_value:")) {
        REGISTERS_PER_VALUE = command.substring(19).toInt();
        Serial.println("🔧 워드/값 변경: " + String(REGISTERS_PER_VALUE));
      }
      else if (command.startsWith("byte_swap:")) {
        BYTE_SWAP = (command.substring(10) == "true");
        Serial.println("🔧 바이트 순서 변경: " + String(BYTE_SWAP ? "Little Endian" : "Big Endian"));
      }
      else if (command.startsWith("word_swap:")) {
        WORD_SWAP = (command.substring(10) == "true");
        Serial.println("🔧 워드 순서 변경: " + String(WORD_SWAP ? "Low Word First" : "High Word First"));
      }
      else if (command.startsWith("continuous_read:")) {
        CONTINUOUS_READ = (command.substring(16) == "true");
        Serial.println("🔧 읽기 방식 변경: " + String(CONTINUOUS_READ ? "연속 읽기" : "개별 읽기"));
      }
      else if (command.startsWith("collection_interval:")) {
        DATA_COLLECTION_INTERVAL_SEC = command.substring(19).toInt();
        Serial.println("🔧 수집 주기 변경: " + String(DATA_COLLECTION_INTERVAL_SEC) + "초");
      }
      else if (command.startsWith("transmission_interval:")) {
        DATA_TRANSMISSION_INTERVAL_SEC = command.substring(21).toInt();
        Serial.println("🔧 전송 주기 변경: " + String(DATA_TRANSMISSION_INTERVAL_SEC) + "초");
      }
      else if (command.startsWith("time_mode:")) {
        USE_ABSOLUTE_TIME = (command.substring(10) == "true");
        Serial.println("🔧 시간 모드 변경: " + String(USE_ABSOLUTE_TIME ? "절대 시간" : "상대 시간"));
      }
      else if (command == "save_settings") {
        saveAllSettings();
        Serial.println("💾 모든 설정이 저장되었습니다");
      }
      else if (command == "wifi_status") {
        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("✅ WiFi 연결됨 - SSID: " + WiFi.SSID() + ", IP: " + WiFi.localIP().toString());
        } else {
          Serial.println("❌ WiFi 연결 안됨 - 상태: " + String(WiFi.status()));
        }
      }
      else if (command == "reset") {
        Serial.println("🔄 ESP32 재부팅 중...");
        delay(1000);
        ESP.restart();
      }
      else {
        Serial.println("❓ 알 수 없는 명령어: " + command);
        Serial.println("📋 사용 가능한 명령어:");
        Serial.println("   wifi_ssid:<SSID>");
        Serial.println("   wifi_password:<PASSWORD>");
        Serial.println("   nas_url:<URL>");
        Serial.println("   target_addresses:<ADDRESSES>");
        Serial.println("   zero_based:<true/false>");
        Serial.println("   data_type:<TYPE>");
        Serial.println("   registers_per_value:<NUM>");
        Serial.println("   byte_swap:<true/false>");
        Serial.println("   word_swap:<true/false>");
        Serial.println("   continuous_read:<true/false>");
        Serial.println("   collection_interval:<SECONDS>");
        Serial.println("   transmission_interval:<SECONDS>");
        Serial.println("   time_mode:<true/false>");
        Serial.println("   save_settings");
        Serial.println("   reset");
      }
    }
  }
}

// NAS로 HTTP POST 전송
bool sendToNAS(String csv) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(NAS_URL.c_str());
    http.setTimeout(15000); // 15초 타임아웃
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String postData = "csv_line=" + csv;
    
    // 최대 2번 시도
    for (int retry = 0; retry < 2; retry++) {
      int code = http.POST(postData);
      if (code == 200) {
        Serial.println("✅ 데이터 전송 성공");
        http.end();
        return true;
      } else {
        Serial.printf("❌ 데이터 전송 실패 (시도 %d/2): %d\n", retry+1, code);
        if (retry == 0) delay(1000);
      }
    }
    http.end();
    return false;
  } else {
    Serial.println("⚠️ WiFi 연결 안됨 - 데이터 전송 불가");
    return false;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("🚀 ESP32 RS485 Modbus 스마트 로거 시작");
  
  // 하드웨어 초기화
  pinMode(RS485_DE_RE, OUTPUT);
  digitalWrite(RS485_DE_RE, LOW);
  
  // EEPROM 초기화 옵션 (필요시 true로 변경)
  bool resetEEPROM = false;
  if (resetEEPROM) {
    Serial.println("🔄 EEPROM 초기화 중...");
    preferences.begin("modbus_cfg", false);
    preferences.clear();
    preferences.end();
    Serial.println("✅ EEPROM 초기화 완료");
  }
  
  // 설정 로드
  loadAllSettings();
  
  // 첫 실행 시 EEPROM에 현재 설정 저장 (기본값 업데이트용)
  static bool firstRun = true;
  if (firstRun) {
    saveAllSettings();
    firstRun = false;
    Serial.println("🔄 첫 실행 - 현재 설정을 EEPROM에 저장했습니다");
  }
  
  // WiFi 연결 (최우선 - 30초 타임아웃)
  Serial.println("📡 WiFi 연결 시도...");
  Serial.println("SSID: " + WIFI_SSID);
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASSWORD.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) { // 30초 타임아웃
    delay(500); 
    Serial.print(".");
    attempts++;
    
    // 10번째 시도마다 진행 상황 출력
    if (attempts % 10 == 0) {
      Serial.println();
      Serial.println("진행: " + String(attempts) + "/60 (" + String(attempts/2) + "초)");
    }
  }
  
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ WiFi 연결 완료!");
    Serial.print("IP 주소: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("❌ WiFi 연결 실패 (30초 타임아웃)");
    Serial.println("시스템을 계속 실행하지만 인터넷 기능이 제한됩니다.");
    Serial.println("==================================================");
  }
  
  // RS485 통신 초기화
  Serial2.begin(BAUDRATE, SERIAL_MODE, RS485_RX, RS485_TX);
  Serial2.setRxBufferSize(512);
  
  node.begin(SLAVE_ID, Serial2);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);
  
  Serial.printf("📡 RS485 초기화 완료: %d%c%d, %d bps\n", DATA_BITS, PARITY, STOP_BITS, BAUDRATE);
  Serial.printf("🔧 Slave ID: %d\n", SLAVE_ID);
  Serial.printf("📊 대상 주소: ");
  for (int i = 0; i < NUM_ADDRESSES; i++) {
    Serial.printf("%d", TARGET_ADDRESSES[i]);
    if (i < NUM_ADDRESSES - 1) Serial.printf(", ");
  }
  Serial.printf(" (총 %d개)\n", NUM_ADDRESSES);
  
  // Modbus 오류 코드 설명
  Serial.println("📋 Modbus 오류 코드 참고:");
  Serial.println("  0: 성공");
  Serial.println("  226: Illegal Data Address (잘못된 주소)");
  Serial.println("  227: Illegal Data Value (잘못된 값)");
  Serial.println("  228: Slave Device Failure (장치 오류)");
  
  // WiFi 연결 확인
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ WiFi 연결됨 - 일반 모드");
  } else {
    Serial.println("⚠️ WiFi 연결 실패 - 기본 웹서버만 시작");
  }
  
  // 웹 서버 시작
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reconnect", handleReconnect);
  server.begin();
  Serial.println("🌐 웹 서버 시작됨");
  
  // NTP 시간 동기화 (WiFi 연결된 경우에만, 한번만 실행)
  if (WiFi.status() == WL_CONNECTED && !ntpSyncDone) {
    Serial.println("⏰ NTP 시간 동기화 시작...");
  configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  while (time(nullptr) < 100000) { delay(500); Serial.print("#"); }
    Serial.println();
    Serial.println("⏰ NTP 동기화 완료 (한번만 실행)");
    ntpSyncDone = true;
  } else if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⏰ NTP 동기화 건너뜀 (WiFi 연결 안됨)");
  } else {
    Serial.println("⏰ NTP 동기화 이미 완료됨");
  }
  
  // Mutex 생성
  dataMutex = xSemaphoreCreateMutex();
  
  // 시스템 시작 시간 초기화 (상대 시간 기준점)
  systemStartTime = time(nullptr); // 현재 시간을 시스템 시작 시간으로 설정
  lastCollectionTime = 0;
  lastTransmissionTime = 0;
  
  systemReady = true;
  Serial.println("🚀 시스템 준비 완료 - 데이터 수집 시작");
  Serial.println("==================================================");
}

void loop() {
  server.handleClient();
  
  static unsigned long lastPollTime = 0;
  static unsigned long lastWiFiCheck = 0;
  unsigned long currentTime = millis();
  
  // WiFi 연결 상태 확인 (30초마다)
  if (currentTime - lastWiFiCheck > 30000) {
    lastWiFiCheck = currentTime;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("❌ WiFi 연결 끊김, 재연결 시도...");
      reconnectWiFi();
    }
  }
  
  // WiFi 재연결 필요 시 재연결 시도
  if (wifiReconnectNeeded) {
    wifiReconnectNeeded = false;
    Serial.println("🔄 WiFi 재연결 시도 (설정 변경 후)...");
    WiFi.disconnect();
    delay(1000);
    WiFi.begin(WIFI_SSID.c_str(), WIFI_PASSWORD.c_str());
    
    // 재연결 결과 확인 (최대 10초 대기)
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      attempts++;
      Serial.print(".");
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("✅ WiFi 재연결 성공!");
      Serial.println("📡 SSID: " + WiFi.SSID());
      Serial.println("🌐 IP: " + WiFi.localIP().toString());
    } else {
      Serial.println("❌ WiFi 재연결 실패 - 상태: " + String(WiFi.status()));
    }
  }
  
  // WiFi가 연결된 경우에만 Modbus 데이터 수집
  // 시스템 준비 완료 후에만 데이터 수집
  if (systemReady) {
    if (WiFi.status() == WL_CONNECTED) {
      if (currentTime - lastPollTime >= (DATA_COLLECTION_INTERVAL_SEC * 1000)) {
        lastPollTime = currentTime;
        pollModbusData();
      }
    } else {
      // WiFi 연결 안된 경우 간단한 상태 출력
      static unsigned long lastStatusTime = 0;
      if (currentTime - lastStatusTime > 10000) { // 10초마다
        lastStatusTime = currentTime;
        Serial.println("⚠️ WiFi 연결 안됨 - Modbus 데이터 수집 중단");
      }
    }
  } else {
    // 시스템 준비 중일 때는 상태 출력만
    if (currentTime - lastPollTime >= 10000) { // 10초마다 상태 출력
      lastPollTime = currentTime;
      Serial.println("⏳ 시스템 준비 중...");
    }
  }
  
  // 시리얼 명령어 처리
  processSerialCommands();
  
  delay(100);
}

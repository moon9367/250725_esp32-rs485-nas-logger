# 🚀 ESP32 RS485 Modbus 스마트 데이터 수집 시스템 v2.0

**완전 변수화된 통신 설정과 데이터 타입 변환을 지원하는 고급 RS485 Modbus RTU 시스템**

## 🌟 핵심 특징

### 🔧 완전 변수화된 설정
- **통신 파라미터**: 볼드레이트, 패리티, 데이터비트, 스톱비트, 타임아웃, 재시도 횟수
- **Modbus 설정**: Slave ID, Function Code, 레지스터 주소 리스트, 데이터 타입
- **네트워크 설정**: WiFi SSID/비밀번호, NAS URL
- **타이밍 설정**: 폴링 간격, 데이터 수집 주기

### 📊 다중 데이터 타입 지원
- **INT16**: 16비트 부호있는 정수 (-32,768 ~ 32,767)
- **UINT16**: 16비트 부호없는 정수 (0 ~ 65,535)
- **INT32**: 32비트 부호있는 정수 (±2.1×10⁹)
- **FLOAT**: IEEE 754 32비트 실수 (±3.4×10³⁸)

### 🎛️ 두 가지 설정 관리 방식
1. **웹 UI**: 브라우저를 통한 직관적인 설정 변경
2. **Python 도구**: COM 포트를 통한 프로그래밍 스타일 설정

## 📋 주요 설정 변수

### 🔌 통신 설정
```cpp
int BAUDRATE = 9600;         // 4800, 9600, 19200, 38400, 115200
char PARITY = 'N';           // 'N' (None), 'E' (Even), 'O' (Odd)
int DATA_BITS = 8;           // 보통 8비트
int STOP_BITS = 1;           // 1 또는 2비트
int TIMEOUT_MS = 500;        // 응답 대기 시간
int RETRIES = 3;             // 실패 시 재시도 횟수
```

### 🔧 Modbus 설정
```cpp
int SLAVE_ID = 1;            // 장치 ID (1~247)
int FUNCTION_CODE = 0x03;    // 0x01~0x04
int TARGET_ADDRESSES[] = {203, 212, 218, 227};
int NUM_ADDRESSES = 4;
bool ZERO_BASED = true;      // 1-based 주소 자동 보정
String DATA_TYPE = "FLOAT";  // INT16, UINT16, INT32, FLOAT
```

### 📋 Modbus 주소 체계 이해
**기존 방식 (400203) → 새로운 방식 (203)**
- **400203**: Modbus 표준 주소 (4 = Function Code 03, 00203 = 레지스터 주소)
- **203**: 실제 레지스터 주소만 사용 (Function Code는 별도 설정)
- **장점**: 더 직관적이고 설정이 간단해짐
- **변환**: 400203 → Function Code: 03, Register: 203

### ⏱️ 타이밍 설정
```cpp
int POLLING_INTERVAL_MS = 1000;  // 폴링 간격 (100ms ~ 5분)
```

### 🌐 네트워크 설정
```cpp
String WIFI_SSID = "YOUR_SSID";
String WIFI_PASSWORD = "YOUR_PASSWORD";
String NAS_URL = "http://192.168.0.10/data_logger.php";
```

## 🏗️ 시스템 아키텍처

### 🔄 동작 프로세스
1. **부팅**: WiFi 연결 → NAS URL 설정
2. **설정 로드**: Preferences에서 사용자 설정 복원
3. **범위 계산**: 주소 리스트 → 최소/최대 범위 자동 계산
4. **범위 읽기**: 한 번의 Modbus 요청으로 전체 범위 읽기
5. **필터링**: 응답에서 필요한 주소만 추출
6. **타입 변환**: 지정된 DATA_TYPE으로 변환 (INT/FLOAT)
7. **데이터 저장**: 순차적으로 배열에 저장
8. **전송**: NAS 서버로 HTTP POST 전송
9. **재시도**: 실패 시 RETRIES만큼 재시도

### 📊 범위 확장 예시
```
사용자 주소: [203, 212, 218, 227]
자동 계산: 최소=203, 최대=227
요청 범위: 203~227 (25개 레지스터)
Modbus 요청: Slave_ID, FC=0x03, Start=203, Count=25
응답에서 추출: 주소 203, 212, 218, 227만 선별

기존 방식과 비교:
- 기존: 400203, 400212, 400218, 400227 (4번의 개별 요청)
- 새로운: 203~227 범위 읽기 (1번의 효율적 요청)
```

## 📁 프로젝트 구조

```
250725_esp32-rs485-nas-logger/
├── esp32_enhanced_modbus_logger/
│   └── esp32_enhanced_modbus_logger.ino     # 스마트 코드 (v2.0)
├── esp32_config_tool_simple.py              # Python 설정 도구
├── data_logger.php                           # PHP 서버
├── esp32_settings.json                       # 설정 파일
└── README.md                                 # 이 문서 (v2.0)
```

## 🛠️ 설치 및 설정

### 📱 ESP32 펌웨어 업로드
```bash
# Arduino IDE에서 파일 업로드
esp32_enhanced_modbus_logger/esp32_enhanced_modbus_logger.ino

# 필수 라이브러리:
# - ModbusMaster
# - ArduinoJson
# - Preferences (ESP32 기본)
```

### 🌐 웹 설정 인터페이스
1. ESP32 부팅 후 시리얼 모니터에서 IP 주소 확인
2. 브라우저에서 `http://[ESP32_IP]` 접속
3. 설정 페이지에서 모든 파라미터 조정 가능:
   - 통신 설정 (볼드레이트, 패리티, 스톱비트)
   - Modbus 설정 (Slave ID, Function Code, 주소)
   - 데이터 타입 선택
   - 네트워크 설정
   - 타이밍 설정

### 🐍 Python 설정 도구
```bash
# 인터랙티브 설정
python esp32_config_tool_simple.py

# 포트 지정
python esp32_config_tool_simple.py --port COM3

# 설정 파일 로드
python esp32_config_tool_simple.py --config-file esp32_settings.json

# 실시간 모니터링
python esp32_config_tool_simple.py --monitor --monitor-duration 300

# 빠른 초기화
python esp32_config_tool_simple.py --quick-setup
```

## 📊 설정 예시

### 🏭 산업용 모니터링 설정
```json
{
  "communication": {
    "baudrate": 38400,
    "parity": "E",
    "timeout_ms": 300,
    "retries": 3
  },
  "modbus": {
    "slave_id": 5,
    "function_code": 3,
    "target_addresses": [100, 101, 102, 103, 104],
    "data_type": "INT16"
  },
  "timing": {
    "polling_interval_ms": 2000
  }
}
```

### 🌾 스마트팜 설정
```json
{
  "communication": {
    "baudrate": 9600,
    "parity": "N",
    "timeout_ms": 1000
  },
  "modbus": {
    "slave_id": 1,
    "function_code": 3,
    "target_addresses": [203, 212, 218, 227],
    "data_type": "FLOAT"
  },
  "timing": {
    "polling_interval_ms": 5000
  }
}
```

### ⚡ 에너지 모니터 설정
```json
{
  "communication": {
    "baudrate": 19200,
    "parity": "N",
    "timeout_ms": 750
  },
  "modbus": {
    "slave_id": 2,
    "function_code": 3,
    "target_addresses": [500, 501, 502],
    "data_type": "INT32"
  },
  "timing": {
    "polling_interval_ms": 30000
  }
}
```

## 🌐 웹 UI 기능

### 📡 통신 설정 섹션
- **볼드레이트**: 드롭다운 메뉴 (4800, 9600, 19200, 38400, 115200)
- **패리티**: None(N), Even(E), Odd(O) 선택
- **스톱 비트**: 1비트 또는 2비트 선택
- **타임아웃**: 100ms~5000ms 범위 설정
- **재시도 횟수**: 1~10회 설정

### 🔧 Modbus 설정 섹션
- **Slave ID**: 1~247 범위 설정
- **Function Code**: 0x01~0x04 선택 (설명 포함)
- **주소 리스트**: 콤마로 구분된 텍스트 입력 (예: 203,212,218,227)
- **1-Based 주소**: 체크박스로 자동 주소 보정
- **데이터 타입**: INT16/UINT16/INT32/FLOAT 선택

**주소 입력 방식**:
- ✅ **새로운 방식**: `203, 212, 218, 227` (직관적)
- ❌ **기존 방식**: `400203, 400212, 400218, 400227` (복잡함)

### 🌐 네트워크 설정 섹션
- **WiFi SSID**: 네트워크 이름 입력
- **WiFi 비밀번호**: 비밀번호 입력
- **NAS URL**: 서버 주소 입력

### ⚡ 타이밍 설정 섹션
- **폴링 간격**: 100ms~300000ms (5분) 범위 설정
- **실시간 계산**: 밀리초와 초 단위 동시 표시

### 📊 실시간 상태 표시
- **IP 주소**: 현재 할당된 IP 주소
- **WiFi 상태**: 연결/비연결 상태
- **시리얼 모드**: 실제 설정된 통신 모드 표시
- **범위 정보**: 자동 계산된 레지스터 범위
- **성공률**: 통신 요청 성공률 통계

## 🔍 데이터 타입 변환 상세

### 🌡️ INT16 변환 예시
```cpp
// 원본 데이터: [0x04, 0x57] (Big Endian)
// 변환 결과: 0x0457 = 1111 (십진)
// 사용 예시: 온도 센서 (°C × 10으로 전송)
// 실제 온도: 11.11°C
```

### 💧 UINT16 변환 예시
```cpp
// 원본 데이터: [0x09, 0xC4] (Big Endian)  
// 변환 결과: 0x09C4 = 2500 (십진)
// 사용 예시: 습도 센서 (% × 100으로 전송)
// 실제 습도: 25.00%
```

### 🔢 INT32 변환 예시
```cpp
// 원본 데이터: [0x00, 0x00, 0x27, 0x10] (Big Endian)
// 변환 결과: 0x10000 (Big-Endian은 아님)
// 4바이트 리틀 엔디언으로 해석 필요
```

### 🔄 FLOAT 변환 예시
```cpp
// 원본 데이터: [0x41, 0xAE, 0xCC, 0xCD] 
// IEEE 754 변환: 21.85 (부동소수점)
// 사용 예시: 모든 실수값 센서
```

## 🚀 고급 기능

### 🔧 자동 주소 보정 (1-Based 지원)
```cpp
bool ZERO_BASED = true;  // 매뉴얼이 1-based인 경우

// 사용자 입력: 주소 203 (1-based)
// 내부 변환: 주소 202 (0-based)
// Modbus 요청: 주소 202부터 읽기
// 응답 변환: 주소 203으로 사용자에게 표시
```

### ⚡ 최적화된 통신
- **긴 타임아웃**: 고속 볼드레이트에서는 짧은 타임아웃 사용
- **스마트 재시도**: 백오프(backoff) 알고리즘으로 재시도 간격 조정
- **버퍼 관리**: 시리얼 버퍼 자동 정리로 데이터 충돌 방지

### 📊 실시간 통계
- **요청 횟수**: 총 Modbus 요청 횟수 트래킹
- **성공률**: 통신 성공률 백분율 계산
- **업타임**: 시스템 동작 시간 측정
- **데이터 품질**: 유효한 데이터 비율 측정

## 🛠️ 문제 해결 가이드

### ❌ 통신 연결 문제
**증상**: 시리얼 포트 연결 실패
**해결책**:
1. COM 포트 번호 확인 (디바이스 관리자)
2. ESP32 전원 상태 확인 
3. USB 드라이버 재설치
4. 다른 USB 케이블/포트 시도

### ❌ Modbus 타임아웃
**증상**: "Modbus 응답 없음" 오류
**해결책**:
1. 타임아웃 값을 늘리기 (500ms → 1000ms)
2. 볼드레이트 매뉴얼과 일치 확인
3. 슬레이브 ID 정확성 확인
4. RS485 연결 상태 점검

### ❌ 데이터 비정상
**증상**: 수신된 데이터가 예상과 다름
**해결책**:
1. 데이터 타입 매뉴얼 재확인 (INT/FLOAT)
2. 패리티 설정 확인 (N/E/O)
3. 엔디언 순서 확인 (Big/Little)
4. 레지스터 주소 매뉴얼 확인

### ❌ WiFi 연결 불안정
**증상**: WiFi 연결이 끊김
**해결책**:
1. 신호 강도 확인 (RSSI 값)
2. SSID/비밀번호 철자 확인
3. ESP32 재부팅
4. 정적 IP 설정 고려

### ❌ 설정 저장 실패
**증상**: 설정이 재부팅 후 사라짐
**해결책**:
1. Preferences 저장 확인
2. ESP32 플래시 메모리 상태 점검
3. 웹 설정 후 "저장" 버튼 클릭 확인
4. Python 도구에서 설정 적용 확인

## 🌟 확장성 및 활용

### 🏭 산업 자동화 활용
- **제조 장비 모니터링**: 모터 온도, 진동 센서
- **플랜트 관리**: 공압, 유압 시스템 모니터링
- **품질 관리**: 측정 데이터 수집 및 분석

### 🏡 IoT 스마트 홈
- **HVAC 시스템**: 온도, 습도, 공기질 센서
- **전력 관리**: 스마트 미터, 태양광 발전량
- **보안 시스템**: 센서 네트워크 관리

### 🌱 스마트 농업 구현
- **환경 모니터링**: 온도, 습도, 토양 수분
- **자동 급수**: 수위 센서 기반 타이머 제어
- **작물 관리**: 성장 데이터 수집 및 분석

### ⚡ 에너지 관리
- **전력 사용량**: 실시간 전력 계량
- **배터리 모니터링**: 충방전 상태 추적
- **재생 에너지**: 태양광, 풍력 발전량 측정

---

**🚀 이 시스템은 초보자부터 전문가까지 누구나 쉽게 사용할 수 있는 완전 변수화된 RS485 Modbus 데이터 수집 솔루션입니다.**

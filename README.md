# 📊 ESP32 RS485 NAS Logger 시스템

## 🎯 프로젝트 개요

**Arduino Nano**에서 생성한 가상 센서 데이터를 **ESP32**가 **RS485 Modbus RTU**로 수집하여 **NAS 서버**에 **CSV 형태**로 저장하는 IoT 데이터 로깅 시스템입니다.

## 🏗️ 시스템 구성

```
Arduino Nano → RS485 Modbus → ESP32 → WiFi → NAS Server
(센서 생성)   (RTU 통신)    (수집+평균)  (HTTP)  (CSV 저장)
```

### 1. **Arduino Nano** (Modbus RTU Slave)
- **역할**: 가상 센서 데이터 생성 및 Modbus RTU 서버
- **통신**: SoftwareSerial (4800 baud)
- **핀 설정**: TX=9, RX=8, DE/RE=2
- **Modbus ID**: 1
- **레지스터**: 12개 (6개 float → 12개 word)

### 2. **ESP32** (Modbus RTU Master + WiFi Client)
- **역할**: 데이터 수집, 평균값 계산, NAS 전송
- **통신**: RS485 Modbus RTU (4800 baud) + WiFi
- **핀 설정**: TX=17, RX=16, DE/RE=4
- **멀티태스킹**: FreeRTOS 기반 2개 Task

### 3. **NAS 서버** (Data Storage)
- **역할**: CSV 데이터 수신 및 파일 저장
- **웹서버**: Web Station (PHP)
- **스크립트**: `upload.php`

## 📊 센서 데이터

| 센서 | 범위 | 단위 | 설명 |
|------|------|------|------|
| 온도 | 20.0~35.0 | °C | 대기온도 |
| 습도 | 30~90 | % | 상대습도 |
| 조도 | 0~1000 | Lux | 일사량 |
| 풍속 | 0~15.0 | m/s | 풍속 |
| 풍향 | 0~359 | ° | 풍향 |
| 강우센서 | 0 또는 1 | - | 0=맑음, 1=비옴 |

## ⚙️ 멀티태스킹 구조

ESP32는 **FreeRTOS**를 사용하여 두 개의 Task로 동작합니다:

### **Task 1 (CPU Core 0): 데이터 수집**
- **주기**: 5초마다 연속 수집
- **동작**: Modbus RTU로 Arduino에서 센서 데이터 읽기
- **저장**: 데이터 버퍼에 누적

### **Task 2 (CPU Core 1): 데이터 전송**
- **주기**: 매 분 00초마다
- **동작**: 1분간 수집된 모든 데이터의 평균값 계산
- **전송**: HTTP POST로 NAS에 CSV 전송

## 📁 파일 구조

```
250725_esp32-rs485-nas-logger/
├── arduino_modbus_virtual_sensor/
│   └── arduino_modbus_virtual_sensor.ino    # Arduino 가상센서
├── esp32_rs485_nas_logger/
│   └── esp32_rs485_nas_logger.ino           # ESP32 메인 코드
├── upload.php                               # NAS PHP 스크립트
├── README.md                                # 프로젝트 문서
└── 에이아이시드 RS485 Modbus Protocol.pptx  # 프로토콜 문서
```

## 🔧 설정 방법

### **1. Arduino Nano 설정**
```cpp
// arduino_modbus_virtual_sensor.ino
#define RS485_TX 9
#define RS485_RX 8  
#define RS485_DE_RE 2
// 통신속도: 4800 baud
// Modbus Slave ID: 1
```

### **2. ESP32 설정**
```cpp
// esp32_rs485_nas_logger.ino
#define RS485_TX 17
#define RS485_RX 16
#define RS485_DE_RE 4
#define BAUDRATE 4800

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* nas_url = "http://YOUR_NAS_IP:PORT/rs485/upload.php";
```

### **3. NAS 설정**
- Web Station 활성화
- `/web/rs485/upload.php` 업로드
- 폴더 권한 설정 (777)

## 📈 데이터 형식

### **CSV 형식**
```
YYYY-MM-DD HH:MM,온도,습도,조도,풍속,풍향,강우
2025-07-27 12:34,28.5,65.2,450,5.2,180,0
```

### **NAS 저장 구조**
```
/web/rs485/
├── 2025/
│   ├── 07/
│   │   ├── 20250727.csv
│   │   └── 20250728.csv
│   └── 08/
│       └── 20250801.csv
└── upload.php
```

## 🚀 실행 방법

### **1. Arduino 업로드**
```bash
1. Arduino IDE에서 arduino_modbus_virtual_sensor.ino 열기
2. 보드: Arduino Nano, 포트 선택
3. 업로드
4. 시리얼 모니터 (9600 baud) 확인
```

### **2. ESP32 업로드**
```bash
1. Arduino IDE에서 esp32_rs485_nas_logger.ino 열기
2. 보드: ESP32 Dev Module, 포트 선택
3. WiFi 정보, NAS URL 수정
4. 업로드
5. 시리얼 모니터 (115200 baud) 확인
```

### **3. 동작 확인**
```bash
# ESP32 시리얼 모니터 예시
=== 멀티태스킹 시작 ===
수집 1 - 온도: 28.5°C, 습도: 65.2%, 강우: 0
수집 2 - 온도: 29.1°C, 습도: 64.8%, 강우: 1
...
=== 1분간 수집 완료 (12개) ===
평균값 - 온도: 28.8°C, 습도: 65.0%, 조도: 450 Lux...
CSV: 2025-07-27 12:34,28.8,65.0,450,5.2,180,0.3
전송 성공
```

## 🔍 테스트 방법

### **Modbus 통신 테스트**
- Arduino 시리얼 모니터에서 "Modbus 응답 전송 완료" 확인
- ESP32 시리얼 모니터에서 "Modbus 응답 파싱 성공!" 확인

### **HTTP 전송 테스트**
```bash
curl -d "csv_line=2025-07-27 12:34,28.8,65.0,450,5.2,180,0" \
     http://YOUR_NAS_IP:PORT/rs485/upload.php
```

### **NAS 저장 확인**
- `/web/rs485/YYYY/MM/YYYYMMDD.csv` 파일 생성 확인
- CSV 헤더 자동 추가 확인

## 🛠️ 주요 특징

### **안정성**
- ✅ 멀티태스킹: 수집과 전송 동시 진행
- ✅ 재시도 로직: HTTP 전송 실패 시 재시도
- ✅ Mutex: Task 간 데이터 보호
- ✅ 타임아웃: HTTP 15초 타임아웃

### **정확성**
- ✅ NTP 시간 동기화
- ✅ CRC16 검증 (Modbus)
- ✅ IEEE754 float 변환
- ✅ 1분 평균값 계산

### **확장성**
- ✅ 실제 센서로 교체 가능
- ✅ 센서 개수 확장 가능
- ✅ 다른 NAS/서버 연동 가능
- ✅ SD카드 백업 추가 가능

## 🚨 문제 해결

### **Modbus 통신 오류**
```bash
# Arduino 시리얼 출력 확인
"Modbus 요청: 시작주소=0x0, 레지스터수=12"
"전송 바이트: 0x1 0x3 0x18 ..."

# ESP32 시리얼 출력 확인  
"Modbus 응답 파싱 성공!"
"센서[0]: XX.XX (0xXXXX, 0xXXXX)"
```

### **WiFi 연결 오류**
- SSID, 비밀번호 확인
- 2.4GHz WiFi 사용 확인
- ESP32 WiFi 안테나 확인

### **HTTP 전송 오류**
- NAS IP, 포트 확인
- Web Station 활성화 확인
- upload.php 경로 확인
- 방화벽 설정 확인

## 📚 라이브러리

### **Arduino Nano**
- SoftwareSerial (내장)

### **ESP32**
- ModbusMaster
- WiFi (내장)
- HTTPClient (내장)
- time.h (내장)

## 🔗 확장 아이디어

- 📱 **모바일 앱**: 실시간 데이터 모니터링
- 📈 **대시보드**: Grafana, InfluxDB 연동
- 🔔 **알림 시스템**: 임계값 초과 시 알림
- 💾 **로컬 저장**: SD카드 백업
- 🌐 **클라우드**: AWS, Azure 연동

## 📞 지원

문제 발생 시 다음 정보와 함께 문의해주세요:
- Arduino 시리얼 모니터 출력
- ESP32 시리얼 모니터 출력  
- NAS 에러 로그
- 하드웨어 연결 상태

---

**🎯 이 시스템은 산업용 IoT 센서 네트워크의 기본 모델로 활용할 수 있습니다!** 
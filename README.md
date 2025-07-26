# 🌤️ ESP32 RS485 Modbus RTU NAS Logger

**실제 하드웨어 센서 장비와 호환되는 RS485 Modbus RTU 기반 데이터 수집 및 NAS 로깅 시스템**

## 📋 시스템 개요

### 🎯 목적
- RS485 Modbus RTU를 통한 센서 데이터 수집
- WiFi를 통한 NAS 서버로의 실시간 데이터 전송
- 1분 평균값 기반의 안정적인 데이터 로깅

### 🏗️ 시스템 구성
```
[실제 센서 장비] ←→ [ESP32 Master] ←→ [WiFi] ←→ [NAS Server]
     (Slave)           (Modbus RTU)              (PHP + CSV)
```

### 🔧 핵심 특징
- ✅ **실제 장비 프로토콜 호환**: 표준 Modbus RTU 프로토콜 준수
- ✅ **멀티태스킹**: FreeRTOS 기반 동시 데이터 수집 및 전송
- ✅ **안정적 통신**: RS485 + CRC16 검증
- ✅ **자동 시간 동기화**: NTP 기반 절대 시간 정확성
- ✅ **확장 가능**: 32개 센서 지원 (64개 Modbus 레지스터)

## 📊 데이터 사양

### 🌡️ 센서 데이터 (32개 Float)
| 인덱스 | 센서 타입 | 범위 | 단위 | 설명 |
|--------|-----------|------|------|------|
| 0 | 온도 | 20.0~35.0 | °C | 대기 온도 |
| 1 | 습도 | 30~90 | % | 상대 습도 |
| 2 | 조도 | 0~1000 | Lux | 태양광 조도 |
| 3 | 풍속 | 0~15.0 | m/s | 풍속 |
| 4 | 풍향 | 0~359 | ° | 풍향 |
| 5 | 강우 | 0 or 1 | - | 강우 감지 |
| 6~31 | 확장 센서 | - | - | 추가 센서 데이터 |

### 📡 Modbus RTU 프로토콜
- **시작 주소**: 0x00CB (203번)
- **레지스터 수**: 64개 (128바이트)
- **Float 개수**: 32개
- **통신 속도**: 4800 baud
- **데이터 형식**: IEEE754 Float (워드 간 리틀엔디언, 워드 내 빅엔디언)

### 📤 요청/응답 프레임
```
Master → Slave: 01 03 00 CB 00 40 35 C4
Slave → Master: 01 03 80 [128 Bytes Data] CRC_L CRC_H
```

## 🛠️ 하드웨어 설정

### 📌 핀 연결 (ESP32)
| 기능 | ESP32 핀 | 설명 |
|------|----------|------|
| RS485 TX | GPIO 17 | 송신 |
| RS485 RX | GPIO 16 | 수신 |
| RS485 DE/RE | GPIO 4 | 송수신 제어 |

### 📌 핀 연결 (Arduino Nano)
| 기능 | Arduino 핀 | 설명 |
|------|------------|------|
| RS485 TX | D9 | 송신 |
| RS485 RX | D8 | 수신 |
| RS485 DE/RE | D2 | 송수신 제어 |

### 🔌 RS485 모듈
- **모델**: MAX485 또는 호환 모듈
- **전원**: 3.3V (ESP32) / 5V (Arduino)
- **통신**: 반이중 통신

## ⚙️ 소프트웨어 아키텍처

### 🔄 FreeRTOS 태스크 구조
```
Core 0: dataCollectionTask
├── 5초마다 Modbus 요청
├── 센서 데이터 수집
└── 데이터 버퍼 저장

Core 1: dataTransmissionTask  
├── 매 분 정각 체크
├── 1분 평균값 계산
├── CSV 생성
└── NAS로 HTTP POST
```

### 📊 데이터 처리 흐름
1. **수집**: 5초마다 32개 센서값 수집
2. **버퍼링**: 60개 샘플을 메모리에 저장
3. **평균**: 1분간 수집된 데이터의 평균 계산
4. **전송**: CSV 형식으로 NAS 서버 전송
5. **로깅**: 연/월/일별 CSV 파일에 저장

## 📁 파일 구조
```
250725_esp32-rs485-nas-logger/
├── esp32_rs485_nas_logger/
│   └── esp32_rs485_nas_logger.ino    # ESP32 마스터 코드
├── arduino_modbus_virtual_sensor/
│   └── arduino_modbus_virtual_sensor.ino  # Arduino 가상 센서
├── upload.php                         # NAS PHP 서버 스크립트
├── README.md                          # 프로젝트 문서
└── 에이아이시드 RS485 Modbus Protocol.pptx  # 프로토콜 사양서
```

## 🚀 설치 및 실행

### 📋 필수 라이브러리
#### ESP32 (Arduino IDE)
- `WiFi` (기본 포함)
- `HTTPClient` (기본 포함)
- `ArduinoJson` (선택사항)

#### Arduino Nano
- `SoftwareSerial` (기본 포함)

### 🔧 ESP32 설정
1. **WiFi 설정 수정**
   ```cpp
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
   ```

2. **NAS 서버 주소 수정**
   ```cpp
   const char* serverUrl = "http://YOUR_NAS_IP/upload.php";
   ```

3. **업로드 설정**
   - 보드: ESP32 Dev Module
   - 포트: 해당 COM 포트
   - 업로드 속도: 115200

### 🔧 Arduino 설정
1. **업로드 설정**
   - 보드: Arduino Nano
   - 프로세서: ATmega328P (Old Bootloader)
   - 포트: 해당 COM 포트

### 🌐 NAS 서버 설정
1. **PHP 스크립트 업로드**
   - `upload.php`를 NAS의 웹 서버 디렉토리에 업로드
   - 웹 서버에서 PHP 실행 권한 확인

2. **폴더 구조 생성**
   ```
   /data/
   ├── 2024/
   │   ├── 01/
   │   │   ├── 01.csv
   │   │   └── 02.csv
   │   └── 02/
   └── 2025/
   ```

## 📊 CSV 데이터 형식

### 📄 파일명
```
/data/YYYY/MM/DD.csv
예: /data/2025/01/27.csv
```

### 📋 헤더
```csv
날짜시간,온도,습도,조도,풍향,풍속,강우
```

### 📊 데이터 예시
```csv
날짜시간,온도,습도,조도,풍향,풍속,강우
2025-01-27 14:30,25.3,59.1,638,148,7.5,0
2025-01-27 14:31,26.1,58.7,645,152,7.8,0
2025-01-27 14:32,25.8,59.3,632,145,7.2,1
```

## 🔍 문제 해결

### ❌ Modbus 통신 오류
**증상**: "Modbus read fail" 또는 CRC Error
**해결책**:
1. RS485 핀 연결 확인
2. 통신 속도 일치 확인 (4800 baud)
3. DE/RE 핀 제어 확인
4. 전원 공급 안정성 확인

### ❌ WiFi 연결 실패
**증상**: "WiFi 연결 실패"
**해결책**:
1. SSID/비밀번호 확인
2. WiFi 신호 강도 확인
3. ESP32 재부팅
4. 정적 IP 설정 고려

### ❌ HTTP 전송 실패
**증상**: "전송 실패: -1"
**해결책**:
1. NAS 서버 주소 확인
2. 네트워크 연결 상태 확인
3. PHP 스크립트 권한 확인
4. 방화벽 설정 확인

### ❌ 데이터 수집 불안정
**증상**: 일부 데이터 누락
**해결책**:
1. Modbus 통신 타이밍 조정
2. 버퍼 크기 증가
3. 재시도 로직 추가
4. 전원 공급 안정성 확인

## 🔧 고급 설정

### ⚡ 성능 최적화
- **통신 속도**: 4800 baud (안정성 우선)
- **수집 주기**: 5초 (데이터 품질과 네트워크 부하 균형)
- **버퍼 크기**: 60개 샘플 (1분 평균용)
- **HTTP 타임아웃**: 15초 (안정적 전송)

### 🔒 보안 고려사항
- **WiFi 암호화**: WPA2/WPA3 사용
- **HTTPS**: 가능시 SSL/TLS 적용
- **방화벽**: NAS 서버 접근 제한
- **정기 백업**: CSV 데이터 백업

## 🚀 확장 가능성

### 📚 참고 자료
- Modbus RTU 프로토콜 사양
- ESP32 개발 가이드
- Arduino SoftwareSerial 라이브러리
- PHP HTTP 처리

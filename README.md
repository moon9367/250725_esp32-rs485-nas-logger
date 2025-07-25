# ESP32 RS485 NAS Logger 시스템 안내

## 시스템 개요

- **아두이노 나노**: RS485 Modbus RTU 슬레이브(가상 센서 데이터 생성)
- **ESP32**: RS485 Modbus RTU 마스터(센서값 수집, 1분 평균 계산, NAS로 HTTP POST 전송)
- **NAS (Synology 등)**: Web Station에서 PHP(upload.php)로 CSV 파일 저장

---

## 전체 흐름

```
[아두이노 나노] ← RS485(Modbus RTU) → [ESP32] ← WiFi → [NAS (upload.php)]
```

---

## 1. 아두이노 나노 (가상 센서)
- SimpleModbusSlave 라이브러리 사용
- 6개 센서값(온도, 습도, 일사, 풍속, 풍향, 감우)을 1초마다 랜덤 생성
- 각 값은 IEEE754 float(워드 내 빅엔디언, 워드 간 리틀엔디언)로 holding 레지스터(0x0000~0x000B)에 저장

### 주요 코드 파일
- `arduino_modbus_virtual_sensor.ino`

---

## 2. ESP32 (Modbus RTU 마스터, NAS 전송)
- ModbusMaster, WiFi, HTTPClient 라이브러리 사용
- 1초마다 아두이노에서 6개 센서값 읽어 누적, 1분 평균값 계산
- CSV 한 줄로 만들어 NAS로 HTTP POST(`csv_line=...`) 전송
- NTP로 시간 동기화

### 주요 코드 파일
- `esp32_rs485_nas_logger.ino`

---

## 3. NAS (upload.php)
- Web Station에 `upload.php` 설치
- POST로 받은 `csv_line`을 연/월별 폴더(`2025/07/2025_07_01.csv`)에 저장
- 파일이 없으면 헤더 자동 추가

### 주요 코드 파일
- `upload.php`

---

## 폴더/파일 구조 예시 (NAS)

```
/web/
  upload.php
  2025/
    07/
      2025_07_01.csv
      2025_07_02.csv
    08/
      2025_08_01.csv
  2026/
    01/
      2026_01_01.csv
```

---

## 테스트 방법

### 1. 아두이노
- 아두이노 IDE로 `arduino_modbus_virtual_sensor.ino` 업로드
- RS485 모듈 연결

### 2. ESP32
- `esp32_rs485_nas_logger.ino` 업로드
- SSID, PW, NAS IP, RS485 핀번호 등 환경에 맞게 수정
- 시리얼 모니터로 로그 확인

### 3. NAS (upload.php)
- `/web/upload.php` 위치에 파일 업로드
- NAS Web Station 활성화, 폴더/파일 권한 확인
- curl 등으로 테스트:
  ```sh
  curl -d "csv_line=2025-07-25 15:04,28.1,63.4,320,260,3.3,0" http://NAS_IP/upload.php
  ```
- 정상 동작 시 연/월별 폴더에 CSV 파일 생성 및 데이터 누적

---

## 참고/확장
- 실 센서 연결 시 아두이노 코드만 수정
- SD카드 저장/하루 1회 전송 등 구조 확장 가능
- NAS upload.php에 인증/보안 강화 필요 시 코드 수정 권장

---

## 문의/지원
- 각 단계별 상세 코드, 회로도, 문제 해결 등 추가 지원이 필요하면 언제든 문의해 주세요! 
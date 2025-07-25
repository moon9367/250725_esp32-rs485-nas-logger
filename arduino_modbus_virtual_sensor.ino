#include <SimpleModbusSlave.h> // 라이브러리 설치 필요

// Modbus holding 레지스터 배열 (12워드 = 6 float)
unsigned int holdingRegs[12];

// float → 2워드 변환 (워드 내 빅엔디언, 워드 간 리틀엔디언)
void floatToRegs(float val, int idx) {
  union {
    float f;
    uint8_t b[4];
  } u;
  u.f = val;
  // 워드 간 리틀엔디언: [word1][word0]
  // 워드 내 빅엔디언: 상위바이트 먼저
  holdingRegs[idx]   = (u.b[2] << 8) | u.b[3]; // word1 (빅엔디언)
  holdingRegs[idx+1] = (u.b[0] << 8) | u.b[1]; // word0 (빅엔디언)
}

void setup() {
  Serial.begin(9600); // RS485 통신속도
  // 슬레이브ID=1, 9600bps, 8N2, holdingRegs 12개
  modbus_configure(&Serial, 9600, SERIAL_8N2, 1, 2, 12, holdingRegs);
}

void loop() {
  // 센서값 생성 (랜덤)
  float temp     = random(200, 350) / 10.0;   // 20.0~35.0℃
  float hum      = random(300, 900) / 10.0;   // 30~90%
  float solar    = random(0, 1000);           // 0~1000 Lux
  float wind_spd = random(0, 150) / 10.0;     // 0~15.0 m/s
  float wind_dir = random(0, 360);            // 0~359°
  float rain     = random(0, 2);              // 0 or 1

  // holding 레지스터에 저장
  floatToRegs(temp,     0);  // 0x0000~0x0001
  floatToRegs(hum,      2);  // 0x0002~0x0003
  floatToRegs(solar,    4);  // 0x0004~0x0005
  floatToRegs(wind_spd, 6);  // 0x0006~0x0007
  floatToRegs(wind_dir, 8);  // 0x0008~0x0009
  floatToRegs(rain,    10);  // 0x000A~0x000B

  modbus_update();
  delay(1000); // 1초마다 값 갱신
} 
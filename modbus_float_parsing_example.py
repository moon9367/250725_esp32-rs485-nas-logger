#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Modbus RTU Float 파싱 예제
워드 스왑(리틀 엔디언 워드 순서) 적용
"""

import struct
from pymodbus.client.sync import ModbusSerialClient as ModbusClient

def parse_float_with_word_swap(high_word, low_word):
    """
    Modbus RTU Float 파싱 (워드 스왑 적용)
    워드 내부는 빅 엔디언, 워드 순서는 리틀 엔디언
    
    Args:
        high_word (int): 상위 워드 (16bit)
        low_word (int): 하위 워드 (16bit)
    
    Returns:
        float: 파싱된 Float 값
    """
    # 워드 스왑: [하위워드][상위워드] → [상위워드][하위워드]
    # 각 워드는 Big-Endian으로 처리
    bytes_data = bytearray([
        (high_word >> 8) & 0xFF,  # 상위워드의 상위바이트
        high_word & 0xFF,         # 상위워드의 하위바이트
        (low_word >> 8) & 0xFF,   # 하위워드의 상위바이트
        low_word & 0xFF           # 하위워드의 하위바이트
    ])
    
    # Big-Endian으로 Float 해석
    return struct.unpack('>f', bytes_data)[0]

def parse_float_from_modbus_response(response_data, sensor_addr, start_addr):
    """
    Modbus 응답에서 특정 센서 주소의 Float 값 파싱
    
    Args:
        response_data (list): Modbus 응답 바이트 배열
        sensor_addr (int): 센서 레지스터 주소
        start_addr (int): Modbus 요청 시작 주소
    
    Returns:
        float: 파싱된 센서 값
    """
    # 시작주소 + 오프셋(레지스터 번호 차이) × 2바이트로 정확한 인덱스 계산
    offset = sensor_addr - start_addr
    byte_index = 3 + (offset * 2)  # 3은 Modbus 헤더 크기
    
    print(f"센서 주소 {sensor_addr} 파싱: 오프셋={offset}, 바이트인덱스={byte_index}")
    
    if byte_index + 3 >= len(response_data):
        print(f"주소 {sensor_addr}: 응답 길이 부족 ({len(response_data)} < {byte_index + 4})")
        return 0.0
    
    # 워드 2개 추출 (Big-Endian)
    high_word = (response_data[byte_index] << 8) | response_data[byte_index + 1]
    low_word = (response_data[byte_index + 2] << 8) | response_data[byte_index + 3]
    
    print(f"주소 {sensor_addr} 원시 워드: {high_word:04X} {low_word:04X}")
    print(f"주소 {sensor_addr} 원시 바이트: {response_data[byte_index]:02X} {response_data[byte_index + 1]:02X} {response_data[byte_index + 2]:02X} {response_data[byte_index + 3]:02X}")
    
    # 워드 스왑 적용하여 Float 변환
    result = parse_float_with_word_swap(high_word, low_word)
    
    print(f"주소 {sensor_addr} 워드 스왑 후: {low_word:04X} {high_word:04X}")
    print(f"주소 {sensor_addr} Float 값: {result:.2f}")
    
    return result

def example_usage():
    """
    사용 예제
    """
    print("=== Modbus RTU Float 파싱 예제 ===\n")
    
    # 예제: BC C0 41 D0 → 41 D0 BC C0로 워드 스왑하여 26.0 해석
    print("1. 예제 데이터 테스트:")
    print("원시 데이터: BC C0 41 D0")
    
    # Big-Endian으로 워드 추출
    high_word = 0xBCC0  # BC C0
    low_word = 0x41D0   # 41 D0
    
    result = parse_float_with_word_swap(high_word, low_word)
    print(f"워드 스왑 후 Float 값: {result:.1f}°C")
    print()
    
    # 실제 Modbus 응답 예제
    print("2. 실제 Modbus 응답 파싱 예제:")
    
    # 예제 응답 데이터 (시작주소 190, 센서 주소들 포함)
    example_response = [
        0x05, 0x03, 0x60,  # Modbus 헤더 (Slave ID, Function, Byte Count)
        # 주소 190-191: 00 00 00 00
        0x00, 0x00, 0x00, 0x00,
        # 주소 192-193: 00 00 00 00  
        0x00, 0x00, 0x00, 0x00,
        # 주소 194-195: BC C0 41 D0 (온도 26.0°C)
        0xBC, 0xC0, 0x41, 0xD0,
        # ... 더 많은 데이터
    ]
    
    # 센서 주소 정의
    sensor_addresses = {
        "온도": 203,    # TEMP_ADDR
        "습도": 212,    # HUMID_ADDR
        "감우": 218,    # RAIN_ADDR
        "일사": 227,    # LIGHT_ADDR
        "풍속": 230,    # WIND_SPD_ADDR
        "풍향": 233     # WIND_DIR_ADDR
    }
    
    start_addr = 190  # START_ADDR
    
    print(f"시작 주소: {start_addr}")
    print("센서별 파싱 결과:")
    
    for sensor_name, sensor_addr in sensor_addresses.items():
        try:
            value = parse_float_from_modbus_response(example_response, sensor_addr, start_addr)
            print(f"  {sensor_name} ({sensor_addr}): {value:.2f}")
        except Exception as e:
            print(f"  {sensor_name} ({sensor_addr}): 오류 - {e}")
        print()

def pymodbus_example():
    """
    pymodbus를 사용한 실제 Modbus 통신 예제
    """
    print("3. pymodbus 실제 통신 예제:")
    
    # Modbus RTU 클라이언트 설정
    client = ModbusClient(
        method='rtu',
        port='COM3',  # 시리얼 포트 (Windows) 또는 '/dev/ttyUSB0' (Linux)
        baudrate=9600,
        parity='N',
        stopbits=1,
        bytesize=8,
        timeout=1
    )
    
    try:
        # Modbus 연결
        if client.connect():
            print("Modbus RTU 연결 성공")
            
            # 연속 레지스터 읽기 (190부터 96개 레지스터)
            start_addr = 190
            count = 96
            slave_id = 5
            
            result = client.read_holding_registers(start_addr, count, unit=slave_id)
            
            if not result.isError():
                print(f"레지스터 읽기 성공: {len(result.registers)}개 워드")
                
                # 센서별 Float 값 파싱
                sensor_addresses = {
                    "온도": 203,
                    "습도": 212,
                    "감우": 218,
                    "일사": 227,
                    "풍속": 230,
                    "풍향": 233
                }
                
                print("\n센서 값 파싱 결과:")
                for sensor_name, sensor_addr in sensor_addresses.items():
                    offset = sensor_addr - start_addr
                    if offset * 2 + 1 < len(result.registers):
                        high_word = result.registers[offset * 2]
                        low_word = result.registers[offset * 2 + 1]
                        
                        value = parse_float_with_word_swap(high_word, low_word)
                        print(f"  {sensor_name} ({sensor_addr}): {value:.2f}")
                    else:
                        print(f"  {sensor_name} ({sensor_addr}): 데이터 범위 초과")
            else:
                print(f"Modbus 읽기 오류: {result}")
        else:
            print("Modbus RTU 연결 실패")
            
    except Exception as e:
        print(f"오류 발생: {e}")
    finally:
        client.close()

if __name__ == "__main__":
    example_usage()
    print("\n" + "="*50 + "\n")
    # pymodbus_example()  # 실제 하드웨어가 있을 때 주석 해제

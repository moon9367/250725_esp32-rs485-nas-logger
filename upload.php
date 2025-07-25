<?php
// 1. POST로 csv_line 파라미터 받기
$csv_line = $_POST['csv_line'] ?? '';
if (!$csv_line) {
    http_response_code(400);
    echo "csv_line 파라미터 없음";
    exit;
}

$year = date('Y');
$month = date('m');
$date = date('Y_m_d'); // 2025_07_01 형식
$dir = __DIR__ . "/$year/$month";
if (!is_dir($dir)) {
    mkdir($dir, 0777, true); // 폴더가 없으면 생성
}
$file = "$dir/{$date}.csv";

// 3. 파일이 없으면 헤더 추가
if (!file_exists($file)) {
    file_put_contents($file, "datetime,temp,hum,solar,wind_dir,wind_spd,rain\n", FILE_APPEND);
}

// 4. csv_line 추가
file_put_contents($file, $csv_line . "\n", FILE_APPEND);

// 5. 응답
echo "OK";
?> 
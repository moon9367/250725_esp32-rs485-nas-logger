<?php
/**
 * ESP32 RS485 Modbus 데이터 로거 - 향상된 버전
 * 범위 확장 및 필터링 지원
 * JSON 및 CSV 형식 지원
 */

header('Content-Type: application/json; charset=utf-8');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: POST, GET, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');

// OPTIONS 요청 처리 (CORS preflight)
if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit;
}

// 에러 리포팅 설정 (개발 환경에서만)
error_reporting(E_ALL);
ini_set('display_errors', 1);

class DataLogger {
    private $dataDir;
    private $maxFileSize = 10 * 1024 * 1024; // 10MB
    
    public function __construct() {
        $this->dataDir = __DIR__ . '/data';
        $this->ensureDataDirectory();
    }
    
    /**
     * 데이터 디렉토리 생성 및 권한 설정
     */
    private function ensureDataDirectory() {
        if (!is_dir($this->dataDir)) {
            mkdir($this->dataDir, 0755, true);
        }
        
        // 연도별 디렉토리 생성
        $currentYear = date('Y');
        $yearDir = $this->dataDir . '/' . $currentYear;
        if (!is_dir($yearDir)) {
            mkdir($yearDir, 0755, true);
        }
        
        // 월별 디렉토리 생성
        $currentMonth = date('m');
        $monthDir = $yearDir . '/' . $currentMonth;
        if (!is_dir($monthDir)) {
            mkdir($monthDir, 0755, true);
        }
    }
    
    /**
     * 로그 메시지 기록
     */
    private function logMessage($level, $message) {
        $logFile = $this->dataDir . '/logger.log';
        $timestamp = date('Y-m-d H:i:s');
        $logEntry = "[{$timestamp}] [{$level}] {$message}" . PHP_EOL;
        file_put_contents($logFile, $logEntry, FILE_APPEND | LOCK_EX);
    }
    
    /**
     * 입력 데이터 검증
     */
    private function validateData($data) {
        // 기본 검증
        if (empty($data)) {
            return ['valid' => false, 'error' => '빈 데이터'];
        }
        
        // 타임스탬프 확인
        if (!isset($data['timestamp'])) {
            return ['valid' => false, 'error' => '타임스탬프 누락'];
        }
        
        // 데이터 필드 확인
        if (!isset($data['data']) && !isset($data['csv_line'])) {
            return ['valid' => false, 'error' => '데이터 필드 누락'];
        }
        
        return ['valid' => true];
    }
    
    /**
     * 파일 크기 관리
     */
    private function rotateFileIfNeeded($filePath) {
        if (file_exists($filePath) && filesize($filePath) > $this->maxFileSize) {
            $timestamp = date('Ymd_His');
            $backupPath = $filePath . '.' . $timestamp . '.bak';
            rename($filePath, $backupPath);
            
            // 백업 파일 압축 (gzip 가능한 경우)
            if (function_exists('gzopen')) {
                $gz = gzopen($filePath . '.gz');
                $original = fopen($backupPath, 'rb');
                if ($gz && $original) {
                    stream_copy_to_stream($original, $gz);
                    fclose($gz);
                    fclose($original);
                    unlink($backupPath); // 원본 삭제
                }
            }
            
            $this->logMessage('INFO', "파일 로테이션: {$filePath}");
        }
    }
    
    /**
     * CSV 형식 데이터 처리
     */
    private function processCsvData($csvLine, $metadata = []) {
        $currentDate = date('Y-m-d');
        $filePath = $this->dataDir . '/' . date('Y') . '/' . date('m') . "/{$currentDate}.csv";
        
        // 파일 로테이션 체크
        $this->rotateFileIfNeeded($filePath);
        
        // 헤더 추가 (파일이 없는 경우)
        if (!file_exists($filePath)) {
            $headers = [
                'timestamp',
                'temperature',
                'humidity', 
                'rain',
                'illuminance',
                'wind_speed',
                'wind_direction'
            ];
            
            // 필터링된 값들을 위한 동적 헤더 생성
            if (isset($metadata['target_addresses'])) {
                $addresses = $metadata['target_addresses'];
                $additionalHeaders = [];
                for ($i = 0; $i < count($addresses); $i++) {
                    $additionalHeaders[] = "register_" . $addresses[$i];
                }
                $headers = array_merge($headers, $additionalHeaders);
            }
            
            file_put_contents($filePath, implode(',', $headers) . PHP_EOL, FILE_APPEND | LOCK_EX);
        }
        
        // 데이터 추가
        file_put_contents($filePath, $csvLine . PHP_EOL, FILE_APPEND | LOCK_EX);
        
        $this->logMessage('INFO', "CSV 데이터 저장됨: " . substr($csvLine, 0, 50) . "...");
        
        return [
            'success' => true,
            'file' => $filePath,
            'size' => filesize($filePath)
        ];
    }
    
    /**
     * JSON 형식 데이터 처리
     */
    private function processJsonData($data, $metadata = []) {
        // 메타데이터 추가
        $enrichedData = array_merge($data, [
            'processed_at' => date('Y-m-d H:i:s'),
            'server_ip' => $_SERVER['SERVER_ADDR'] ?? 'unknown',
            'user_agent' => $_SERVER['HTTP_USER_AGENT'] ?? 'unknown'
        ]);
        
        // JSON 로그 파일에 저장
        $jsonFile = $this->dataDir . '/' . date('Y') . '/' . date('m') . '/' . date('Y-m-d') . '_raw.json';
        
        // 파일 로테이션 체크
        $this->rotateFileIfNeeded($jsonFile);
        
        file_put_contents($jsonFile, json_encode($enrichedData, JSON_UNESCAPED_UNICODE) . PHP_EOL, FILE_APPEND | LOCK_EX);
        
        $this->logMessage('INFO', "JSON 데이터 저장됨");
        
        return [
            'success' => true,
            'file' => $jsonFile,
            'size' => filesize($jsonFile)
        ];
    }
    
    /**
     * 요약 통계 생성
     */
    private function generateSummary($data) {
        $summary = [
            'timestamp' => date('Y-m-d H:i:s'),
            'data_count' => 1,
            'slave_id' => $data['slave_id'] ?? null,
            'target_addresses' => $data['target_addresses'] ?? null
        ];
        
        // 데이터가 있다면 통계 계산
        if (isset($data['data']) && is_array($data['data'])) {
            $values = array_column($data['data'], 'value');
            $summary['stats'] = [
                'min' => count($values) > 0 ? min($values) : null,
                'max' => count($values) > 0 ? max($values) : null,
                'avg' => count($values) > 0 ? array_sum($values) / count($values) : null,
                'count' => count($values)
            ];
        }
        
        return $summary;
    }
    
    /**
     * 메인 처리 함수
     */
    public function handleRequest() {
        try {
            // 요청 메서드 확인
            if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
                http_response_code(405);
                return [
                    'success' => false,
                    'error' => 'POST 메서드만 지원됩니다.',
                    'method' => $_SERVER['REQUEST_METHOD']
                ];
            }
            
            // 입력 데이터 받기
            $rawInput = file_get_contents('php://input');
            
            if (empty($rawInput)) {
                http_response_code(400);
                return [
                    'success' => false,
                    'error' => '요청 본문이 비어있습니다.'
                ];
            }
            
            // JSON 파싱 시도
            $data = null;
            $metadata = [];
            
            // JSON 형식인지 확인
            if (strpos(trim($rawInput), '{') === 0) {
                $data = json_decode($rawInput, true);
                if (json_last_error() !== JSON_ERROR_NONE) {
                    http_response_code(400);
                    return [
                        'success' => false,
                        'error' => 'JSON 파싱 오류: ' . json_last_error_msg()
                    ];
                }
                
                // 메타데이터 추출
                $metadata = [
                    'target_addresses' => $data['target_addresses'] ?? null,
                    'slave_id' => $data['slave_id'] ?? null
                ];
                
            } else {
                // CSV 형식으로 처리
                $csvLine = trim($rawInput);
                $data = ['csv_line' => $csvLine];
            }
            
            // 데이터 검증
            $validation = $this->validateData($data);
            if (!$validation['valid']) {
                http_response_code(400);
                return [
                    'success' => false,
                    'error' => $validation['error']
                ];
            }
            
            $results = [];
            
            // CSV 데이터 처리
            if (isset($data['csv_line'])) {
                $csvResult = $this->processCsvData($data['csv_line'], $metadata);
                $results['csv'] = $csvResult;
            }
            
            // JSON 데이터 처리
            if (isset($data['data']) || isset($data['timestamp'])) {
                $jsonResult = $this->processJsonData($data, $metadata);
                $results['json'] = $jsonResult;
            }
            
            // 요약 통계 생성
            $summary = $this->generateSummary($data);
            $results['summary'] = $summary;
            
            // 성공 응답
            return [
                'success' => true,
                'message' => '데이터가 성공적으로 저장되었습니다.',
                'timestamp' => date('Y-m-d H:i:s'),
                'results' => $results,
                'server_info' => [
                    'version' => '2.0',
                    'php_version' => PHP_VERSION,
                    'memory_usage' => memory_get_usage(true),
                    'max_file_size' => $this->maxFileSize
                ]
            ];
            
        } catch (Exception $e) {
            http_response_code(500);
            $this->logMessage('ERROR', "예외 발생: " . $e->getMessage());
            
            return [
                'success' => false,
                'error' => '서버 내부 오류',
                'message' => $e->getMessage(),
                'timestamp' => date('Y-m-d H:i:s')
            ];
        }
    }
    
    /**
     * 상태 정보 조회
     */
    public function getStatus() {
        $dataFiles = glob($this->dataDir . '/**/*.csv');
        $jsonFiles = glob($this->dataDir . '/**/*.json');
        
        $totalSize = 0;
        foreach (array_merge($dataFiles, $jsonFiles) as $file) {
            $totalSize += filesize($file);
        }
        
        return [
            'server_status' => 'running',
            'timestamp' => date('Y-m-d H:i:s'),
            'data_files' => count($dataFiles),
            'json_files' => count($jsonFiles),
            'total_size' => $totalSize,
            'data_directory' => $this->dataDir,
            'php_version' => PHP_VERSION,
            'memory_usage' => memory_get_usage(true)
        ];
    }
}

// 실행
try {
    $logger = new DataLogger();
    
    // GET 요청인 경우 상태 정보 반환
    if ($_SERVER['REQUEST_METHOD'] === 'GET') {
        echo json_encode($logger->getStatus(), JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT);
    } else {
        // POST 요청 처리
        $result = $logger->handleRequest();
        echo json_encode($result, JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT);
    }
    
} catch (Exception $e) {
    http_response_code(500);
    echo json_encode([
        'success' => false,
        'error' => 'Critical error',
        'message' => $e->getMessage(),
        'timestamp' => date('Y-m-d H:i:s')
    ], JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT);
}
?>

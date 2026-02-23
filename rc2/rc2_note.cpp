// 引入必要的函式庫
#include <WiFi.h>             // 提供 ESP32 建立 Wi-Fi 連線或熱點的功能
#include <WebServer.h>        // 提供 HTTP 網頁伺服器功能，用於載入控制介面
#include <WebSocketsServer.h> // 提供 WebSocket 伺服器功能，用於低延遲的即時遙控訊號傳輸

// 設定 ESP32 建立的 Wi-Fi 熱點 (AP 模式) 名稱與密碼
const char* ssid = "RC2_Car";
const char* password = "12345678";

// 初始化伺服器物件
WebServer server(80);                     // 網頁伺服器運行於標準的 Port 80
WebSocketsServer webSocket = WebSocketsServer(81); // WebSocket 伺服器運行於 Port 81

// 定義直流馬達驅動板 (例如 L298N) 的控制腳位
const int PIN_MOTOR_ENA = 14;  // ENA: 負責控制馬達轉速 (使用 PWM 訊號)
const int PIN_MOTOR_IN1 = 26; // IN1: 負責控制馬達正反轉的方向腳位 1
const int PIN_MOTOR_IN2 = 27; // IN2: 負責控制馬達正反轉的方向腳位 2

// 定義伺服馬達 (負責前輪轉向) 的控制腳位
const int PIN_SERVO = 13;

// 定義伺服馬達的作動角度參數 (可依據實際車體機械結構微調)
const int SERVO_ANGLE_LEFT = 135;  // 方向盤打到底的左轉角度
const int SERVO_ANGLE_RIGHT = 45;  // 方向盤打到底的右轉角度
const int SERVO_ANGLE_CENTER = 90; // 方向盤回正的置中角度

// 將前端網頁介面的 HTML, CSS 與 JavaScript 程式碼儲存於 ESP32 的快閃記憶體 (PROGMEM) 中，節省 RAM 空間
const char index_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="zh-TW">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <title>ESP32 雙桿遙控 (WS版)</title>
    <style>
        /* 基礎版面設定：深色背景、置中對齊，並禁用原生的滑動與更新行為 (避免操作搖桿時畫面跟著拉扯) */
        body {
            background-color: #2c3e50; color: white; font-family: sans-serif;
            margin: 0; height: 100vh; display: flex; flex-direction: column; align-items: center;
            touch-action: none; overscroll-behavior: none; overflow: hidden;
        }
        h2 { margin-top: 10px; font-size: 1.2rem; }
        .status { color: #f1c40f; font-size: 0.9rem; margin-bottom: 0px; }
        
        /* 搖桿控制區塊佈局 */
        .controller-area {
            display: flex; justify-content: space-between; align-items: center;
            width: 100%; height: 60%; padding: 0 40px; box-sizing: border-box; /* 加上 box-sizing 防止跑版 */
        }
        .slider-wrapper {
            position: relative; display: flex; justify-content: center; align-items: center;
            width: 40%; height: 300px; background: rgba(0,0,0,0.2); border-radius: 20px;
        }
        
        /* 搖桿標籤設定：透過絕對定位將文字放在左下角與右下角 */
        .label {
            position: absolute; pointer-events: none; font-weight: bold; opacity: 0.6; z-index: 10;
        }
        .label-left{ bottom: 15px; left: 15px; text-align: left; }
        .label-right{ bottom: 15px; right: 15px; text-align: right; }
        
        /* 客製化滑桿 (模擬實體遙控器搖桿) 外觀 */
        input[type=range] {
            -webkit-appearance: none; background: transparent; cursor: pointer; z-index: 5;
        }
        input[type=range]::-webkit-slider-runnable-track {
            background: #7f8c8d; border-radius: 5px; height: 12px;
        }
        input[type=range]::-webkit-slider-thumb {
            -webkit-appearance: none; border-radius: 50%; box-shadow: 0 0 10px rgba(0,0,0,0.5);
            margin-top: -24px; height: 60px; width: 60px;
        }
        
        /* 轉向滑桿 (橫向) 設定 */
        #steering { width: 90%; }
        #steering::-webkit-slider-thumb { background: #3498db; }
        
        /* 油門滑桿設定：將預設的橫向滑桿旋轉 -90 度變成直向 */
        #throttle { transform: rotate(-90deg); width: 350px; }
        #throttle::-webkit-slider-thumb { background: #e74c3c; }
    </style>
</head>
<body>
    <h2>RC2搖桿 (WS極速版)</h2>
    <div class="status" id="debug-text">連線中...</div>
    <div class="controller-area">
        <div class="slider-wrapper">
            <span class="label label-left">轉向</span>
            <input type="range" id="steering" min="-100" max="100" value="0">
        </div>
        <div class="slider-wrapper">
            <span class="label label-right">油門</span>
            <input type="range" id="throttle" min="-255" max="255" value="0">
        </div>
    </div>
<script>
        // 取得 DOM 元素
        const throttleInput = document.getElementById('throttle');
        const steeringInput = document.getElementById('steering');
        const debugText = document.getElementById('debug-text');
        
        // 儲存當前車輛狀態 (speed: 油門大小, turn: 轉向角度)
        let state = { speed: 0, turn: 0 };
        
        // 傳送頻率控制機制 (避免短時間發送過多封包導致 ESP32 當機)
        let lastSendTime = 0;
        const SEND_INTERVAL = 40; // 限制每 40 毫秒最多發送一次訊號 (約 25fps)

        // 建立 WebSocket 連線，動態抓取當前網頁的 IP (即 ESP32 的 IP) 並連線至 Port 81
        const socket = new WebSocket('ws://' + location.hostname + ':81/');

        // WebSocket 連線成功時的事件
        socket.onopen = function() {
            debugText.innerText = "● 已連線 (WebSocket Ready)";
            debugText.style.color = "#2ecc71";
        };

        // WebSocket 連線斷開時的事件
        socket.onclose = function() {
            debugText.innerText = "× 連線中斷";
            debugText.style.color = "#e74c3c";
        };

        // 將遙控數據發送給 ESP32 的核心函式
        function sendData(force = false) {
            const now = Date.now();

            // 若非強制發送 (force=true)，且距離上次發送時間小於設定的間隔，則不發送
            if (!force && (now - lastSendTime < SEND_INTERVAL)) {
                return;
            }

            lastSendTime = now;

            // 更新除錯文字
            debugText.innerText = `轉向: ${state.turn} | 油門: ${state.speed} `;
            
            // 透過 WebSocket 發送數據，格式為 "油門,轉向" (例如: "255,-50")
            if (socket.readyState === WebSocket.OPEN) {
                socket.send(`${state.speed},${state.turn}`);
            }
        }

        // 處理滑桿拖曳事件
        function handleInput(e) {
            const target = e.target;
            if (target.id === 'throttle') state.speed = target.value;
            else if (target.id === 'steering') state.turn = target.value;
            
            sendData(false); // 一般拖曳時受發送頻率限制
        }

        // 處理手指放開時，搖桿自動回正的事件 (類似實體遙控器的彈簧機制)
        function resetSlider(e) {
            const target = e.target;
            target.value = 0; // 將滑桿 UI 歸零
            if (target.id === 'throttle') state.speed = 0;
            if (target.id === 'steering') state.turn = 0;

            sendData(true); // 放開時強制立即發送停止/回正訊號，確保安全
        }

        // 監聽滑桿數值改變事件
        throttleInput.addEventListener('input', handleInput);
        steeringInput.addEventListener('input', handleInput);
        
        // 監聽滑鼠放開 (電腦端) 或手指離開 (手機端) 事件，觸發回正機制
        const endEvents = ['mouseup', 'touchend'];
        endEvents.forEach(evt => {
            throttleInput.addEventListener(evt, resetSlider);
            steeringInput.addEventListener(evt, resetSlider);
        });
        
        // 禁用網頁右鍵選單 (避免長按時誤觸系統選單)
        document.addEventListener('contextmenu', event => event.preventDefault());
    </script>
</body>
</html>
)=====";

/* * 伺服馬達轉向控制函式
 * angle 範圍限制在 0 ~ 180 度之間
 */
void setServoAngle(int angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;

    // 將角度 (0~180) 映射為標準伺服馬達的脈衝寬度 (500~2500 微秒)
    long pulseWidth = map(angle, 0, 180, 500, 2500);

    // 將脈衝寬度轉換為 ESP32 的 16-bit PWM 佔空比 (0~65535)
    // 週期為 20000 微秒 (50Hz)
    int duty = (pulseWidth * 65536) / 20000;

    ledcWrite(PIN_SERVO, duty); // 輸出 PWM 訊號至伺服馬達
}

/* * 直流馬達動力控制函式
 * speed 範圍約為 -255 到 255
 */
void setMotorSpeed(int speed) {
    if (speed > 0) {
        // 正轉：IN1 為高電位，IN2 為低電位
        digitalWrite(PIN_MOTOR_IN1, HIGH);
        digitalWrite(PIN_MOTOR_IN2, LOW);
        ledcWrite(PIN_MOTOR_ENA, speed); // 輸出速度 PWM
    }
    else if (speed < 0) {
        // 反轉：IN1 為低電位，IN2 為高電位
        digitalWrite(PIN_MOTOR_IN1, LOW);
        digitalWrite(PIN_MOTOR_IN2, HIGH);
        ledcWrite(PIN_MOTOR_ENA, abs(speed)); // 速度取絕對值後輸出 PWM
    }
    else {
        // 停止：雙腳位皆為低電位，並將 PWM 設為 0
        digitalWrite(PIN_MOTOR_IN1, LOW);
        digitalWrite(PIN_MOTOR_IN2, LOW);
        ledcWrite(PIN_MOTOR_ENA, 0);
    }
}

/*
 * WebSocket 事件處理器
 * 負責接收手機端傳來的字串指令，解析後控制馬達
 */
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    // 檢查收到的封包是否為純文字
    if (type == WStype_TEXT) {
        String text = (char*)payload; // 將封包轉換為字串

        // 尋找分隔符號 ',' 的位置
        int commaIndex = text.indexOf(',');

        // 確保格式正確（包含逗號）
        if (commaIndex > 0) {
            // 切割字串，解析出油門 (speed) 與轉向 (turn) 的數值
            int speed = text.substring(0, commaIndex).toInt();
            int turn = text.substring(commaIndex + 1).toInt();

            // 將手機端傳來的轉向數值 (-100~100) 映射為實際的伺服馬達角度
            int angle = map(turn, -100, 100, SERVO_ANGLE_LEFT, SERVO_ANGLE_RIGHT);

            // 執行硬體控制
            setMotorSpeed(speed);
            setServoAngle(angle);
        }
    }
}

// 處理 HTTP 根目錄 ("/") 的請求，負責將 HTML 網頁發送給瀏覽器
void handleRoot() {
    server.send(200, "text/html", index_html);
}

// ESP32 開機初始化設定
void setup() {
    Serial.begin(115200); // 開啟序列埠監控 (用於除錯)

    // 設定馬達方向腳位為輸出模式
    pinMode(PIN_MOTOR_IN1, OUTPUT);
    pinMode(PIN_MOTOR_IN2, OUTPUT);

    // 設定 ESP32 PWM (適用於新版 ESP32 Arduino Core 3.x API)
    // 馬達 ENA：頻率 20000Hz，解析度 8-bit (0~255)
    ledcAttach(PIN_MOTOR_ENA, 20000, 8);
    // 伺服馬達：標準頻率 50Hz，解析度提升至 16-bit 以獲得平滑的轉向控制
    ledcAttach(PIN_SERVO, 50, 16);

    // 啟動 Wi-Fi AP (熱點模式)
    WiFi.softAP(ssid, password);
    Serial.println(WiFi.softAPIP()); // 在監控視窗印出 IP 位址 (預設通常為 192.168.4.1)

    // 註冊 HTTP 路由與啟動網頁伺服器
    server.on("/", handleRoot);
    server.begin();

    // 啟動 WebSocket 伺服器並綁定事件處理器
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
}

// 主迴圈
void loop() {
    // 持續監聽並處理 HTTP 請求與 WebSocket 通訊
    server.handleClient();
    webSocket.loop();
}
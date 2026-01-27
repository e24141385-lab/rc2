#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

const char* ssid = "RC2_Car";
const char* password = "12345678";

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

const int PIN_MOTOR_ENA = 2;
const int PIN_MOTOR_IN1 = 26;
const int PIN_MOTOR_IN2 = 27;

const int PIN_SERVO = 13;

const int SERVO_ANGLE_LEFT = 135;
const int SERVO_ANGLE_RIGHT = 45;
const int SERVO_ANGLE_CENTER = 90;

const char index_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="zh-TW">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <title>ESP32 雙桿遙控 (WS版)</title>
    <style>
        body {
            background-color: #2c3e50; color: white; font-family: sans-serif;
            margin: 0; height: 100vh; display: flex; flex-direction: column; align-items: center;
            touch-action: none; overscroll-behavior: none; overflow: hidden;
        }
        h2 { margin-top: 10px; font-size: 1.2rem; }
        .status { color: #f1c40f; font-size: 0.9rem; margin-bottom: 0px; }
        .controller-area {
            display: flex; justify-content: space-between; align-items: center;
            width: 100%; height: 60%; padding: 0 40px; box-sizing: border-box; /* 加上 box-sizing 防止跑版 */
        }
        .slider-wrapper {
            position: relative; display: flex; justify-content: center; align-items: center;
            width: 40%; height: 300px; background: rgba(0,0,0,0.2); border-radius: 20px;
        }
        .label {
            position: absolute; pointer-events: none; font-weight: bold; opacity: 0.6; z-index: 10;
        }
        .label-left{ bottom: 15px; left: 15px; text-align: left; }
        .label-right{ bottom: 15px; right: 15px; text-align: right; }
        
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
        #steering { width: 90%; }
        #steering::-webkit-slider-thumb { background: #3498db; }
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
        const throttleInput = document.getElementById('throttle');
        const steeringInput = document.getElementById('steering');
        const debugText = document.getElementById('debug-text');
        
        let state = { speed: 0, turn: 0 };
        
        let lastSendTime = 0;
        const SEND_INTERVAL = 40; 

        const socket = new WebSocket('ws://' + location.hostname + ':81/');

        socket.onopen = function() {
            debugText.innerText = "● 已連線 (WebSocket Ready)";
            debugText.style.color = "#2ecc71";
        };

        socket.onclose = function() {
            debugText.innerText = "× 連線中斷";
            debugText.style.color = "#e74c3c";
        };

        function sendData(force = false) {
            const now = Date.now();

            if (!force && (now - lastSendTime < SEND_INTERVAL)) {
                return;
            }

            lastSendTime = now;

            debugText.innerText = `轉向: ${state.turn} | 油門: ${state.speed} `;
            
            if (socket.readyState === WebSocket.OPEN) {
                socket.send(`${state.speed},${state.turn}`);
            }
        }

        function handleInput(e) {
            const target = e.target;
            if (target.id === 'throttle') state.speed = target.value;
            else if (target.id === 'steering') state.turn = target.value;
            
            sendData(false);
        }

        function resetSlider(e) {
            const target = e.target;
            target.value = 0;
            if (target.id === 'throttle') state.speed = 0;
            if (target.id === 'steering') state.turn = 0;

            sendData(true);
        }

        throttleInput.addEventListener('input', handleInput);
        steeringInput.addEventListener('input', handleInput);
        
        const endEvents = ['mouseup', 'touchend'];
        endEvents.forEach(evt => {
            throttleInput.addEventListener(evt, resetSlider);
            steeringInput.addEventListener(evt, resetSlider);
        });
        
        document.addEventListener('contextmenu', event => event.preventDefault());
    </script>
</body>
</html>
)=====";

void setServoAngle(int angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    long pulseWidth = map(angle, 0, 180, 500, 2500);
    int duty = (pulseWidth * 65536) / 20000;
    ledcWrite(PIN_SERVO, duty);
}

void setMotorSpeed(int speed) {
    if (speed > 0) {
        digitalWrite(PIN_MOTOR_IN1, HIGH);
        digitalWrite(PIN_MOTOR_IN2, LOW);
        ledcWrite(PIN_MOTOR_ENA, speed);
    }
    else if (speed < 0) {
        digitalWrite(PIN_MOTOR_IN1, LOW);
        digitalWrite(PIN_MOTOR_IN2, HIGH);
        ledcWrite(PIN_MOTOR_ENA, abs(speed));
    }
    else {
        digitalWrite(PIN_MOTOR_IN1, LOW);
        digitalWrite(PIN_MOTOR_IN2, LOW);
        ledcWrite(PIN_MOTOR_ENA, 0);
    }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    if (type == WStype_TEXT) {
        String text = (char*)payload;

        int commaIndex = text.indexOf(',');

        if (commaIndex > 0) {
            int speed = text.substring(0, commaIndex).toInt();
            int turn = text.substring(commaIndex + 1).toInt();

            int angle = map(turn, -100, 100, SERVO_ANGLE_LEFT, SERVO_ANGLE_RIGHT);

            setMotorSpeed(speed);
            setServoAngle(angle);
        }
    }
}

void handleRoot() {
    server.send(200, "text/html", index_html);
}

void setup() {
    Serial.begin(115200);

    pinMode(PIN_MOTOR_IN1, OUTPUT);
    pinMode(PIN_MOTOR_IN2, OUTPUT);

    ledcAttach(PIN_MOTOR_ENA, 20000, 8);
    ledcAttach(PIN_SERVO, 50, 16);

    WiFi.softAP(ssid, password);
    Serial.println(WiFi.softAPIP());

    server.on("/", handleRoot);
    server.begin();

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
}

void loop() {
    server.handleClient();
    webSocket.loop();
}
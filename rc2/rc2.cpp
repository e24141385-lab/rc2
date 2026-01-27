#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "RC2_Car";
const char* password = "12345678";

WebServer server(80);

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
    <title>ESP32 雙桿遙控</title>
    <style>
        body {
            background-color: #2c3e50;
            color: white;
            font-family: sans-serif;
            margin: 0;
            height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
            touch-action: none; 
            overscroll-behavior: none;
            overflow: hidden;
        }
        h2 { margin-top: 10px; font-size: 1.2rem; }
        .status { color: #f1c40f; font-size: 0.9rem; margin-bottom: 0px; }
        .controller-area {
            display: flex;
            justify-content: space-between;
            align-items: center;
            width: 100%;
            height: 60%;
            padding: 0 40px;
        }
        .slider-wrapper {
            position: relative;
            display: flex;
            justify-content: center;
            align-items: center;
            width: 40%;
            height: 300px;
            background: rgba(0,0,0,0.2);
            border-radius: 20px;
        }
        .label {
            position: absolute;
            pointer-events: none;
            font-weight: bold;
            opacity: 0.6;
            z-index: 10;
        }
        .label-left{
            bottom: 15px;
            left: 15px;
            text-align: left;
        }
        .label-right{
            bottom: 15px;
            right: 15px;
            text-align: right;
        }
        input[type=range] {
            -webkit-appearance: none;
            background: transparent;
            cursor: pointer;
            z-index: 5;
        }
        input[type=range]::-webkit-slider-runnable-track {
            background: #7f8c8d;
            border-radius: 5px;
            height: 12px;
        }
        input[type=range]::-webkit-slider-thumb {
            -webkit-appearance: none;
            border-radius: 50%;
            box-shadow: 0 0 10px rgba(0,0,0,0.5);
            margin-top: -24px;
            height: 60px;
            width: 60px;
        }
        #steering { width: 90%; }
        #steering::-webkit-slider-thumb { background: #3498db; }
        #throttle {
            transform: rotate(-90deg);
            width: 350px;
        }
        #throttle::-webkit-slider-thumb { background: #e74c3c; }
    </style>
</head>
<body>
    <h2>RC2搖桿</h2>
    <div class="status" id="debug-text">等待指令...</div>
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

        function sendData() {
            debugText.innerText = `轉向: ${state.turn} | 油門: ${state.speed} `;
            const url = `/data?speed=${state.speed}&turn=${state.turn}`;
            
            fetch(url)
                .then(response => response.text())
                .then(text => console.log('ESP32回應:', text))
                .catch(err => {});
        }

        function handleInput(e) {
            const target = e.target;
            if (target.id === 'throttle') state.speed = target.value;
            else if (target.id === 'steering') state.turn = target.value;
            sendData();
        }

        function resetSlider(e) {
            const target = e.target;
            target.value = 0;
            if (target.id === 'throttle') state.speed = 0;
            if (target.id === 'steering') state.turn = 0;
            sendData();
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

void handleRoot() {
    server.send(200, "text/html", index_html);
}

void handleData() {
    if (server.hasArg("speed") && server.hasArg("turn")) {
        int speed = server.arg("speed").toInt();
        int turn = server.arg("turn").toInt();

        int angle = map(turn, -100, 100, SERVO_ANGLE_LEFT, SERVO_ANGLE_RIGHT);

        setMotorSpeed(speed);
        setServoAngle(angle);

        server.send(200, "text/plain", "OK");
    }
    else {
        server.send(400, "text/plain", "Error");
    }
}

void setup() {
    Serial.begin(115200);

    pinMode(PIN_MOTOR_IN1, OUTPUT);
    pinMode(PIN_MOTOR_IN2, OUTPUT);

    ledcAttach(PIN_MOTOR_ENA, 20000, 8);
    ledcAttach(PIN_SERVO, 50, 16);

    WiFi.softAP(ssid, password);

    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.begin();
}

void loop() {
    server.handleClient();
}
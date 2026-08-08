#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#define IN1 1
#define IN2 2
#define IN3 3
#define IN4 4


const char* ssid     = "manish";
const char* password = "12345678";


const char* CAM_STREAM_URL = "http://10.234.120.149/stream";

WebServer server(80);          
WebSocketsServer webSocket(81); 

unsigned long lastCmdTime = 0;
const unsigned long CMD_TIMEOUT = 400; 
bool moving = false;

String PAGE; //

const char PAGE_TEMPLATE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Robot</title>
<style>
  * { box-sizing:border-box; -webkit-tap-highlight-color:transparent; user-select:none; }
  body{
    margin:0; min-height:100vh; display:flex; flex-direction:column; align-items:center;
    justify-content:center; font-family:'Segoe UI',Arial,sans-serif; color:#fff;
    background:radial-gradient(circle at 50% 0%, #1e2a4a 0%, #0a0e1a 60%, #000 100%);
    padding:20px;
  }
  .card{
    background:rgba(255,255,255,0.06);
    backdrop-filter:blur(14px);
    -webkit-backdrop-filter:blur(14px);
    border:1px solid rgba(255,255,255,0.12);
    border-radius:22px;
    padding:26px 28px 32px;
    box-shadow:0 8px 32px rgba(0,0,0,0.45), inset 0 1px 0 rgba(255,255,255,0.08);
    max-width:340px; width:100%; text-align:center;
  }
  h2{
    margin:0 0 4px; font-size:22px; letter-spacing:0.5px;
    background:linear-gradient(90deg,#4fd1ff,#7c8bff);
    -webkit-background-clip:text; background-clip:text; color:transparent;
  }
  .ip{ font-size:12px; color:#8b96b8; margin-bottom:18px; letter-spacing:0.5px; }
  .status{
    display:inline-block; margin-bottom:20px; font-size:13px; color:#7fffb0;
    padding:4px 12px; border-radius:20px; background:rgba(127,255,176,0.1);
    border:1px solid rgba(127,255,176,0.25);
  }
  .status.disc{ color:#ff8a8a; background:rgba(255,138,138,0.1); border-color:rgba(255,138,138,0.25); }
  .dpad{
    display:grid;
    grid-template-columns:70px 70px 70px;
    grid-template-rows:70px 70px 70px;
    gap:10px;
    justify-content:center;
    margin:0 auto;
  }
  .btn{
    display:flex; align-items:center; justify-content:center;
    font-size:26px; border-radius:16px; border:1px solid rgba(255,255,255,0.15);
    background:linear-gradient(145deg,#232c46,#161c30);
    color:#dfe6ff; cursor:pointer;
    box-shadow:0 4px 10px rgba(0,0,0,0.35), inset 0 1px 0 rgba(255,255,255,0.06);
    transition:transform .08s ease, background .15s ease, box-shadow .15s ease;
  }
  .btn:active, .btn.active{
    transform:scale(0.92);
    background:linear-gradient(145deg,#3a5bff,#2b3fd6);
    box-shadow:0 0 18px rgba(79,140,255,0.6), inset 0 1px 0 rgba(255,255,255,0.15);
  }
  .up{grid-column:2; grid-row:1;}
  .left{grid-column:1; grid-row:2;}
  .stop{grid-column:2; grid-row:2; font-size:14px; font-weight:bold; color:#ff8a8a; letter-spacing:1px;}
  .right{grid-column:3; grid-row:2;}
  .down{grid-column:2; grid-row:3;}
  .stop:active, .stop.active{
    background:linear-gradient(145deg,#ff5b5b,#c0392b);
    box-shadow:0 0 18px rgba(255,91,91,0.6);
    color:#fff;
  }
  .hint{margin-top:18px; font-size:11px; color:#5c6788;}
  .cam-wrap{
    position:relative; width:100%; aspect-ratio:4/3; border-radius:16px; overflow:hidden;
    background:#000; border:1px solid rgba(255,255,255,0.12); margin-bottom:16px;
    box-shadow:0 4px 14px rgba(0,0,0,0.4), inset 0 0 0 1px rgba(255,255,255,0.03);
  }
  .cam-wrap img{ width:100%; height:100%; object-fit:cover; display:block; }
  .cam-badge{
    position:absolute; top:8px; left:8px; font-size:11px; font-weight:bold; letter-spacing:0.5px;
    background:rgba(0,0,0,0.55); color:#ff6b6b; padding:3px 9px; border-radius:12px;
    display:flex; align-items:center; gap:5px;
  }
  .cam-dot{width:7px; height:7px; border-radius:50%; background:#ff6b6b; animation:pulse 1.2s infinite;}
  @keyframes pulse{0%{opacity:1}50%{opacity:0.3}100%{opacity:1}}
  .cam-note{font-size:10px; color:#5c6788; margin:-10px 0 16px;}
</style></head><body>
<div class="card">
  <h2>ESP32 Robot Control</h2>
  <div class="ip">IP: %ROBOT_IP%</div>
  <div class="cam-wrap">
    <span class="cam-badge"><span class="cam-dot"></span>LIVE</span>
    <img src="%CAM_URL%" id="camStream" onerror="this.style.opacity=0.25;">
  </div>
  <div class="cam-note">Dono robot aur camera ab isi mobile hotspot par hain</div>
  <div class="status" id="status">CONNECTING...</div>
  <div class="dpad">
    <button class="btn up" id="btnF">&#9650;</button>
    <button class="btn left" id="btnL">&#9664;</button>
    <button class="btn stop" id="btnS">STOP</button>
    <button class="btn right" id="btnR">&#9654;</button>
    <button class="btn down" id="btnB">&#9660;</button>
  </div>
  <div class="hint">Press &amp; hold to move &bull; release to stop</div>
</div>
<script>
let ws;
let wsReady = false;
let keepAliveTimer = null;

function setStatus(t, disc){
  const el = document.getElementById('status');
  el.innerText = t;
  el.classList.toggle('disc', !!disc);
}

function connectWS(){
  // WebSocketsServer port 81 par chal raha hai, isliye yaha :81 zaroori hai
  ws = new WebSocket("ws://" + location.hostname + ":81/");
  ws.onopen = () => { wsReady = true; setStatus("READY"); };
  ws.onclose = () => { wsReady = false; setStatus("DISCONNECTED - retrying...", true); setTimeout(connectWS, 1000); };
  ws.onerror = () => { ws.close(); };
}
connectWS();

function sendCmd(c){
  if(wsReady){
    ws.send(c);
    const map={F:"FORWARD",B:"BACKWARD",L:"LEFT",R:"RIGHT",S:"STOPPED"};
    setStatus(map[c]||"STOPPED");
  }
}

function startRepeat(c){
  sendCmd(c);
  if(keepAliveTimer) clearInterval(keepAliveTimer);
  keepAliveTimer = setInterval(()=> sendCmd(c), 150);
}
function stopRepeat(){
  if(keepAliveTimer){ clearInterval(keepAliveTimer); keepAliveTimer = null; }
  sendCmd('S');
}

function bindHold(id,code){
  const el=document.getElementById(id);
  const start=(e)=>{e.preventDefault(); el.classList.add('active'); startRepeat(code);};
  const end=(e)=>{e.preventDefault(); el.classList.remove('active'); stopRepeat();};
  el.addEventListener('mousedown',start);
  el.addEventListener('touchstart',start,{passive:false});
  el.addEventListener('mouseup',end);
  el.addEventListener('mouseleave',end);
  el.addEventListener('touchend',end,{passive:false});
  el.addEventListener('touchcancel',end,{passive:false});
}
bindHold('btnF','F');
bindHold('btnB','B');
bindHold('btnL','L');
bindHold('btnR','R');
document.getElementById('btnS').addEventListener('click', stopRepeat);
</script>
</body></html>
)rawliteral";

void stopM(){
  digitalWrite(IN1,0); digitalWrite(IN2,0);
  digitalWrite(IN3,0); digitalWrite(IN4,0);
  moving = false;
}
void forward(){
  digitalWrite(IN1,1); digitalWrite(IN2,0);
  digitalWrite(IN3,1); digitalWrite(IN4,0);
  moving = true;
}
void backward(){
  digitalWrite(IN1,0); digitalWrite(IN2,1);
  digitalWrite(IN3,0); digitalWrite(IN4,1);
  moving = true;
}
void left(){
  digitalWrite(IN1,0); digitalWrite(IN2,1);
  digitalWrite(IN3,1); digitalWrite(IN4,0);
  moving = true;
}
void right(){
  digitalWrite(IN1,1); digitalWrite(IN2,0);
  digitalWrite(IN3,0); digitalWrite(IN4,1);
  moving = true;
}

void applyCmd(char d){
  lastCmdTime = millis();
  switch(d){
    case 'F': forward(); break;
    case 'B': backward(); break;
    case 'L': left(); break;
    case 'R': right(); break;
    default:  stopM(); break;
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length){
  switch(type){
    case WStype_CONNECTED:
      Serial.printf("WS client #%u connected\n", num);
      stopM(); 
      break;
    case WStype_DISCONNECTED:
      Serial.printf("WS client #%u disconnected\n", num);
      stopM(); 
      break;
    case WStype_TEXT:
      if(length >= 1){
        applyCmd((char)payload[0]);
      }
      break;
    default:
      break;
  }
}

void setup(){
  Serial.begin(115200);
  pinMode(IN1,OUTPUT); pinMode(IN2,OUTPUT);
  pinMode(IN3,OUTPUT); pinMode(IN4,OUTPUT);
  stopM();

 
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to hotspot: ");
  Serial.println(ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! Robot IP: ");
  Serial.println(WiFi.localIP());

 
  if (MDNS.begin("esp32robot")) {
    Serial.println("mDNS ready: http://esp32robot.local");
  }

  
  PAGE = String(PAGE_TEMPLATE);
  PAGE.replace("%ROBOT_IP%", WiFi.localIP().toString());
  PAGE.replace("%CAM_URL%", CAM_STREAM_URL);

  server.on("/", [](){ server.send(200, "text/html", PAGE); });
  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void loop(){
  server.handleClient();
  webSocket.loop();


  if(moving && (millis() - lastCmdTime > CMD_TIMEOUT)){
    stopM();
  }
 
  if(WiFi.status() != WL_CONNECTED){
    stopM();
    WiFi.reconnect();
  }
}

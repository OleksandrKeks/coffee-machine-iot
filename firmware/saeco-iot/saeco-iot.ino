/*
 * ============================================
 * Saeco Lirika OTC RI9851/01 — IoT Controller
 * ============================================
 * ESP32-WROOM-32 + 7x PC817 Optocouplers
 * Дублирование кнопок кофемашины через Wi-Fi
 * 
 * GitHub: OleksandrKeks/coffee-machine-iot
 * License: MIT
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// ===== НАСТРОЙКИ Wi-Fi =====
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
const char* HOSTNAME  = "saeco";

// ===== GPIO ПИНЫ (ESP32 → PC817 → Кнопки Saeco) =====
#define PIN_ESPRESSO  23  // D23 → Кнопка Эспрессо (18)
#define PIN_LUNGO     22  // D22 → Кнопка Лунго (19)
#define PIN_CAPPUCCINO 21 // D21 → Кнопка Капучино (22)
#define PIN_LATTE     19  // D19 → Кнопка Латте (23)
#define PIN_STRENGTH  18  // D18 → Кнопка Крепость (20)
#define PIN_MENU       5  // D5  → Кнопка Меню (24)
#define PIN_POWER      4  // D4  → Кнопка Вкл/Выкл (21)

// Массив всех пинов для инициализации
const int BTN_PINS[] = {
  PIN_ESPRESSO, PIN_LUNGO, PIN_CAPPUCCINO, PIN_LATTE,
  PIN_STRENGTH, PIN_MENU, PIN_POWER
};
const int BTN_COUNT = sizeof(BTN_PINS) / sizeof(BTN_PINS[0]);

// ===== ТАЙМИНГИ НАЖАТИЙ (мс) =====
#define PRESS_SHORT     200   // Короткое нажатие
#define PRESS_LONG     3000   // Длинное (программирование дозы)
#define PAUSE_DOUBLE    300   // Пауза между двойным нажатием
#define COOLDOWN       5000   // Минимум между командами напитков

// ===== СОСТОЯНИЕ =====
unsigned long lastBrewTime = 0;
bool brewing = false;
String lastAction = "idle";
String machineStatus = "ready";

WebServer server(80);

// ===== ИМИТАЦИЯ НАЖАТИЯ КНОПКИ =====
void pressButton(int pin, int duration = PRESS_SHORT) {
  digitalWrite(pin, HIGH);
  delay(duration);
  digitalWrite(pin, LOW);
}

// Двойное нажатие (2 порции)
void doublePressButton(int pin) {
  pressButton(pin, PRESS_SHORT);
  delay(PAUSE_DOUBLE);
  pressButton(pin, PRESS_SHORT);
}

// ===== ПРОВЕРКА КУЛДАУНА =====
bool checkCooldown() {
  if (millis() - lastBrewTime < COOLDOWN) {
    server.send(429, "application/json",
      "{\"error\":\"cooldown\",\"wait_ms\":" +
      String(COOLDOWN - (millis() - lastBrewTime)) + "}");
    return false;
  }
  return true;
}

// ===== ВЕБ-СТРАНИЦА (HTML + CSS + JS) =====
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="uk">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Saeco Lirika IoT</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;
background:#1a1a2e;color:#eee;min-height:100vh;display:flex;
flex-direction:column;align-items:center;padding:16px}
h1{font-size:1.4em;margin:8px 0;color:#e94560}
.status{font-size:0.85em;color:#aaa;margin-bottom:16px;
padding:8px 16px;background:#16213e;border-radius:20px}
.status .dot{display:inline-block;width:8px;height:8px;
border-radius:50%;margin-right:6px;background:#4ecca3}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;
width:100%;max-width:360px}
</style>
</head>
<body>
<h1>☕ Saeco Lirika</h1>
<div class="status"><span class="dot" id="dot"></span>
<span id="st">Підключено</span></div>
<div class="grid" id="btns"></div>
<div id="msg" style="margin-top:16px;font-size:0.9em;
color:#4ecca3;min-height:24px"></div>
)rawliteral";

const char INDEX_HTML2[] PROGMEM = R"rawliteral(
<style>
.btn{border:none;border-radius:16px;padding:20px 10px;font-size:1em;
font-weight:600;cursor:pointer;transition:all 0.15s;display:flex;
flex-direction:column;align-items:center;gap:6px;
box-shadow:0 4px 15px rgba(0,0,0,0.3)}
.btn:active{transform:scale(0.95);box-shadow:0 2px 8px rgba(0,0,0,0.3)}
.btn .ico{font-size:2em}
.btn.coffee{background:linear-gradient(135deg,#6f4e37,#8B6914);color:#fff}
.btn.milk{background:linear-gradient(135deg,#e8d5b7,#f5e6cc);color:#3e2723}
.btn.ctrl{background:linear-gradient(135deg,#16213e,#1a1a2e);
color:#e94560;border:2px solid #e94560}
.btn.power{background:linear-gradient(135deg,#4ecca3,#45b393);color:#1a1a2e}
.btn.x2{font-size:0.75em;background:linear-gradient(135deg,#534a8a,#6c5eb5);color:#fff}
.btn:disabled{opacity:0.4;transform:none}
.full{grid-column:1/-1}
.row2{display:grid;grid-template-columns:1fr 1fr;gap:12px;grid-column:1/-1}
</style>
<script>
const B=[
{id:'espresso',ico:'☕',lbl:'Еспресо',cls:'coffee',ep:'/espresso'},
{id:'lungo',ico:'🍵',lbl:'Лунго',cls:'coffee',ep:'/lungo'},
{id:'cappuccino',ico:'🥛',lbl:'Капучіно',cls:'milk',ep:'/cappuccino'},
{id:'latte',ico:'🧋',lbl:'Латте',cls:'milk',ep:'/latte'},
{id:'strength',ico:'💪',lbl:'Міцність',cls:'ctrl',ep:'/strength'},
{id:'menu',ico:'⚙️',lbl:'Меню',cls:'ctrl',ep:'/menu'},
{id:'power',ico:'⏻',lbl:'Увімк/Вимк',cls:'power full',ep:'/power'},
];
const g=document.getElementById('btns');
const m=document.getElementById('msg');
)rawliteral";

const char INDEX_HTML3[] PROGMEM = R"rawliteral(
B.forEach(b=>{
  const el=document.createElement('div');
  // Для напитков добавляем кнопку x2
  if(['espresso','lungo'].includes(b.id)){
    el.className='row2';
    el.innerHTML=`<button class="btn ${b.cls}" onclick="cmd('${b.ep}')">
      <span class="ico">${b.ico}</span>${b.lbl}</button>
      <button class="btn x2" onclick="cmd('${b.ep}?double=1')">
      <span class="ico">${b.ico}${b.ico}</span>x2</button>`;
  } else {
    el.innerHTML=`<button class="btn ${b.cls}" onclick="cmd('${b.ep}')">
      <span class="ico">${b.ico}</span>${b.lbl}</button>`;
  }
  g.appendChild(el);
});
// СТОП кнопка
const s=document.createElement('div');
s.innerHTML=`<button class="btn ctrl full" onclick="cmd('/stop')"
  style="border-color:#ff6b6b;color:#ff6b6b">
  <span class="ico">⏹</span>СТОП</button>`;
g.appendChild(s);

function cmd(ep){
  m.textContent='⏳ Надсилаю...';
  fetch(ep).then(r=>r.json()).then(d=>{
    m.textContent='✅ '+d.action;
    setTimeout(()=>m.textContent='',3000);
  }).catch(e=>{m.textContent='❌ Помилка: '+e});
}
// Статус каждые 5 сек
setInterval(()=>{
  fetch('/status').then(r=>r.json()).then(d=>{
    document.getElementById('st').textContent=d.status;
    document.getElementById('dot').style.background=
      d.status==='ready'?'#4ecca3':'#e94560';
  }).catch(()=>{
    document.getElementById('st').textContent='Офлайн';
    document.getElementById('dot').style.background='#666';
  });
},5000);
</script></body></html>
)rawliteral";

// ===== ОБРАБОТЧИКИ API =====

void handleRoot() {
  String html = String(INDEX_HTML) + String(INDEX_HTML2) + String(INDEX_HTML3);
  server.send(200, "text/html", html);
}

void handleStatus() {
  String json = "{\"status\":\"" + machineStatus + "\","
    "\"last_action\":\"" + lastAction + "\","
    "\"uptime\":" + String(millis() / 1000) + ","
    "\"wifi_rssi\":" + String(WiFi.RSSI()) + ","
    "\"ip\":\"" + WiFi.localIP().toString() + "\"}";
  server.send(200, "application/json", json);
}

void handleEspresso() {
  if (!checkCooldown()) return;
  bool dbl = server.hasArg("double") && server.arg("double") == "1";
  if (dbl) doublePressButton(PIN_ESPRESSO);
  else pressButton(PIN_ESPRESSO);
  lastBrewTime = millis();
  lastAction = dbl ? "espresso x2" : "espresso";
  machineStatus = "brewing";
  server.send(200, "application/json",
    "{\"ok\":true,\"action\":\"" + lastAction + "\"}");
}

void handleLungo() {
  if (!checkCooldown()) return;
  bool dbl = server.hasArg("double") && server.arg("double") == "1";
  if (dbl) doublePressButton(PIN_LUNGO);
  else pressButton(PIN_LUNGO);
  lastBrewTime = millis();
  lastAction = dbl ? "lungo x2" : "lungo";
  machineStatus = "brewing";
  server.send(200, "application/json",
    "{\"ok\":true,\"action\":\"" + lastAction + "\"}");
}

void handleCappuccino() {
  if (!checkCooldown()) return;
  pressButton(PIN_CAPPUCCINO);
  lastBrewTime = millis();
  lastAction = "cappuccino";
  machineStatus = "brewing";
  server.send(200, "application/json",
    "{\"ok\":true,\"action\":\"cappuccino\"}");
}

void handleLatte() {
  if (!checkCooldown()) return;
  pressButton(PIN_LATTE);
  lastBrewTime = millis();
  lastAction = "latte";
  machineStatus = "brewing";
  server.send(200, "application/json",
    "{\"ok\":true,\"action\":\"latte\"}");
}

void handleStrength() {
  pressButton(PIN_STRENGTH);
  lastAction = "strength";
  server.send(200, "application/json",
    "{\"ok\":true,\"action\":\"strength\"}");
}

void handleMenu() {
  pressButton(PIN_MENU);
  lastAction = "menu";
  server.send(200, "application/json",
    "{\"ok\":true,\"action\":\"menu\"}");
}

void handlePower() {
  pressButton(PIN_POWER);
  lastAction = "power";
  machineStatus = (machineStatus == "off") ? "starting" : "off";
  server.send(200, "application/json",
    "{\"ok\":true,\"action\":\"power\"}");
}

void handleStop() {
  // Стоп = нажатие кнопки эспрессо (прерывает подачу)
  pressButton(PIN_ESPRESSO);
  lastAction = "stop";
  machineStatus = "ready";
  server.send(200, "application/json",
    "{\"ok\":true,\"action\":\"stop\"}");
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Saeco Lirika IoT Controller ===");

  // Инициализация GPIO — все LOW (кнопки отпущены)
  for (int i = 0; i < BTN_COUNT; i++) {
    pinMode(BTN_PINS[i], OUTPUT);
    digitalWrite(BTN_PINS[i], LOW);
  }
  Serial.println("GPIO initialized: all buttons released");

  // Подключение к Wi-Fi
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    Serial.print("RSSI: "); Serial.println(WiFi.RSSI());
  } else {
    Serial.println("\nWiFi FAILED! Starting AP mode...");
    WiFi.softAP("Saeco-IoT", "coffee1234");
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
  }

  // mDNS — доступ по http://saeco.local
  if (MDNS.begin(HOSTNAME)) {
    Serial.println("mDNS: http://saeco.local");
  }

  // Регистрация маршрутов
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/espresso", handleEspresso);
  server.on("/lungo", handleLungo);
  server.on("/cappuccino", handleCappuccino);
  server.on("/latte", handleLatte);
  server.on("/strength", handleStrength);
  server.on("/menu", handleMenu);
  server.on("/power", handlePower);
  server.on("/stop", handleStop);

  // CORS для внешних приложений
  server.enableCORS(true);

  server.begin();
  Serial.println("Web server started!");
  Serial.println("=================================");
}

// ===== LOOP =====
void loop() {
  server.handleClient();

  // Автосброс статуса через 60 сек после приготовления
  if (machineStatus == "brewing" &&
      millis() - lastBrewTime > 60000) {
    machineStatus = "ready";
  }

  // Переподключение Wi-Fi при потере связи
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnect = 0;
    if (millis() - lastReconnect > 30000) {
      Serial.println("WiFi lost, reconnecting...");
      WiFi.reconnect();
      lastReconnect = millis();
    }
  }
}

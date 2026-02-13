/*
 * КОФЕМАШИНА ESP32 + TELEGRAM БОТ (КНОПКИ) + ВЕБ-ИНТЕРФЕЙС
 * Telegram: @Keksav_bot
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

const String BOT_TOKEN = "YOUR_TELEGRAM_BOT_TOKEN";
const String CHAT_ID = "YOUR_TELEGRAM_CHAT_ID";
const String TG_URL = "https://api.telegram.org/bot" + BOT_TOKEN;
long lastUpdateId = 0;
unsigned long lastTgCheck = 0;

WiFiClientSecure secureClient;
WebServer server(80);

const int BTN_GREEN=12, BTN_BLUE=14, BTN_YELLOW=27;
const int LED_RED=2, LED_GREEN=4, LED_BLUE=5, LED_YELLOW=18;

bool powerOn=false;
int menuState=0, strengthLevel=1;
bool ledRed=false, ledGreen=false, ledBlue=false, ledYellow=false;

void updateLEDs() {
  digitalWrite(LED_RED, ledRed);
  digitalWrite(LED_GREEN, ledGreen);
  digitalWrite(LED_BLUE, ledBlue);
  digitalWrite(LED_YELLOW, ledYellow);
}

// ============ TELEGRAM С КНОПКАМИ ============

void tgPost(String method, String body) {
  HTTPClient http;
  http.begin(secureClient, TG_URL + "/" + method);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  Serial.println("TG " + method + ": " + String(code));
  http.end();
}

String getKeyboard() {
  String kb = "{\"inline_keyboard\":[";
  kb += "[{\"text\":\"" + String(powerOn ? "🟢 ВИМКНУТИ" : "⚡ УВІМКНУТИ") + "\",\"callback_data\":\"power\"}],";
  kb += "[{\"text\":\"" + String(ledRed ? "🔴 ●" : "🔴 ○") + "\",\"callback_data\":\"red\"},";
  kb += "{\"text\":\"" + String(ledGreen ? "🟢 ●" : "🟢 ○") + "\",\"callback_data\":\"green\"}],";
  kb += "[{\"text\":\"" + String(ledBlue ? "🔵 ●" : "🔵 ○") + "\",\"callback_data\":\"blue\"},";
  kb += "{\"text\":\"" + String(ledYellow ? "🟡 ●" : "🟡 ○") + "\",\"callback_data\":\"yellow\"}],";
  kb += "[{\"text\":\"🔬 Тест\",\"callback_data\":\"test\"},{\"text\":\"🚫 Все вимк\",\"callback_data\":\"alloff\"}],";
  kb += "[{\"text\":\"📊 Статус\",\"callback_data\":\"status\"}]";
  kb += "]}";
  return kb;
}

String getStatusText() {
  String s = "☕ <b>Кофемашина ESP32</b>\n\n";
  s += powerOn ? "✅ УВІМКНЕНА\n" : "⛔ ВИМКНЕНА\n";
  s += "🔴 Червоний: " + String(ledRed ? "ВКЛ" : "вимк") + "\n";
  s += "🟢 Зелений: " + String(ledGreen ? "ВКЛ" : "вимк") + "\n";
  s += "🔵 Синій: " + String(ledBlue ? "ВКЛ" : "вимк") + "\n";
  s += "🟡 Жовтий: " + String(ledYellow ? "ВКЛ" : "вимк") + "\n";
  s += "\nIP: " + WiFi.localIP().toString();
  return s;
}

void sendMainMenu(String chatId) {
  String body = "{\"chat_id\":" + chatId + ",\"text\":\"" + getStatusText() + "\",\"parse_mode\":\"HTML\",\"reply_markup\":" + getKeyboard() + "}";
  tgPost("sendMessage", body);
}

void editMessage(String chatId, String msgId) {
  String body = "{\"chat_id\":" + chatId + ",\"message_id\":" + msgId + ",\"text\":\"" + getStatusText() + "\",\"parse_mode\":\"HTML\",\"reply_markup\":" + getKeyboard() + "}";
  tgPost("editMessageText", body);
}

void answerCallback(String callbackId, String text) {
  String body = "{\"callback_query_id\":\"" + callbackId + "\",\"text\":\"" + text + "\"}";
  tgPost("answerCallbackQuery", body);
}

void handleCallback(String data, String chatId, String msgId, String cbId) {
  String toast = "";

  if (data == "power") {
    powerOn = !powerOn;
    if (!powerOn) { ledRed=ledGreen=ledBlue=ledYellow=false; }
    else { ledGreen=true; }
    toast = powerOn ? "✅ Увімкнено" : "⛔ Вимкнено";
  }
  else if (data == "red") { ledRed=!ledRed; toast=ledRed?"🔴 ВКЛ":"🔴 вимк"; }
  else if (data == "green") { ledGreen=!ledGreen; toast=ledGreen?"🟢 ВКЛ":"🟢 вимк"; }
  else if (data == "blue") { ledBlue=!ledBlue; toast=ledBlue?"🔵 ВКЛ":"🔵 вимк"; }
  else if (data == "yellow") { ledYellow=!ledYellow; toast=ledYellow?"🟡 ВКЛ":"🟡 вимк"; }
  else if (data == "alloff") { ledRed=ledGreen=ledBlue=ledYellow=false; toast="🚫 Все вимкнені"; }
  else if (data == "test") {
    toast = "🔬 Тест...";
    answerCallback(cbId, toast);
    int pins[]={LED_RED,LED_GREEN,LED_BLUE,LED_YELLOW};
    for(int j=0;j<3;j++){for(int i=0;i<4;i++){digitalWrite(pins[i],HIGH);delay(150);digitalWrite(pins[i],LOW);delay(100);}}
    for(int i=0;i<4;i++)digitalWrite(pins[i],HIGH);delay(500);
    for(int i=0;i<4;i++)digitalWrite(pins[i],LOW);
    updateLEDs(); editMessage(chatId, msgId); return;
  }
  else if (data == "status") { toast = "📊 Оновлено"; }

  updateLEDs();
  answerCallback(cbId, toast);
  editMessage(chatId, msgId);
}

void checkTelegram() {
  HTTPClient http;
  String url = TG_URL + "/getUpdates?offset=" + String(lastUpdateId + 1) + "&limit=1&timeout=0";
  http.begin(secureClient, url);
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();
    JsonDocument doc;
    deserializeJson(doc, payload);

    if (doc["result"].size() > 0) {
      JsonObject update = doc["result"][0];
      lastUpdateId = update["update_id"].as<long>();

      // Callback від кнопки
      if (update.containsKey("callback_query")) {
        String data = update["callback_query"]["data"].as<String>();
        String chatId = String(update["callback_query"]["message"]["chat"]["id"].as<long>());
        String msgId = String(update["callback_query"]["message"]["message_id"].as<long>());
        String cbId = update["callback_query"]["id"].as<String>();
        Serial.println("TG CB: " + data);
        handleCallback(data, chatId, msgId, cbId);
      }
      // Текстове повідомлення /start
      else if (update.containsKey("message")) {
        String text = update["message"]["text"].as<String>();
        String chatId = String(update["message"]["chat"]["id"].as<long>());
        if (chatId == CHAT_ID) {
          text.toLowerCase();
          if (text == "/start" || text == "/help" || text == "/menu") {
            sendMainMenu(chatId);
          }
        }
      }
    }
  }
  http.end();
}

// ============ ВЕБ-ІНТЕРФЕЙС ============

String getHTML() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Кофемашина</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;display:flex;justify-content:center;min-height:100vh;padding:20px}
.c{max-width:400px;width:100%}h1{text-align:center;margin:20px 0;font-size:1.5em}
.s{text-align:center;padding:10px;border-radius:8px;margin:10px 0;font-size:1.2em;font-weight:bold}
.on{background:#1b5e20;color:#69f0ae}.off{background:#b71c1c;color:#ef9a9a}
.g{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:15px 0}
.b{padding:20px;border:none;border-radius:12px;font-size:1.1em;cursor:pointer;font-weight:bold;transition:.2s;color:#fff}
.b:active{transform:scale(.95)}
.ro{background:#5c1010}.ri{background:#f44336;box-shadow:0 0 20px #f44336}
.go{background:#1b3a1b}.gi{background:#4caf50;box-shadow:0 0 20px #4caf50}
.bo{background:#0d1b3e}.bi{background:#2196f3;box-shadow:0 0 20px #2196f3}
.yo{background:#3e3a0d}.yi{background:#ffeb3b;color:#000;box-shadow:0 0 20px #ffeb3b}
.cb{width:100%;padding:15px;border:none;border-radius:10px;font-size:1.1em;cursor:pointer;margin:5px 0;font-weight:bold;color:#fff}
.pw{background:#4caf50}.ao{background:#f44336}.ts{background:#ff9800}
.i{background:#16213e;padding:12px;border-radius:8px;margin:10px 0;font-size:.9em}
</style></head><body><div class='c'><h1>☕ Кофемашина</h1>
)rawliteral";
  html+="<div class='s "+String(powerOn?"on":"off")+"'>"+String(powerOn?"✅ УВІМКНЕНА":"⛔ ВИМКНЕНА")+"</div>";
  html+="<div class='i'>TG: @Keksav_bot</div><div class='g'>";
  html+="<button class='b "+String(ledRed?"ri":"ro")+"' onclick=\"location='/t?l=r'\">🔴</button>";
  html+="<button class='b "+String(ledGreen?"gi":"go")+"' onclick=\"location='/t?l=g'\">🟢</button>";
  html+="<button class='b "+String(ledBlue?"bi":"bo")+"' onclick=\"location='/t?l=b'\">🔵</button>";
  html+="<button class='b "+String(ledYellow?"yi":"yo")+"' onclick=\"location='/t?l=y'\">🟡</button></div>";
  html+="<button class='cb pw' onclick=\"location='/p'\">⚡ ВКЛ/ВИМК</button>";
  html+="<button class='cb ts' onclick=\"location='/ts'\">🔬 Тест</button>";
  html+="<button class='cb ao' onclick=\"location='/ao'\">🚫 Все вимк</button>";
  html+="<div class='i'>IP: "+WiFi.localIP().toString()+" | RSSI: "+String(WiFi.RSSI())+" dBm</div>";
  html+="</div></body></html>";
  return html;
}

void hRoot(){server.send(200,"text/html",getHTML());}
void hToggle(){
  String l=server.arg("l");
  if(l=="r")ledRed=!ledRed;if(l=="g")ledGreen=!ledGreen;
  if(l=="b")ledBlue=!ledBlue;if(l=="y")ledYellow=!ledYellow;
  updateLEDs();server.sendHeader("Location","/");server.send(303);
}

void hPower(){powerOn=!powerOn;if(!powerOn)ledRed=ledGreen=ledBlue=ledYellow=false;else ledGreen=true;updateLEDs();server.sendHeader("Location","/");server.send(303);}
void hAllOff(){ledRed=ledGreen=ledBlue=ledYellow=false;updateLEDs();server.sendHeader("Location","/");server.send(303);}
void hTest(){
  int p[]={LED_RED,LED_GREEN,LED_BLUE,LED_YELLOW};
  for(int j=0;j<3;j++)for(int i=0;i<4;i++){digitalWrite(p[i],HIGH);delay(150);digitalWrite(p[i],LOW);delay(100);}
  for(int i=0;i<4;i++)digitalWrite(p[i],HIGH);delay(500);for(int i=0;i<4;i++)digitalWrite(p[i],LOW);
  updateLEDs();server.sendHeader("Location","/");server.send(303);
}

// ============ SETUP & LOOP ============

void setup() {
  Serial.begin(115200);delay(1000);
  Serial.println("\n☕ КОФЕМАШИНА ESP32 + TELEGRAM\n");
  pinMode(BTN_GREEN,INPUT_PULLUP);pinMode(BTN_BLUE,INPUT_PULLUP);pinMode(BTN_YELLOW,INPUT_PULLUP);
  pinMode(LED_RED,OUTPUT);pinMode(LED_GREEN,OUTPUT);pinMode(LED_BLUE,OUTPUT);pinMode(LED_YELLOW,OUTPUT);
  updateLEDs();

  WiFi.begin(ssid,password);
  Serial.print("Wi-Fi: ");Serial.println(ssid);
  int a=0;while(WiFi.status()!=WL_CONNECTED&&a<30){delay(500);Serial.print(".");a++;}
  if(WiFi.status()==WL_CONNECTED){Serial.println("\n✅ Wi-Fi OK!");Serial.print("📡 http://");Serial.println(WiFi.localIP());}
  else Serial.println("\n❌ Wi-Fi FAIL");

  secureClient.setInsecure();
  server.on("/",hRoot);server.on("/t",hToggle);server.on("/p",hPower);server.on("/ao",hAllOff);server.on("/ts",hTest);
  server.begin();
  Serial.println("🌐 Web OK");

  sendMainMenu(CHAT_ID);
  Serial.println("📱 TG OK\n");
}

void loop() {
  server.handleClient();
  if(millis()-lastTgCheck>1000){lastTgCheck=millis();if(WiFi.status()==WL_CONNECTED)checkTelegram();}

  if(digitalRead(BTN_GREEN)==LOW){delay(50);if(digitalRead(BTN_GREEN)==LOW){
    powerOn=!powerOn;if(powerOn)ledGreen=true;else ledRed=ledGreen=ledBlue=ledYellow=false;
    updateLEDs();while(digitalRead(BTN_GREEN)==LOW)delay(10);delay(200);}}

  if(digitalRead(BTN_BLUE)==LOW&&powerOn){delay(50);if(digitalRead(BTN_BLUE)==LOW){
    menuState=(menuState+1)%3;ledBlue=true;updateLEDs();delay(200);ledBlue=false;updateLEDs();
    while(digitalRead(BTN_BLUE)==LOW)delay(10);delay(200);}}

  if(digitalRead(BTN_YELLOW)==LOW&&powerOn){delay(50);if(digitalRead(BTN_YELLOW)==LOW){
    strengthLevel=(strengthLevel%3)+1;ledYellow=true;updateLEDs();delay(200);ledYellow=false;updateLEDs();
    while(digitalRead(BTN_YELLOW)==LOW)delay(10);delay(200);}}
}

// Ceas Smart ESP32 - varianta curată, fără duplicate (ianuarie 2025)
// Pinii și logica ta, cod reorganizat și fără redefiniri

#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ────────────────────────────────────────────────
// Pin definitions
// ────────────────────────────────────────────────
#define PIN_DATA    15
#define PIN_CLK     18
#define PIN_LATCH   5
#define OE_PIN      19
#define DHT_PIN     4
#define BUZZER_PIN  21

const int DIG_PINS[6] = {27, 16, 25, 26, 14, 13};

// ────────────────────────────────────────────────
// Obiecte globale
// ────────────────────────────────────────────────
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7200, 60000);
DHT dht(DHT_PIN, DHT11);
AsyncWebServer server(80);
Preferences prefs;

// ────────────────────────────────────────────────
// Variabile de configurare (citite/salvate în NVS)
// ────────────────────────────────────────────────
int startSecTemp = 10,   durTemp = 5;
int startSecUmid = 20,   durUmid = 5;
int startSecDate = 30,   durDate = 5;
int startSecWeather = 40, durWeather = 5;
int startSecMesaj = 50,  durMesaj = 10;

int alarmHours[3] = {-1, -1, -1};
int alarmMins[3]  = {0, 0, 0};

int brightnessDay   = 4;
int brightnessNight = 2;

String weatherCity  = "Bucharest";
String weatherApiKey = "";
String mesaj        = "Hello";

// ────────────────────────────────────────────────
// Variabile runtime
// ────────────────────────────────────────────────
unsigned long epochTime = 0;
int hours = 0, minutes = 0, seconds = 0;
int currentDay = 0, currentMonth = 0, currentYear = 0;
float outdoorTemp = 0;

enum DisplayMode { MODE_TIME, MODE_TEMP, MODE_UMID, MODE_DATE, MODE_WEATHER, MODE_MESAJ };
DisplayMode currentMode = MODE_TIME;

int mesajScrollPos = 0;
bool alarmTriggered[3] = {false, false, false};

// Pattern-uri segmente
const byte digits[10] = {
  0b00111111, 0b00000110, 0b01011011, 0b01001111, 0b01100110,
  0b01101101, 0b01111101, 0b00000111, 0b01111111, 0b01101111
};

const byte patterns[26] = {  // A-Z aproximative
  0b01110111,0b01111100,0b00111001,0b01011110,0b01111001,0b01110001,
  0b01101111,0b01110110,0b00000110,0b00011110,0b01110101,0b00111000,
  0b01010101,0b01101110,0b00111111,0b01110011,0b01100111,0b01110010,
  0b01101101,0b01110000,0b00111110,0b00011100,0b00101010,0b01110110,
  0b01101110,0b01011011
};

const byte letterT = 0b01110000;  // t
const byte letterU = 0b00111100;  // u
const byte blank    = 0b00000000;

// ────────────────────────────────────────────────
// SETUP
// ────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Configurare pini shift-register și digiți
  pinMode(PIN_DATA, OUTPUT);
  pinMode(PIN_CLK, OUTPUT);
  pinMode(PIN_LATCH, OUTPUT);
  pinMode(OE_PIN, OUTPUT);
  digitalWrite(OE_PIN, HIGH);

  ledcSetup(0, 5000, 8);
  ledcAttachPin(OE_PIN, 0);

  for (int i = 0; i < 6; i++) {
    pinMode(DIG_PINS[i], OUTPUT);
    digitalWrite(DIG_PINS[i], LOW);
  }

  pinMode(BUZZER_PIN, OUTPUT);
  dht.begin();

  // Citire setări din NVS
  prefs.begin("settings", false);
  loadSettings();

  // Conectare WiFi
  WiFiManager wm;
  wm.autoConnect("ESP32_CeasSmart");

  // NTP
  timeClient.begin();
  timeClient.update();
  epochTime = timeClient.getEpochTime();
  updateTimeVariables();
  updateWeather();

  // Pornire server web
  setupWebServer();
  server.begin();

  Serial.println("Ceas pornit. IP: " + WiFi.localIP().toString());
}

// ────────────────────────────────────────────────
// LOOP
// ────────────────────────────────────────────────
void loop() {
  static unsigned long lastSec = 0;
  unsigned long now = millis();

  if (now - lastSec >= 1000) {
    lastSec = now;
    epochTime++;
    updateTimeVariables();
    checkAlarms();
  }

  static unsigned long lastWeatherUpdate = 0;
  if (now - lastWeatherUpdate >= 600000UL) {
    lastWeatherUpdate = now;
    updateWeather();
  }

  determineMode(seconds);

  int d1,d2,d3,d4,d5,d6;
  getDisplayDigits(d1,d2,d3,d4,d5,d6);
  multiplexDisplay(d1,d2,d3,d4,d5,d6);

  // Refresh suplimentar pentru modurile cu blank-uri
  if (currentMode != MODE_TIME) {
    multiplexDisplay(d1,d2,d3,d4,d5,d6);
  }
}

// ────────────────────────────────────────────────
// Funcții de afișare
// ────────────────────────────────────────────────

void determineMode(int sec) {
  if      (sec >= startSecMesaj   && sec < startSecMesaj   + durMesaj)   currentMode = MODE_MESAJ;
  else if (sec >= startSecWeather && sec < startSecWeather + durWeather) currentMode = MODE_WEATHER;
  else if (sec >= startSecDate    && sec < startSecDate    + durDate)    currentMode = MODE_DATE;
  else if (sec >= startSecUmid    && sec < startSecUmid    + durUmid)    currentMode = MODE_UMID;
  else if (sec >= startSecTemp    && sec < startSecTemp    + durTemp)    currentMode = MODE_TEMP;
  else currentMode = MODE_TIME;
}

void getDisplayDigits(int &d1, int &d2, int &d3, int &d4, int &d5, int &d6) {
  switch (currentMode) {
    case MODE_TIME:
      d1 = hours   / 10; d2 = hours   % 10;
      d3 = minutes / 10; d4 = minutes % 10;
      d5 = seconds / 10; d6 = seconds % 10;
      break;

    case MODE_TEMP: {
      float t = dht.readTemperature();
      if (isnan(t)) t = 0;
      int ti = (int)t;
      int td = (int)((t - ti)*10);
      d1 = letterT; d2 = blank;
      d3 = ti / 10; d4 = ti % 10;
      d5 = td;      d6 = blank;
      break;
    }

    case MODE_UMID: {
      float u = dht.readHumidity();
      if (isnan(u)) u = 0;
      int ui = (int)u;
      d1 = letterU; d2 = blank;
      d3 = ui / 10; d4 = ui % 10;
      d5 = blank;   d6 = blank;
      break;
    }

    case MODE_DATE:
      d1 = currentDay   / 10; d2 = currentDay   % 10;
      d3 = currentMonth / 10; d4 = currentMonth % 10;
      d5 = (currentYear % 100) / 10; d6 = currentYear % 10;
      break;

    case MODE_WEATHER: {
      int ti = (int)outdoorTemp;
      d1 = patterns['O'-'A']; d2 = blank;
      d3 = ti / 10; d4 = ti % 10;
      d5 = blank;   d6 = blank;
      break;
    }

    case MODE_MESAJ: {
      int len = mesaj.length();
      int pos = mesajScrollPos % (len + 6);
      d1 = (pos < len) ? getLetterPattern(mesaj[pos]) : blank;
      d2 = (pos+1 < len) ? getLetterPattern(mesaj[pos+1]) : blank;
      d3 = (pos+2 < len) ? getLetterPattern(mesaj[pos+2]) : blank;
      d4 = (pos+3 < len) ? getLetterPattern(mesaj[pos+3]) : blank;
      d5 = (pos+4 < len) ? getLetterPattern(mesaj[pos+4]) : blank;
      d6 = (pos+5 < len) ? getLetterPattern(mesaj[pos+5]) : blank;
      mesajScrollPos++;
      break;
    }
  }
}

byte getLetterPattern(char c) {
  if (c >= 'A' && c <= 'Z') return patterns[c - 'A'];
  if (c >= 'a' && c <= 'z') return patterns[c - 'a'];
  return blank;
}

void multiplexDisplay(int d1, int d2, int d3, int d4, int d5, int d6) {
  byte vals[6] = {digits[d1], digits[d2], digits[d3], digits[d4], digits[d5], digits[d6]};

  int lvl = (hours >= 6 && hours < 22) ? brightnessDay : brightnessNight;
  int duty = lvl * 25;  // 0..250
  ledcWrite(0, 255 - duty);  // inversat pt OE active low

  for (int i = 0; i < 6; i++) {
    shiftOutSegment(vals[i]);
    digitalWrite(DIG_PINS[i], HIGH);
    delayMicroseconds(600);
    digitalWrite(DIG_PINS[i], LOW);
  }
}

void shiftOutSegment(byte val) {
  digitalWrite(PIN_LATCH, LOW);
  shiftOut(PIN_DATA, PIN_CLK, MSBFIRST, val);
  digitalWrite(PIN_LATCH, HIGH);
}

// ────────────────────────────────────────────────
// Timp, vreme, alarme
// ────────────────────────────────────────────────

void updateWeather() {
  if (WiFi.status() != WL_CONNECTED || weatherApiKey.length() == 0) {
    outdoorTemp = 0;
    return;
  }

  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + weatherCity + "&appid=" + weatherApiKey + "&units=metric";
  http.begin(url);
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      outdoorTemp = doc["main"]["temp"].as<float>();
    }
  }
  http.end();
}

void updateTimeVariables() {
  unsigned long secsDay = 86400;
  unsigned long days = epochTime / secsDay;
  unsigned long secsToday = epochTime % secsDay;

  hours   = secsToday / 3600;
  minutes = (secsToday % 3600) / 60;
  seconds = secsToday % 60;

  currentYear = 1970;
  while (days >= daysInYear(currentYear)) {
    days -= daysInYear(currentYear);
    currentYear++;
  }

  int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (isLeapYear(currentYear)) mdays[1] = 29;

  currentMonth = 1;
  while (days >= mdays[currentMonth-1]) {
    days -= mdays[currentMonth-1];
    currentMonth++;
  }
  currentDay = days + 1;
}

bool isLeapYear(int y) {
  return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

unsigned long daysInYear(int y) {
  return isLeapYear(y) ? 366 : 365;
}

void checkAlarms() {
  for (int i = 0; i < 3; i++) {
    if (alarmHours[i] >= 0 &&
        hours == alarmHours[i] &&
        minutes == alarmMins[i] &&
        !alarmTriggered[i]) {
      alarmTriggered[i] = true;
      tone(BUZZER_PIN, 1200, 8000);
    }
    if (minutes != alarmMins[i]) {
      alarmTriggered[i] = false;
    }
  }
}

// ────────────────────────────────────────────────
// Încărcare & salvare setări
// ────────────────────────────────────────────────

void loadSettings() {
  startSecTemp   = prefs.getInt("sTemp", 10);   durTemp        = prefs.getInt("dTemp", 5);
  startSecUmid   = prefs.getInt("sUmid", 20);   durUmid        = prefs.getInt("dUmid", 5);
  startSecDate   = prefs.getInt("sDate", 30);   durDate        = prefs.getInt("dDate", 5);
  startSecWeather = prefs.getInt("sWth", 40);   durWeather     = prefs.getInt("dWth", 5);
  startSecMesaj  = prefs.getInt("sMsg", 50);    durMesaj       = prefs.getInt("dMsg", 10);

  alarmHours[0] = prefs.getInt("ah0", -1); alarmMins[0] = prefs.getInt("am0", 0);
  alarmHours[1] = prefs.getInt("ah1", -1); alarmMins[1] = prefs.getInt("am1", 0);
  alarmHours[2] = prefs.getInt("ah2", -1); alarmMins[2] = prefs.getInt("am2", 0);

  brightnessDay   = prefs.getInt("bDay",   4);
  brightnessNight = prefs.getInt("bNight", 2);

  weatherCity  = prefs.getString("city",    "Bucharest");
  weatherApiKey = prefs.getString("apikey", "");
  mesaj        = prefs.getString("msg",     "Hello");
}

void saveSettings() {
  prefs.putInt("sTemp", startSecTemp);   prefs.putInt("dTemp", durTemp);
  prefs.putInt("sUmid", startSecUmid);   prefs.putInt("dUmid", durUmid);
  prefs.putInt("sDate", startSecDate);   prefs.putInt("dDate", durDate);
  prefs.putInt("sWth",  startSecWeather); prefs.putInt("dWth",  durWeather);
  prefs.putInt("sMsg",  startSecMesaj);  prefs.putInt("dMsg",  durMesaj);

  prefs.putInt("ah0", alarmHours[0]); prefs.putInt("am0", alarmMins[0]);
  prefs.putInt("ah1", alarmHours[1]); prefs.putInt("am1", alarmMins[1]);
  prefs.putInt("ah2", alarmHours[2]); prefs.putInt("am2", alarmMins[2]);

  prefs.putInt("bDay",   brightnessDay);
  prefs.putInt("bNight", brightnessNight);

  prefs.putString("city",    weatherCity);
  prefs.putString("apikey",  weatherApiKey);
  prefs.putString("msg",     mesaj);
}

// ────────────────────────────────────────────────
// Server web
// ────────────────────────────────────────────────

void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Ceas Smart Config</title>";
    html += "<style>body{font-family:Arial;margin:20px} label{display:inline-block;width:220px;margin:8px 0}</style></head><body>";
    html += "<h1>Configurare Ceas Smart</h1><form action='/save' method='POST'>";

    html += "<h3>Moduri afișare</h3>";
    html += inputNum("Sec start temperatură", "startTemp", startSecTemp);
    html += inputNum("Durată temp (s)", "durTemp", durTemp);
    html += inputNum("Sec start umiditate", "startUmid", startSecUmid);
    html += inputNum("Durată umid (s)", "durUmid", durUmid);
    html += inputNum("Sec start dată", "startDate", startSecDate);
    html += inputNum("Durată dată (s)", "durDate", durDate);
    html += inputNum("Sec start vreme", "startWeather", startSecWeather);
    html += inputNum("Durată vreme (s)", "durWeather", durWeather);
    html += inputNum("Sec start mesaj", "startMesaj", startSecMesaj);
    html += inputNum("Durată mesaj (s)", "durMesaj", durMesaj);

    html += "<h3>Mesaj personalizat</h3>";
    html += "<label>Mesaj:</label><input type='text' name='mesaj' value='" + mesaj + "' style='width:300px'><br><br>";

    html += "<h3>Vreme (OpenWeatherMap)</h3>";
    html += inputText("Oraș", "weatherCity", weatherCity);
    html += inputText("API Key", "weatherApiKey", weatherApiKey);

    html += "<h3>Alarme (max 3, oră -1 = dezactivat)</h3>";
    for (int i = 0; i < 3; i++) {
      html += "<label>Alarmă " + String(i+1) + " oră:</label><input type='number' name='ah"+String(i)+"' value='"+String(alarmHours[i])+"' min='-1' max='23'><br>";
      html += "<label>Alarmă " + String(i+1) + " minut:</label><input type='number' name='am"+String(i)+"' value='"+String(alarmMins[i])+"' min='0' max='59'><br><br>";
    }

    html += "<h3>Luminozitate (1–10)</h3>";
    html += inputRange("Zi", "brightDay", brightnessDay, 1, 10);
    html += inputRange("Noapte", "brightNight", brightnessNight, 1, 10);

    html += "<br><input type='submit' value='Salvează'>&nbsp;&nbsp;";
    html += "</form>";

    html += "<br><form action='/buzzer' method='POST'><input type='submit' value='Test Buzzer 3 sec'></form>";
    html += "</body></html>";

    request->send(200, "text/html", html);
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
    // Moduri
    if (request->hasParam("startTemp", true))   startSecTemp   = request->getParam("startTemp", true)->value().toInt();
    if (request->hasParam("durTemp", true))     durTemp        = request->getParam("durTemp", true)->value().toInt();
    if (request->hasParam("startUmid", true))   startSecUmid   = request->getParam("startUmid", true)->value().toInt();
    if (request->hasParam("durUmid", true))     durUmid        = request->getParam("durUmid", true)->value().toInt();
    if (request->hasParam("startDate", true))   startSecDate   = request->getParam("startDate", true)->value().toInt();
    if (request->hasParam("durDate", true))     durDate        = request->getParam("durDate", true)->value().toInt();
    if (request->hasParam("startWeather", true)) startSecWeather = request->getParam("startWeather", true)->value().toInt();
    if (request->hasParam("durWeather", true))  durWeather     = request->getParam("durWeather", true)->value().toInt();
    if (request->hasParam("startMesaj", true))  startSecMesaj  = request->getParam("startMesaj", true)->value().toInt();
    if (request->hasParam("durMesaj", true))    durMesaj       = request->getParam("durMesaj", true)->value().toInt();

    // Mesaj
    if (request->hasParam("mesaj", true))       mesaj = request->getParam("mesaj", true)->value();

    // Vreme
    if (request->hasParam("weatherCity", true)) weatherCity = request->getParam("weatherCity", true)->value();
    if (request->hasParam("weatherApiKey", true)) weatherApiKey = request->getParam("weatherApiKey", true)->value();

    // Alarme
    for (int i = 0; i < 3; i++) {
      String hn = "ah" + String(i);
      String mn = "am" + String(i);
      if (request->hasParam(hn.c_str(), true)) alarmHours[i] = request->getParam(hn.c_str(), true)->value().toInt();
      if (request->hasParam(mn.c_str(), true)) alarmMins[i]  = request->getParam(mn.c_str(), true)->value().toInt();
    }

    // Luminozitate
    if (request->hasParam("brightDay", true))   brightnessDay   = request->getParam("brightDay", true)->value().toInt();
    if (request->hasParam("brightNight", true)) brightnessNight = request->getParam("brightNight", true)->value().toInt();

    saveSettings();
    request->redirect("/");
  });

  server.on("/buzzer", HTTP_POST, [](AsyncWebServerRequest *request){
    tone(BUZZER_PIN, 1400, 3000);
    request->redirect("/");
  });
}

// Helper pentru HTML input number
String inputNum(String label, String name, int val) {
  return "<label>" + label + ":</label><input type='number' name='" + name + "' value='" + String(val) + "'><br>";
}

// Helper pentru HTML input text
String inputText(String label, String name, String val) {
  return "<label>" + label + ":</label><input type='text' name='" + name + "' value='" + val + "' style='width:300px'><br>";
}

// Helper pentru HTML range
String inputRange(String label, String name, int val, int minv, int maxv) {
  return "<label>" + label + ":</label><input type='range' name='" + name + "' min='" + String(minv) + "' max='" + String(maxv) + "' value='" + String(val) + "'><br>";
}
/*
  ============================================================
  ESP32 SMARTDISPLAY + MOTONAV
  Single-file all-in-one sketch
  Hardware:
  - ESP32 DevKit V1
  - OLED SSD1306 128x64 I2C (SDA=21, SCL=22, addr=0x3C)
  - Buzzer GPIO25
  - 1 tombol fisik: GPIO4 ke GND (INPUT_PULLUP), banyak fungsi lewat klik / tahan / 3x klik
  
  Masalah: 

  ============================================================
*/
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BluetoothSerial.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <LittleFS.h>            // animasi mata (eyegifs.bin) disimpan di sini, bukan di flash program

// Tipe data animasi mata: harus di sini (sebelum prototipe fungsi otomatis Arduino)
struct EyeEnt { char name[16]; uint16_t frames; uint32_t off; uint32_t size; };

// ── Aset online di GitHub (repo EspFirm, folder firmware) ──
// eyegifs.bin   : animasi mata, diunduh ESP32 sekali saat setup awal -> LittleFS
// dashboard.html: tampilan web, diambil browser langsung dari GitHub (tidak ada di ESP32)
#define ASSET_BASE   "https://raw.githubusercontent.com/Adisdzaky1/EspFirm/main/firmware/"
#define EYE_BIN_URL  ASSET_BASE "eyegifs.bin"
#define DASH_URL     ASSET_BASE "dashboard.html"
#define DASH_URL2    "https://cdn.jsdelivr.net/gh/Adisdzaky1/EspFirm@main/firmware/dashboard.html"   // cadangan
#define EYE_BIN_PATH "/eyegifs.bin"
#define EYE_TMP_PATH "/eyegifs.tmp"
#define EYE_FRAME_MAX 1100         // ukuran terbesar 1 frame terkompresi (layar 128x64 maks 1024 mentah + header)

// Halaman pengantar kecil yang disajikan ESP32 di "/": memuat dashboard dari GitHub (disimpan di browser
// supaya tetap bisa dibuka tanpa internet) atau, bila belum pernah berhasil, menampilkan form WiFi darurat.
static const char DASH_STUB[] PROGMEM =
  "<!doctype html><html lang=\"id\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>SmartDisplay</title>\n"
  "<style>body{margin:0;font:15px system-ui,sans-serif;background:#f5f6fb;color:#1b1f2a;display:grid;place-items:center;min-height:100vh}@media(prefers-color-scheme:dark){body{background:#12141a;color:#e8eaf0}input{background:#1d2028;color:#e8eaf0}}.c{width:min(92vw,420px);padding:20px;box-sizing:border-box}h2{margin:0 0 6px}p{margin:6px 0;opacity:.8}input,button{width:100%;box-sizing:border-box;padding:12px;margin:6px 0;border-radius:12px;border:1px solid #8885;font:inherit}button{background:#4f6bff;color:#fff;border:0;font-weight:600}.sp{width:26px;height:26px;border:3px solid #8885;border-top-color:#4f6bff;border-radius:50%;animation:r 1s linear infinite;margin:12px 0}@keyframes r{to{transform:rotate(360deg)}}</style></head><body>\n"
  "<div class=\"c\"><h2>SmartDisplay</h2><div id=\"ld\"><div class=\"sp\"></div><p>Memuat dashboard...</p></div>\n"
  "<div id=\"fb\" hidden><p>Dashboard belum bisa dimuat (butuh internet di HP ini, sekali saja). Sementara, atur WiFi perangkat di sini:</p>\n"
  "<input id=\"s\" placeholder=\"Nama WiFi (SSID)\" autocapitalize=\"none\"><input id=\"w\" type=\"password\" placeholder=\"Sandi WiFi\"><button id=\"b\">Sambungkan WiFi</button><p id=\"m\"></p>\n"
  "<button id=\"rl\" style=\"background:#8886\">Coba muat dashboard lagi</button></div></div>\n"
  "<script>\n"
  "var U=['%U1%','%U2%'],K='dash_v1';\n"
  "function $(i){return document.getElementById(i)}\n"
  "function show(h){document.open();document.write(h);document.close()}\n"
  "function good(h){return h&&h.length>5000&&h.indexOf('</html>')>0}\n"
  "function net(i){if(i>=U.length)return Promise.reject();var c=new AbortController(),t=setTimeout(function(){c.abort()},7000);\n"
  "return fetch(U[i]+'?t='+Math.floor(Date.now()/60000),{signal:c.signal}).then(function(r){clearTimeout(t);if(!r.ok)throw 0;return r.text()}).then(function(h){if(!good(h))throw 0;return h}).catch(function(){clearTimeout(t);return net(i+1)})}\n"
  "function fb(){$('ld').hidden=true;$('fb').hidden=false}\n"
  "net(0).then(function(h){try{localStorage.setItem(K,h)}catch(e){}show(h)},function(){var h=null;try{h=localStorage.getItem(K)}catch(e){}if(good(h))show(h);else fb()});\n"
  "$('rl').onclick=function(){location.reload()};\n"
  "$('b').onclick=function(){var s=$('s').value.trim();if(!s){$('m').textContent='Isi nama WiFi dulu';return}\n"
  "$('m').textContent='Mengirim...';fetch('/setwifi?ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent($('w').value)).then(function(r){return r.text()}).then(function(t){\n"
  "if(t.indexOf('TRYING')!=0){$('m').textContent=t;return}$('m').textContent='Mencoba tersambung... lihat layar OLED. Setelah tersambung, sambungkan HP ke WiFi yang sama lalu buka alamat IP yang tampil di OLED.'},function(){$('m').textContent='Perangkat tidak merespons'})};\n"
  "</script></body></html>\n";



// Perbesar stack loop() (default 8 KB). setup()/loop() memanggil HTTPClient + JSON + WiFi,
// stack default rawan overflow -> "Stack canary watchpoint" lalu reboot terus.
SET_LOOP_TASK_STACK_SIZE(24 * 1024);   // 24 KB: pengambilan data HTTPS (TLS) di loop() butuh stack lebih besar

// ═══════════════════════════════════════════════════════════
// HARDWARE & KONFIGURASI DASAR
// ═══════════════════════════════════════════════════════════
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_ADDR     0x3C

#define SDA_PIN         21
#define SCL_PIN         22
#define BUZZER_PIN      19
#define PIN_BTN          4   // Satu-satunya tombol: klik, tahan lama, 3x klik cepat (lihat bagian TOMBOL FISIK)

#define WIFI_MODE_ID     0
#define NAV_MODE_ID      1

#define BT_DEVICE_NAME  "ESP32 Navigation"

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;     // WIB
const int daylightOffset_sec = 0;

// WeatherAPI.com config
const char* weatherApiKey   = "9cefcc6eb6564c29aa611710261805";
const char* weatherLocation  = "Arcawinangun";
const char* cityDisplay      = "Banyumas";

// ═══════════════════════════════════════════════════════════
// OBJEK GLOBAL
// ═══════════════════════════════════════════════════════════
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
BluetoothSerial SerialBT;
WebServer server(80);
Preferences prefs;

// ═══════════════════════════════════════════════════════════
// PROTOTYPES
// ═══════════════════════════════════════════════════════════
void bootScreen(const String &line1, const String &line2);
void loadPrefs();
void savePrefs();

void ensureWiFiMode();
void connectWiFiFromPrefs(bool showBootMessages);
void wifiStabilize();
void startWebServer();
void stopWebServer();

void syncNTP();
void updateTimeFromNTP();
bool fetchWeatherFromAPI();
void applyOLEDContrast(uint8_t level);
void setDisplayOn(bool on);

void saveWeatherToPrefs();
void loadWeatherFromPrefs();

void drawSmartDisplay();
void drawClockScreen();
void drawWeatherScreen();
void drawRobotEyesScreen();
void drawInfoScreen();

void updateAutoMode();
void updateSleepState();
bool isInSleepWindow(int startHour, int startMin, int endHour, int endMin, int currentHour, int currentMin);
void updateAlarmState();
void startAlarmBeepPattern(uint8_t count, uint16_t onMs, uint16_t offMs);
void updateBuzzerTask();
void buzzerOn(uint16_t freq);
void buzzerOff();
void beepTest();

void setupWebServer();
String jsonEscape(const String &in);
String formatDateString();
String formatTimeString();
String wifiSignalBars(int rssi);

void handleWebStatus();
void handleWebMode();
void handleWebAuto();
void handleWebWeather();
void handleWebBrightness();
void handleWebAutoBright();
void handleWebAlarm();
void handleWebSleep();
void handleWebBeep();
void handleWebSwitchNav();
void handleWebDeepSleep();
void handleWebEyes();
unsigned long eyeRandomIdleMs();

void switchToNavMode();
void switchToWiFiMode();

void enterNavMode();
void enterWiFiMode();
void updateNavMode();
void clearNavState();
void parseBTLine(const String &line);
void processBTInput();
void drawNavWaitingScreen();
void drawNavConnectedFlash();
void drawNavArrowScreen();
void drawNavIdleScreen();
void drawNavWaitScreen();
void drawNavArrivedScreen();
void drawNavGPSWeakScreen();
void drawNavIconArrowUp(int cx, int cy, int sz);
void drawNavIconArrowRight(int cx, int cy, int sz);
void drawNavIconArrowLeft(int cx, int cy, int sz);
void drawNavIconUTurn(int cx, int cy, int sz);
void drawNavIconRoundabout(const String &type, int cx, int cy, int sz);
void drawNavIconArrived(int cx, int cy, int sz);
void drawNavIconWarningOutline(const String &type, int cx, int cy, int sz);
void drawCenteredText(const String &txt, int y, uint8_t size);
void drawScrollingText(const String &txt, int x, int y, uint8_t size, int maxWidth);

String getCurrentModeText();
String getCurrentSleepText();
String getCurrentAlarmText();
void saveModeToPrefs(uint8_t mode);
uint8_t readModeFromPrefs();

String readPrefsString(const char* key, const String &fallback);
float readPrefsFloat(const char* key, float fallback);
int readPrefsInt(const char* key, int fallback);
bool readPrefsBool(const char* key, bool fallback);

void saveWifiCredentials(const String &ssid, const String &pass);
void startWifiValidation(const String &newSSID, const String &newPASS, bool fromBT);
void enterWiFiModeForValidation(const String &newSSID, const String &newPASS);
bool ensureWifiCredentialsFromSerial();
bool parseCredentialLine(const String &line, String &ssidOut, String &passOut);

String buildWeatherURL();
String getNavAction(const String &packetType);

void updateOLEDForMode();
void updateSystemDisplay();
void handleSleepBrightness();
void applyBrightnessForTime();
void saveSystemSettings();
void loadSystemSettings();

void processWebEndpointsCommentary();

bool parseHHMM(const String &s, int &hOut, int &mOut);

// ── Tombol fisik, Mini Games & Deep Sleep ───────────────────
void pollButtons();
void openGames();
void gameLoop();
void gameCentered(const char* s, int y, int size);
void gameDrawOverlay(uint32_t sc, uint32_t hi);
void drawGameMenu();
void dinoReset();
void drawDino(int x, int y, bool onGround, uint32_t frame);
void drawCactus(int x, int y, int w, int h);
bool rectHit(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh);
bool dinoUpdate(bool jump);
void openTools();
void toolLoop();
void timerTick();
void dinoDraw();
void flapReset();
void drawBird(int y, bool wingUp);
bool flapUpdate(bool flap);
void flapDraw();
bool requestDeepSleep();
void updateDeepSleepPending();
void enterDeepSleepMode();
void exitDeepSleepMode();

// ── WiFi banyak, AP darurat, data online, sholat, emas ─────
void wifiListSave();
void wifiListLoad();
void wifiListAdd(const String &ssid, const String &pass);
void wifiListRemove(int idx);
void startEmergencyAP(bool show);
void stopEmergencyAP();
bool connectBestSavedWifi(bool show);
void loadOnlineCache();
bool fetchForecast();
bool fetchPrayerTimes();
bool fetchGoldPrice();
void updateOnlineData();
void updatePrayerReminder();
void updatePrayerAlert();
void dismissPrayerAlert();
void drawPrayerAlert();
void drawForecastScreen();
void drawPrayerScreen();
void drawGoldScreen();
int forecastStartIndex();
int fcUvToday10();
const char* wxCodeText(uint8_t c);
int nextPrayerSlot(long *secsLeft);
uint32_t goldPrice();
double goldRatio(double v);
String fmtRupiah(uint32_t v);
void handleWebFeat();
void handleWebAutoMask();
void handleWebPrayer();
void handleWebPrayerRefresh();
void handleWebForecastRefresh();
void handleWebGold();
void handleWebWifis();
void handleWebWifiDel();
void handleWebWifiUse();

// ═══════════════════════════════════════════════════════════
// STATE GLOBAL
// ═══════════════════════════════════════════════════════════
enum SystemMode : uint8_t { WIFI_MODE = 0, NAV_MODE = 1 };
SystemMode systemMode = WIFI_MODE;

bool wifiStarted = false;
bool webStarted = false;
bool webRoutesConfigured = false;
bool btEnabled = false;

String wifiSSID;
String wifiPASS;

// Clock/time
int curHour = 0, curMin = 0, curSec = 0;
int curDay = 1, curMon = 1, curYear = 2026, curWday = 0;
const char* dayNames[] = {"Minggu","Senin","Selasa","Rabu","Kamis","Jumat","Sabtu"};
const char* monNames[] = {"","Jan","Feb","Mar","Apr","Mei","Jun","Jul","Agu","Sep","Okt","Nov","Des"};

// Weather cache
float wTemp = 0.0f;
float wHumid = 0.0f;
float wFeels = 0.0f;
float wWind = 0.0f;
String wDesc = "Belum ada data";
String wLastUpdate = "--:--";
bool weatherValid = false;
unsigned long lastWeatherFetchMs = 0;

// Display mode
uint8_t displayMode = 0;      // 0..6 (lihat PAGE_COUNT)
bool autoMode = true;
unsigned long lastModeRotateMs = 0;
const unsigned long modeRotateIntervalMs = 8000;

// Sleep / brightness
bool autoBrightness = true;
int manualBrightness = 220;
int dayBrightness = 220;
int nightBrightness = 10;
bool oledSleepState = false;
int sleepStartHour = 22;
int sleepStartMin = 0;
int sleepEndHour = 6;
int sleepEndMin = 0;
bool sleepEnabled = false;

// Alarm
bool alarmEnabled = false;
int alarmHour = 7;
int alarmMin = 0;
int alarmLastTriggeredYMD = -1;



// Buzzer pattern
bool buzzerPatternActive = false;
uint8_t buzzerPatternCount = 0;
uint8_t buzzerPatternDone = 0;
uint16_t buzzerPatternOnMs = 120;
uint16_t buzzerPatternOffMs = 80;
bool buzzerPatternOnState = false;
unsigned long buzzerPatternTs = 0;
const uint16_t buzzerFreq = 2200;

// Buzzer siren (alarm bangun pagi) - warble 2 nada selama durasi tertentu
bool buzzerSirenActive = false;
unsigned long buzzerSirenStartTs = 0;
unsigned long buzzerSirenDurationMs = 60000; // 1 menit
unsigned long buzzerSirenToneTs = 0;
bool buzzerSirenToneHigh = true;
const uint16_t buzzerSirenFreqHigh = 2200;
const uint16_t buzzerSirenFreqLow  = 1500;
const uint16_t buzzerSirenToneMs   = 350; // durasi tiap nada sebelum berganti

// WiFi reconnect support
unsigned long lastWiFiRetryMs = 0;
const unsigned long wifiRetryIntervalMs = 15000;

// WiFi credential validation (try before save)
bool   wifiValidating   = false;
bool   wifiValidateBT   = false;   // true = berasal dari BT command
String wifiTrySSID      = "";
String wifiTryPASS      = "";
String wifiOldSSID      = "";
String wifiOldPASS      = "";
unsigned long wifiTryMs = 0;
const unsigned long WIFI_VALIDATE_MS = 20000; // timeout 20 detik

// ── Halaman SmartDisplay ────────────────────────────────────
// 0 Jam, 1 Cuaca, 2 Mata, 3 Info, 4 Prakiraan, 5 Sholat, 6 Emas
const uint8_t PAGE_COUNT = 7;
uint8_t autoPageMask = 0x7F;                 // bit n = halaman n ikut berganti otomatis (semua bit 0 = tidak berganti)

// ── Banyak WiFi tersimpan + AP darurat ──────────────────────
const int WIFI_MAX = 5;
String wifiListSsid[WIFI_MAX];
String wifiListPass[WIFI_MAX];
int wifiListN = 0;
bool apActive = false;                       // true = hotspot darurat sedang menyala
const char* AP_SSID = "SmartDisplay-Setup";
const char* AP_PASS = "smartdisplay";        // minimal 8 karakter (ganti sesuai selera)
const unsigned long AP_FALLBACK_MS = 60000;  // WiFi putus selama ini -> AP darurat menyala
String wifiLastResult = "";                  // "ok:<ssid>" / "fail:<ssid>" (hasil validasi WiFi terakhir)
unsigned long wifiDownSince = 0;
uint8_t wifiRetryCount = 0;
bool staInitDone = false;                    // sync NTP / cuaca / web sudah dijalankan untuk koneksi ini

// ── Prakiraan cuaca 5 hari + UV + kualitas udara (Open-Meteo, tanpa API key) ──
struct FcDay { int8_t tmax, tmin; uint8_t pop, uv10, code, wday, mon, dom; };
const int FC_DAYS = 5;
FcDay fcDay[FC_DAYS];
bool fcValid = false;
int16_t aqiUS = -1;                          // US AQI saat ini (-1 = belum ada)
float pm25Now = -1;
String fcLast = "--:--";
float geoLat = -7.42f, geoLon = 109.25f;     // koordinat lokasi (diambil otomatis dari WeatherAPI)

// ── Jadwal & pengingat sholat (Aladhan, metode Kemenag RI) ──
// urutan: 0 Subuh, 1 Terbit, 2 Dzuhur, 3 Ashar, 4 Maghrib, 5 Isya
const char* const prayerNames[6] = { "Subuh", "Terbit", "Dzuhur", "Ashar", "Maghrib", "Isya" };
const uint8_t PR_IDX[5] = { 0, 2, 3, 4, 5 }; // 5 waktu sholat -> indeks di prayerMin[]
int16_t prayerMin[6] = { 0, 0, 0, 0, 0, 0 }; // menit sejak 00:00
bool prayerValid = false;
int prayerYmd = 0;                           // tanggal jadwal yang tersimpan (YYYYMMDD)
uint32_t prayerEpoch = 0;
bool prayerReminderOn = true;
uint8_t prayerMask = 0x1F;                   // bit 0..4 = Subuh, Dzuhur, Ashar, Maghrib, Isya
int prayerLastYmd[5] = { 0, 0, 0, 0, 0 };
volatile bool prayerAlertActive = false;
uint32_t prayerAlertStart = 0;
uint8_t prayerAlertIdx = 0;
uint16_t prayerLastFreq = 0;

// ── Harga emas per 1 gram (spot dunia x kurs USD/IDR) ───────
double goldXauUsd = 0;                       // USD per troy ounce
double goldUsdIdr = 0;
double goldRawGram = 0;                      // Rupiah per gram sebelum penyesuaian
double goldOpenGram = 0;                     // harga pembuka hari ini (untuk persen perubahan)
int goldOpenYmd = 0;
String goldLast = "--:--";
float goldAdjPct = 0;                        // penyesuaian % (agar mendekati harga toko)
bool goldValid = false;
const int GOLD_HIST_N = 48;
double goldHist[GOLD_HIST_N];                // riwayat (mentah), 1 titik per ~10 menit
int goldHistCount = 0;
unsigned long goldHistLastMs = 0;



// ── Jadwal pengambilan data online ──────────────────────────
unsigned long nextWeatherAt = 0, nextFcAt = 0, nextPrayerAt = 0, nextGoldAt = 0, nextFxAt = 0;

// Bluetooth nav state
String btBuffer = "";
String navType = "";
String navDistance = "";
String navRoad = "";
String idleSpeed = "--";
String waitMessage = "";
bool navGPSWeak = false;
bool navArrived = false;
bool btClientPrev = false;
unsigned long lastBTDataMs = 0;
unsigned long lastScrollMs = 0;
unsigned long lastBlinkMs = 0;
unsigned long lastNavFrameMs = 0;
bool blinkState = false;
int scrollIndex = 0;
const unsigned long navTimeoutMs = 30000;
const unsigned long scrollIntervalMs = 350;
const unsigned long blinkIntervalMs = 500;
const unsigned long navFrameIntervalMs = 28; // ~35fps

// Animasi mata (GIF) - lihat drawRobotEyesScreen()
uint8_t eyeBuf[SCREEN_WIDTH * SCREEN_HEIGHT / 8];   // frame yang sedang tampil (hasil dekode)
uint8_t eyeRle[EYE_FRAME_MAX];                      // 1 frame terkompresi dibaca dari file
EyeEnt *eyeTab = nullptr;                           // tabel animasi (dimuat dari eyegifs.bin)
File eyeFile;
bool eyeFsOk = false, eyeReady = false, eyeStarted = false;
int eyeCount = 0, eyeBlank = 0;
int eyeCurIdx = 0;
int eyeLastExpr = -1;
uint16_t eyeFrames = 0;
uint16_t eyeFrameIdx = 0;
// Setup awal (layar beranimasi): lihat setupWizardTick()
bool setupActive = false;
uint8_t setupStep = 0;
unsigned long setupDrawMs = 0, setupStepMs = 0;
uint32_t eyeDlGot = 0, eyeDlTotal = 0;
uint8_t eyeDlTry = 0;
bool eyeDlFailed = false, dashOk = false;
String eyeDlErr = "";
unsigned long eyeNextMs = 0;
unsigned long eyeIdleUntilMs = 0;
int eyeMinSec = 4;      // jeda acak antar animasi mata (detik) - diatur lewat web
int eyeMaxSec = 20;


// ═══════════════════════════════════════════════════════════
// TOMBOL FISIK (1 tombol, GPIO 4) + MINI GAMES + DEEP SLEEP
// ═══════════════════════════════════════════════════════════

// Satu tombol (GPIO 4) dengan debounce. Event yang dihasilkan (semuanya independen):
//  - hit        : saat tombol DITEKAN (langsung, tanpa menunggu lepas) -> lompat / flap di game
//  - click      : klik pendek, dikenali saat DILEPAS (ditahan < LONG_PRESS_MS)
//  - longHit    : ditahan >= LONG_PRESS_MS (terpicu saat masih ditahan; lepasnya tidak dihitung klik)
//  - triple     : 3x klik pendek cepat (jeda antar klik <= MULTI_CLICK_GAP_MS)
//  - single     : 1x klik pendek yang BUKAN bagian dari klik ganda (muncul setelah jeda MULTI_CLICK_GAP_MS)
//  - dbl        : tepat 2x klik pendek cepat (muncul setelah jeda MULTI_CLICK_GAP_MS tanpa klik ketiga)
const uint16_t MULTI_CLICK_GAP_MS = 600;   // jeda maks antar klik pendek agar dihitung 1 rangkaian
const uint16_t LONG_PRESS_MS      = 700;   // lama tahan agar dianggap "tahan lama"

struct Btn {
  uint8_t pin;
  bool stable, lastRaw, longFired;
  bool hit, click, longHit, triple, single, dbl;
  uint8_t clickCount;
  uint32_t tChange, pressStart, lastClickMs;
  Btn(uint8_t p) : pin(p), stable(false), lastRaw(false), longFired(false),
                   hit(false), click(false), longHit(false), triple(false), single(false), dbl(false),
                   clickCount(0), tChange(0), pressStart(0), lastClickMs(0) {}
  void begin() { pinMode(pin, INPUT_PULLUP); }
  void update() {
    uint32_t now = millis();
    bool raw = (digitalRead(pin) == LOW);
    if (raw != lastRaw) { lastRaw = raw; tChange = now; }
    if (now - tChange > 15 && raw != stable) {
      stable = raw;
      if (stable) {                                   // ditekan
        hit = true;
        pressStart = now;
        longFired = false;
      } else if (!longFired) {                        // dilepas sebelum batas tahan = klik pendek
        click = true;
        if (now - lastClickMs > MULTI_CLICK_GAP_MS) clickCount = 0;
        lastClickMs = now;
        if (++clickCount >= 3) { triple = true; clickCount = 0; }
      }
    }
    // Rangkaian klik selesai (tidak ada klik lanjutan) -> tentukan klik tunggal / ganda
    if (!stable && clickCount > 0 && now - lastClickMs > MULTI_CLICK_GAP_MS) {
      if (clickCount == 1) single = true;
      else if (clickCount == 2) dbl = true;
      clickCount = 0;
    }
    if (stable && !longFired && now - pressStart >= LONG_PRESS_MS) {   // masih ditahan
      longFired = true;
      longHit = true;
      clickCount = 0;
    }
  }
  bool take()       { bool v = hit;     hit = false;     return v; }
  bool takeClick()  { bool v = click;   click = false;   return v; }
  bool takeLong()   { bool v = longHit; longHit = false; return v; }
  bool takeTriple() { bool v = triple;  triple = false;  return v; }
  bool takeSingle() { bool v = single;  single = false;  return v; }
  bool takeDouble() { bool v = dbl;     dbl = false;     return v; }
};
Btn btn(PIN_BTN);

// Tombol dibaca tiap 5 ms oleh task sendiri, bukan oleh loop() yang kadang lambat (gambar OLED, dll),
// supaya klik singkat tidak terlewat.
void btnTask(void *pv) {
  for (;;) {
    btn.update();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ── Mini Games: state ───────────────────────────────────────
Preferences gamePrefs;                       // namespace terpisah dari "motonav"
enum GameState { GS_MENU, GS_DINO, GS_DINO_OVER, GS_FLAP_WAIT, GS_FLAP, GS_FLAP_OVER };
GameState gameState = GS_MENU;
bool gameActive = false;                     // true = OLED dipakai game
int gameMenuSel = 0;                         // 0=Dino 1=Flappy 2=Keluar
uint32_t hiDino = 0, hiFlap = 0;
uint32_t gameLastFrame = 0;
// ── Timer & Stopwatch: state ────────────────────────────────
enum ToolState { TL_MENU, TL_STOPWATCH, TL_TIMER };
enum TimerPhase { TM_SET, TM_RUN, TM_PAUSE, TM_DONE };
bool toolActive = false;                     // true = OLED dipakai Timer & Stopwatch
ToolState toolState = TL_MENU;
int toolMenuSel = 0;                         // 0=Stopwatch 1=Timer 2=Keluar
uint32_t toolLastFrame = 0;
bool swRunning = false;                      // stopwatch tetap jalan walau keluar ke menu / SmartDisplay
uint32_t swStartMs = 0, swAccumMs = 0;
const uint16_t TIMER_PRESET_SEC[] = { 60, 180, 300, 600, 900, 1500, 1800, 2700, 3600 };   // 1,3,5,10,15,25,30,45,60 menit
const int TIMER_PRESET_N = sizeof(TIMER_PRESET_SEC) / sizeof(TIMER_PRESET_SEC[0]);
int tmPreset = 2;                            // default 5 menit
TimerPhase tmPhase = TM_SET;
uint32_t tmEndMs = 0, tmRemainMs = 0;        // timer tetap jalan di latar belakang
bool tmBeeping = false;
uint32_t tmBeepStart = 0;
uint32_t gameOverMs = 0;                     // kapan game terakhir berakhir (cegah klik sisa langsung restart)
const uint16_t GAME_FRAME_MS = 30;           // ~33 FPS

// Chrome Dino
const float DINO_START_SPEED = 2.2f;        // kecepatan awal (pelan)
const float DINO_MAX_SPEED   = 8.0f;         // kecepatan maksimum
const int GROUND_Y = 58;
const int DINO_X = 12, DINO_W = 12, DINO_H = 14;
float dinoY, dinoVy, dSpeed, dScore, dDistSince, dNextGap, dGroundScroll;
bool dinoOnGround;
uint32_t dFrame;
struct Cactus { float x; uint8_t w, h; bool active; };
Cactus cacti[3];

// Flappy Bird
const int BIRD_X = 26;
const int GAP_H = 32, PIPE_W = 14, PIPE_SPACING = 72, FLOOR_Y = 62;
float birdY, birdVy;
struct Pipe { float x; int gapY; bool scored; };
Pipe pipes[3];
uint32_t fScore, fFrame;

// ── Deep Sleep (hanya berlaku di WIFI_MODE) ─────────────────
// Matikan WiFi & web server. OLED + jam tetap jalan,
// data terakhir (cuaca dll) tetap dipakai. Keluar: 3x klik cepat tombol GPIO 4.
volatile bool deepSleepMode = false;
bool deepSleepPending = false;               // menunggu balasan web terkirim
unsigned long deepSleepReqMs = 0;
uint32_t deepSleepPrevCpuMhz = 240;

// Pengambilan data online (cuaca, prakiraan, sholat, emas) dan pindah WiFi cadangan
// dikerjakan di TASK TERPISAH (core 0) supaya loop() / OLED / web TIDAK PERNAH tertahan oleh jaringan.
SemaphoreHandle_t strMx = nullptr;            // kunci singkat untuk String yang ditulis task & dibaca loop()
struct StrLock {
  StrLock() { if (strMx) xSemaphoreTake(strMx, portMAX_DELAY); }
  ~StrLock() { if (strMx) xSemaphoreGive(strMx); }
};
volatile bool reqNtpSync = false;             // minta task menyinkronkan jam
volatile bool reqWifiFailover = false;        // minta task mencoba WiFi tersimpan lainnya
volatile bool wifiBusy = false;               // task sedang berganti WiFi: loop() jangan ikut mengutak-atik WiFi
TaskHandle_t onlineTaskHandle = nullptr;

// ═══════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════
// Penyebab restart terakhir (untuk melacak crash: PANIC / TASK_WDT / BROWNOUT, dll.)
const char *resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "POWERON (dicolok / tombol reset)";
    case ESP_RST_SW:        return "SW (restart oleh program)";
    case ESP_RST_PANIC:     return "PANIC (crash program)";
    case ESP_RST_INT_WDT:   return "INT_WDT (watchdog interrupt)";
    case ESP_RST_TASK_WDT:  return "TASK_WDT (task macet)";
    case ESP_RST_WDT:       return "WDT (watchdog)";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (tegangan turun - cek kabel/charger)";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    default:                return "LAINNYA";
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("[BOOT] ESP32 SmartDisplay + MotoNav start");
  strMx = xSemaphoreCreateMutex();
  Serial.printf("[BOOT] Alasan reset: %s\n", resetReasonText());
  randomSeed(esp_random());

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Sambungkan BUZZER_PIN ke peripheral LEDC. Core ESP32 3.x (IDF5) sudah
  // menghapus ledcSetup/ledcAttachPin (versi channel-based); ganti ledcAttach
  // yang langsung memakai nomor pin.
  ledcAttach(BUZZER_PIN, 2000, 8); // pin, base freq 2000Hz, resolusi 8-bit

  btn.begin();
  xTaskCreatePinnedToCore(btnTask, "Btn", 4096, NULL, 2, NULL, 1);   // prioritas di atas loop() supaya klik tidak terlewat


  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[BOOT] OLED tidak ditemukan");
    while (true) { delay(500); }
  }

  prefs.begin("motonav", false);
  gamePrefs.begin("games", false);
  hiDino = gamePrefs.getUInt("dino", 0);
  hiFlap = gamePrefs.getUInt("flap", 0);
  loadPrefs();
  loadWeatherFromPrefs();
  eyeFsOk = LittleFS.begin(true);          // true = format otomatis bila belum pernah dipakai
  if (eyeFsOk) eyeLoadTable();

  bootScreen("Booting ESP32...", "Mohon tunggu");
  {
    esp_reset_reason_t rr = esp_reset_reason();        // tampilkan penyebab restart di OLED bila bukan nyala/reset biasa
    if (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT || rr == ESP_RST_BROWNOUT) {
      bootScreen("Restart tak normal", rr == ESP_RST_PANIC ? "PANIC (crash)" : rr == ESP_RST_BROWNOUT ? "BROWNOUT (daya)" : "WATCHDOG (macet)");
      delay(4000);
    }
  }
  delay(700);

  systemMode = (SystemMode)readModeFromPrefs();
  if (systemMode != WIFI_MODE && systemMode != NAV_MODE) systemMode = WIFI_MODE;

  if (wifiListN == 0) {
    Serial.println("[BOOT] Belum ada WiFi tersimpan -> AP darurat akan menyala (atau kirim lewat Serial: WIFI:nama:sandi)");
  } else {
    wifiSSID = wifiListSsid[0];
    wifiPASS = wifiListPass[0];
  }

  if (systemMode == NAV_MODE) {
    Serial.println("[BOOT] Last mode = NAV_MODE");
    enterNavMode();
  } else {
    Serial.println("[BOOT] Last mode = WIFI_MODE");
    enterWiFiMode();
  }

  if (!setupActive) startOnlineTask();   // saat wizard aktif, task dibuat setelah selesai (hemat 16 KB heap)
  Serial.println("[BOOT] Setup selesai");
}

// ═══════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════
// Runtime serial handler - bisa ganti WiFi kapan saja
void handleRuntimeSerial() {
  static String rxBuf = "";
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      rxBuf.trim();
      if (rxBuf.length() == 0) { rxBuf = ""; return; }
      String ssid, pass;
      if (parseCredentialLine(rxBuf, ssid, pass)) {
        if (deepSleepMode) {
          Serial.println("[SERIAL] Deep sleep aktif (WiFi mati). Keluar dulu: klik tombol GPIO 4 tiga kali cepat.");
        } else {
          Serial.println("[SERIAL] Validasi WiFi baru: " + ssid);
          bootScreen("Cek WiFi baru...", ssid);
          // Pakai sistem validasi – belum simpan ke NVS dulu
          startWifiValidation(ssid, pass, false);
        }
      } else {
        Serial.println("[SERIAL] Format: WIFI:namaWifi:password");
      }
      rxBuf = "";
    } else {
      rxBuf += c;
      if (rxBuf.length() > 120) rxBuf = "";
    }
  }
}

void loop() {
  static unsigned long sysLastMs = 0, sysIterStart = 0, sysMaxLoop = 0;
  unsigned long sysNow = millis();
  if (sysIterStart && sysNow - sysIterStart > sysMaxLoop) sysMaxLoop = sysNow - sysIterStart;
  sysIterStart = sysNow;
  if (sysNow - sysLastMs >= 10000UL) {                    // log diagnostik tiap 10 dtk (lihat di Serial Monitor)
    sysLastMs = sysNow;
    Serial.printf("[SYS] heap=%u min=%u blok=%u loopMax=%lums stackSisa loop=%u online=%u\n", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMinFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), sysMaxLoop,
                  (unsigned)uxTaskGetStackHighWaterMark(NULL), onlineTaskHandle ? (unsigned)uxTaskGetStackHighWaterMark(onlineTaskHandle) : 0u);
    sysMaxLoop = 0;
  }
  updateBuzzerTask();
  handleRuntimeSerial();
  updateTimeFromNTP();
  if (systemMode == WIFI_MODE) {
    if (!setupActive) pollButtons();     // saat setup awal tombol dibaca oleh setupWizardTick()
    updateDeepSleepPending();
  }

  // ════════════════════════════════════════════
  // VALIDASI WIFI BARU – try before save
  // ════════════════════════════════════════════
  if (wifiValidating) {
    if (apActive && webStarted) server.handleClient();   // halaman setup (AP darurat) tetap bisa dibuka selama validasi
    if (WiFi.status() == WL_CONNECTED) {
      // ── SUKSES: baru simpan ke NVS ──────────
      saveWifiCredentials(wifiTrySSID, wifiTryPASS);
      wifiSSID       = wifiTrySSID;
      wifiPASS       = wifiTryPASS;
      wifiValidating = false;
      webStarted     = false;
      staInitDone    = false;
      wifiLastResult = "ok:" + wifiTrySSID;
      Serial.println("[WIFI] Credential baru valid, tersimpan: " + wifiTrySSID);
      bootScreen("WiFi Baru Sukses!", WiFi.localIP().toString());
      delay(1500);
      // Lanjut normal (web server akan restart di loop)
    } else if (millis() - wifiTryMs > WIFI_VALIDATE_MS) {
      // ── GAGAL: kembalikan credential lama ───
      Serial.println("[WIFI] Gagal konek ke: " + wifiTrySSID);
      Serial.println("[WIFI] Restore ke: " + wifiOldSSID);
      wifiSSID       = wifiOldSSID;
      wifiPASS       = wifiOldPASS;
      wifiValidating = false;
      wifiLastResult = "fail:" + wifiTrySSID;
      bootScreen("WiFi Baru GAGAL!", wifiTrySSID);
      delay(800);
      bootScreen("Kembali WiFi lama", wifiOldSSID);
      WiFi.disconnect(true);
      delay(300);
      WiFi.begin(wifiOldSSID.c_str(), wifiOldPASS.c_str());
      if (!apActive) webStarted = false;     // saat AP darurat aktif, web tetap jalan
      if (wifiValidateBT) {
        // Dari BT → balik ke NAV mode
        delay(500);
        switchToNavMode();
      }
    } else {
      // ── Masih menunggu: tampil progress di OLED ─
      static unsigned long lastVDraw = 0;
      if (millis() - lastVDraw > 400) {
        lastVDraw = millis();
        int elapsed = (millis() - wifiTryMs) / 1000;
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
        display.setCursor(4, 2); display.print("Cek WiFi Baru...");
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 16); display.print(wifiTrySSID);
        display.drawLine(0, 26, 128, 26, SSD1306_WHITE);
        display.setCursor(0, 30); display.print("Tunggu: ");
        display.print(elapsed); display.print(" / 20 dtk");
        // Progress bar
        int barW = map(elapsed, 0, 20, 0, 124);
        display.drawRect(2, 42, 124, 8, SSD1306_WHITE);
        display.fillRect(2, 42, barW, 8, SSD1306_WHITE);
        display.setCursor(0, 54); display.print("Jgn matikan ESP32!");
        display.display();
      }
    }
    return; // Skip semua proses lain selama validasi
  }
  // ════════════════════════════════════════════

  if (systemMode == WIFI_MODE) {
    // Saat Deep Sleep: WiFi sengaja mati, jangan reconnect / start web server
    if (!deepSleepMode) {
      if (WiFi.status() == WL_CONNECTED) {
        wifiDownSince = 0;
        wifiRetryCount = 0;
        if (apActive) stopEmergencyAP();
        if ((!webStarted || !staInitDone)) {
          Serial.println("[WIFI] Connected, starting web server");
          reqNtpSync = true;              // sinkron jam & cuaca dikerjakan task latar belakang
          nextWeatherAt = 0;
          if (!webStarted) startWebServer();
          staInitDone = true;
        }
      } else {
        staInitDone = false;
        if (wifiDownSince == 0) wifiDownSince = millis();
        if (!wifiBusy && millis() - lastWiFiRetryMs > wifiRetryIntervalMs) {
          lastWiFiRetryMs = millis();
          wifiRetryCount++;
          if (!apActive && wifiListN > 1 && wifiRetryCount % 3 == 0) {
            Serial.println("[WIFI] Coba WiFi tersimpan lainnya");   // pindah otomatis ke WiFi cadangan
            reqWifiFailover = true;                   // dikerjakan task latar belakang (tidak membekukan OLED)
          } else {
            Serial.println("[WIFI] Retry reconnect");
            if (wifiRetryCount % 2 == 1) {
              WiFi.reconnect();                       // cara halus dulu, tanpa memutus total
            } else {
              WiFi.disconnect(false, false);
              WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());
            }
          }
        }
        if (!apActive && millis() - wifiDownSince > AP_FALLBACK_MS) startEmergencyAP(true);   // semua WiFi gagal -> AP darurat
      }
    }

    // Alarm checker
    updateAlarmState();
    timerTick();               // timer tetap berjalan & berbunyi walau OLED sedang menampilkan hal lain
    updatePrayerReminder();    // pengingat sholat memakai jadwal tersimpan (jalan juga saat Deep Sleep)
    updatePrayerAlert();

    {
      updateSleepState();
      applyBrightnessForTime();
      updateAutoMode();

      if (webStarted) server.handleClient();

      if (setupActive) {
        setupWizardTick();     // setup awal beranimasi (WiFi -> unduh animasi mata -> cek dashboard)
      } else if (oledSleepState) {
        setDisplayOn(false);
      } else if (prayerAlertActive) {
        drawPrayerAlert();     // popup waktu sholat
      } else if (gameActive) {
        gameLoop();            // OLED sedang dipakai Mini Games
      } else if (toolActive) {
        toolLoop();            // OLED sedang dipakai Timer & Stopwatch
      } else {
        drawSmartDisplay();
      }
      if (webStarted) server.handleClient();   // layani web sekali lagi setelah menggambar OLED (respons lebih cepat)
      delay(1);                                // beri waktu task WiFi/latar, hindari loop memutar tanpa henti
    }
  } else {
    pollNavButtons();
    updateNavMode();
  }
}

// ═══════════════════════════════════════════════════════════
// UTILITIES
// ═══════════════════════════════════════════════════════════
String jsonEscape(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }
  return out;
}

void bootScreen(const String &line1, const String &line2) {
  display.clearDisplay();
  display.fillRect(0, 0, 128, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(5, 3);
  display.print("ESP32 SmartDisplay");
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(5, 22);
  display.print(line1);
  display.setCursor(5, 38);
  display.print(line2);
  display.display();
}

String readPrefsString(const char* key, const String &fallback) {
  return prefs.getString(key, fallback);
}
float readPrefsFloat(const char* key, float fallback) {
  return prefs.getFloat(key, fallback);
}
int readPrefsInt(const char* key, int fallback) {
  return prefs.getInt(key, fallback);
}
bool readPrefsBool(const char* key, bool fallback) {
  return prefs.getBool(key, fallback);
}

void saveModeToPrefs(uint8_t mode) {
  prefs.putUChar("mode", mode);
}

uint8_t readModeFromPrefs() {
  return prefs.getUChar("mode", WIFI_MODE);
}

void saveWifiCredentials(const String &ssid, const String &pass) {
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  wifiSSID = ssid;
  wifiPASS = pass;
  wifiListAdd(ssid, pass);
  Serial.println("[BOOT] WiFi credentials saved to NVS");
}

bool parseCredentialLine(const String &line, String &ssidOut, String &passOut) {
  String s = line;
  s.trim();

  // Format 1: WIFI:namaWifi:password  (mudah diketik)
  if (s.startsWith("WIFI:")) {
    String rest = s.substring(5);
    int col = rest.indexOf(':');
    if (col < 1) return false;
    ssidOut = rest.substring(0, col);
    passOut = rest.substring(col + 1);
    ssidOut.trim(); passOut.trim();
    return ssidOut.length() > 0;
  }

  // Format 2: SSID:namaWifi,PASS:password  (format lama)
  if (s.startsWith("SSID:") && s.indexOf(",PASS:") > 0) {
    int commaPos = s.indexOf(",PASS:");
    ssidOut = s.substring(5, commaPos);
    passOut = s.substring(commaPos + 6);
    ssidOut.trim(); passOut.trim();
    return ssidOut.length() > 0;
  }

  return false;
}

bool ensureWifiCredentialsFromSerial() {
  static String serialLine;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialLine.length() == 0) continue;
      String s, p;
      if (parseCredentialLine(serialLine, s, p)) {
        saveWifiCredentials(s, p);
        bootScreen("WiFi tersimpan", s);
        Serial.println("[BOOT] WiFi credentials accepted");
        serialLine = "";
        delay(700);
        return true;
      } else {
        Serial.println("[BOOT] Format salah. Gunakan: SSID:namaWifi,PASS:passwordWifi");
        bootScreen("Format salah", "SSID:namaWifi,PASS:password");
        serialLine = "";
        return false;
      }
    } else {
      serialLine += c;
      if (serialLine.length() > 120) serialLine = "";
    }
  }

  static unsigned long lastMsg = 0;
  if (millis() - lastMsg > 1200) {
    lastMsg = millis();
    Serial.println("[BOOT] Kirim format: SSID:namaWifi,PASS:passwordWifi");
  }
  return false;
}

String formatTimeString() {
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", curHour, curMin, curSec);
  return String(buf);
}

String formatDateString() {
  char buf[32];
  snprintf(buf, sizeof(buf), "%s, %02d %s %04d", dayNames[curWday], curDay, monNames[curMon], curYear);
  return String(buf);
}

String wifiSignalBars(int rssi) {
  // Tidak dipakai lagi - diganti pixel bars langsung di drawClockScreen
  return "";
}

String getCurrentModeText() {
  return systemMode == WIFI_MODE ? "WIFI_MODE" : "NAV_MODE";
}

String getCurrentSleepText() {
  return oledSleepState ? "SLEEP" : "AWAKE";
}

String getCurrentAlarmText() {
  char buf[12];
  snprintf(buf, sizeof(buf), "%02d:%02d", alarmHour, alarmMin);
  return String(buf);
}

bool isInSleepWindow(int startHour, int startMin, int endHour, int endMin, int currentHour, int currentMin) {
  int startTotal = startHour * 60 + startMin;
  int endTotal   = endHour * 60 + endMin;
  int curTotal   = currentHour * 60 + currentMin;
  if (startTotal == endTotal) return false;
  if (startTotal < endTotal) return (curTotal >= startTotal && curTotal < endTotal);
  return (curTotal >= startTotal || curTotal < endTotal);
}

void applyOLEDContrast(uint8_t level) {
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(level);
}

void setDisplayOn(bool on) {
  static bool lastOn = true;
  if (on == lastOn) return;
  lastOn = on;
  if (on) display.ssd1306_command(SSD1306_DISPLAYON);
  else    display.ssd1306_command(SSD1306_DISPLAYOFF);
}

void buzzerOn(uint16_t freq) {
  ledcWriteTone(BUZZER_PIN, freq);
  ledcWrite(BUZZER_PIN, 128);
}

void buzzerOff() {
  ledcWriteTone(BUZZER_PIN, 0);
  ledcWrite(BUZZER_PIN, 0);
}

void startAlarmBeepPattern(uint8_t count, uint16_t onMs, uint16_t offMs) {
  buzzerPatternActive = true;
  buzzerPatternCount = count;
  buzzerPatternDone = 0;
  buzzerPatternOnMs = onMs;
  buzzerPatternOffMs = offMs;
  buzzerPatternOnState = false;
  buzzerPatternTs = 0;
  Serial.println("[ALARM] Beep pattern started");
}

void updateBuzzerTask() {
  updateBuzzerSiren();
  if (!buzzerPatternActive) return;

  unsigned long now = millis();

  if (buzzerPatternTs == 0) {
    buzzerPatternOnState = true;
    buzzerPatternTs = now;
    buzzerOn(buzzerFreq);
    return;
  }

  if (buzzerPatternOnState) {
    if (now - buzzerPatternTs >= buzzerPatternOnMs) {
      buzzerOff();
      buzzerPatternOnState = false;
      buzzerPatternTs = now;
      buzzerPatternDone++;
      if (buzzerPatternDone >= buzzerPatternCount) {
        buzzerPatternActive = false;
        return;
      }
    }
  } else {
    if (now - buzzerPatternTs >= buzzerPatternOffMs) {
      buzzerPatternOnState = true;
      buzzerPatternTs = now;
      buzzerOn(buzzerFreq);
    }
  }
}

// Alarm bangun pagi: bunyi warble (2 nada bergantian) selama 1 menit penuh,
// bukan cuma beberapa kali beep pendek.
void startAlarmSiren(unsigned long durationMs) {
  buzzerSirenActive = true;
  buzzerSirenDurationMs = durationMs;
  buzzerSirenStartTs = millis();
  buzzerSirenToneTs = 0;
  buzzerSirenToneHigh = true;
  Serial.println("[ALARM] Siren started");
}

void stopAlarmSiren() {
  buzzerSirenActive = false;
  buzzerOff();
}

void updateBuzzerSiren() {
  if (!buzzerSirenActive) return;
  unsigned long now = millis();

  if (now - buzzerSirenStartTs >= buzzerSirenDurationMs) {
    stopAlarmSiren();
    return;
  }

  if (buzzerSirenToneTs == 0 || now - buzzerSirenToneTs >= buzzerSirenToneMs) {
    buzzerSirenToneTs = now;
    buzzerSirenToneHigh = !buzzerSirenToneHigh;
    buzzerOn(buzzerSirenToneHigh ? buzzerSirenFreqHigh : buzzerSirenFreqLow);
  }
}

void beepTest() {
  startAlarmBeepPattern(2, 120, 90);
}

void updateAutoMode() {
  if (!autoMode) return;
  if (millis() - lastModeRotateMs >= modeRotateIntervalMs) {
    lastModeRotateMs = millis();
    // Hanya berpindah ke halaman yang dicentang (autoPageMask). Semua kosong = tidak berganti.
    for (int i = 1; i <= PAGE_COUNT; i++) {
      uint8_t n = (displayMode + i) % PAGE_COUNT;
      if (autoPageMask & (1 << n)) {
        if (n != displayMode) {
          displayMode = n;
          Serial.println("[WEB] Auto display mode rotate");
        }
        break;
      }
    }
  }
}

void updateSleepState() {

  if (!sleepEnabled) {
    if (oledSleepState) {
      oledSleepState = false;
      Serial.println("[SLEEP] OLED on");
      setDisplayOn(true);
    }
    return;
  }

  bool sleepNow = isInSleepWindow(sleepStartHour, sleepStartMin, sleepEndHour, sleepEndMin, curHour, curMin);

  if (sleepNow != oledSleepState) {

    oledSleepState = sleepNow;

    Serial.print("[SLEEP] ");
    Serial.println(oledSleepState ? "OLED off" : "OLED on");

    setDisplayOn(!oledSleepState);
  }
}

void applyBrightnessForTime() {
  if (!autoBrightness) {
    applyOLEDContrast((uint8_t)manualBrightness);
    return;
  }
  int level = oledSleepState ? 0 : ((curHour >= 6 && curHour < 18) ? dayBrightness : nightBrightness);
  level = constrain(level, 0, 255);
  applyOLEDContrast((uint8_t)level);
}

void updateAlarmState() {
  if (!alarmEnabled) return;
  if (oledSleepState) return;
  if (curHour == alarmHour && curMin == alarmMin && curSec < 3) {
    int ymd = curYear * 10000 + curMon * 100 + curDay;
    if (alarmLastTriggeredYMD != ymd) {
      alarmLastTriggeredYMD = ymd;
      Serial.println("[ALARM] Triggered");
      startAlarmSiren(60000); // bunyi 1 menit penuh
    }
  }
}

void syncNTP() {
  if (WiFi.status() != WL_CONNECTED) return;
  // PENTING: jangan panggil setenv("TZ", ...) + tzset() di sini.
  // configTime(gmtOffset_sec, ...) di bawah ini SUDAH menggeser jam ke WIB.
  // Kalau TZ di-set lagi setelahnya, offset +7 jam diterapkan DUA KALI,
  // sehingga curHour jadi maju 7 jam dari jam WIB sebenarnya — ini yang
  // menyebabkan alarm tidak pernah cocok di jam yang diset.
  configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");
  Serial.println("[WIFI] NTP sync requested");

  struct tm t;
  unsigned long start = millis();
  while (!getLocalTime(&t, 250) && millis() - start < 10000) {
    delay(50);
  }
  if (getLocalTime(&t, 100)) {
    Serial.println("[WIFI] NTP sync OK");
    curHour = t.tm_hour;
    curMin = t.tm_min;
    curSec = t.tm_sec;
    curDay = t.tm_mday;
    curMon = t.tm_mon + 1;
    curYear = t.tm_year + 1900;
    curWday = t.tm_wday;
  } else {
    Serial.println("[WIFI] NTP sync timeout");
  }
}

void updateTimeFromNTP() {
  struct tm t;
  if (getLocalTime(&t)) {
    curHour = t.tm_hour;
    curMin = t.tm_min;
    curSec = t.tm_sec;
    curDay = t.tm_mday;
    curMon = t.tm_mon + 1;
    curYear = t.tm_year + 1900;
    curWday = t.tm_wday;
  }
}

void saveWeatherToPrefs() {
  prefs.putFloat("w_temp", wTemp);
  prefs.putFloat("w_humid", wHumid);
  prefs.putFloat("w_feels", wFeels);
  prefs.putFloat("w_wind", wWind);
  prefs.putString("w_desc", wDesc);
  prefs.putString("w_last", wLastUpdate);
  prefs.putBool("w_valid", weatherValid);
}

void loadWeatherFromPrefs() {
  wTemp = prefs.getFloat("w_temp", 0.0f);
  wHumid = prefs.getFloat("w_humid", 0.0f);
  wFeels = prefs.getFloat("w_feels", 0.0f);
  wWind = prefs.getFloat("w_wind", 0.0f);
  wDesc = prefs.getString("w_desc", "Belum ada data");
  wLastUpdate = prefs.getString("w_last", "--:--");
  weatherValid = prefs.getBool("w_valid", false);
}

void loadPrefs() {
  systemMode = (SystemMode)readModeFromPrefs();
  wifiSSID = prefs.getString("ssid", "");
  wifiPASS = prefs.getString("pass", "");
  displayMode = prefs.getUChar("dispMode", 0);
  autoMode = prefs.getBool("autoMode", true);
  autoBrightness = prefs.getBool("autoBright", true);
  manualBrightness = prefs.getInt("manBright", 220);
  dayBrightness = prefs.getInt("dayBright", 220);
  nightBrightness = prefs.getInt("nightBright", 10);
  sleepStartHour = prefs.getInt("sleepStart", 22);
  sleepStartMin = prefs.getInt("sleepStartM", 0);
  sleepEndHour = prefs.getInt("sleepEnd", 6);
  sleepEndMin = prefs.getInt("sleepEndM", 0);
  alarmEnabled = prefs.getBool("alarmEn", false);
  alarmHour = prefs.getInt("alarmHour", 7);
  alarmMin = prefs.getInt("alarmMin", 0);
  sleepEnabled = prefs.getBool("sleepEn", false);
  eyeMinSec = constrain(prefs.getInt("eyeMin", 4), 1, 600);
  eyeMaxSec = constrain(prefs.getInt("eyeMax", 20), 1, 600);
  if (eyeMaxSec < eyeMinSec) eyeMaxSec = eyeMinSec;
  autoPageMask = prefs.getUChar("autoMask", 0x7F) & ((1 << PAGE_COUNT) - 1);
  if (displayMode >= PAGE_COUNT) displayMode = 0;
  prayerReminderOn = prefs.getBool("prOn", true);
  prayerMask = prefs.getUChar("prMask", 0x1F) & 0x1F;
  goldAdjPct = prefs.getFloat("goldAdj", 0.0f);
  loadOnlineCache();
  wifiListLoad();
  Serial.println("[BOOT] Preferences loaded");
}

void saveSystemSettings() {
  prefs.putUChar("dispMode", displayMode);
  prefs.putBool("autoMode", autoMode);
  prefs.putBool("autoBright", autoBrightness);
  prefs.putInt("manBright", manualBrightness);
  prefs.putInt("dayBright", dayBrightness);
  prefs.putInt("nightBright", nightBrightness);
  prefs.putInt("sleepStart", sleepStartHour);
  prefs.putInt("sleepStartM", sleepStartMin);
  prefs.putInt("sleepEnd", sleepEndHour);
  prefs.putInt("sleepEndM", sleepEndMin);
  prefs.putBool("alarmEn", alarmEnabled);
  prefs.putInt("alarmHour", alarmHour);
  prefs.putInt("alarmMin", alarmMin);
  prefs.putBool("sleepEn", sleepEnabled);
  prefs.putInt("eyeMin", eyeMinSec);
  prefs.putInt("eyeMax", eyeMaxSec);
  prefs.putUChar("autoMask", autoPageMask);
  prefs.putBool("prOn", prayerReminderOn);
  prefs.putUChar("prMask", prayerMask);
  prefs.putFloat("goldAdj", goldAdjPct);
  prefs.putUChar("mode", systemMode);
}

String buildWeatherURL() {
  String url = "http://api.weatherapi.com/v1/current.json?key=";
  url += weatherApiKey;
  url += "&q=";
  url += weatherLocation;
  url += "&aqi=no&lang=id";
  return url;
}

bool fetchWeatherFromAPI() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Tidak bisa ambil cuaca: WiFi offline");
    return false;
  }

  HTTPClient http;
  String url = buildWeatherURL();
  Serial.println("[WEB] Fetch weather");
  int code = -1;
  for (int attempt = 1; attempt <= 3; attempt++) {     // WiFi kadang goyah: coba ulang sebelum menyerah
    if (WiFi.status() != WL_CONNECTED) break;
    http.setConnectTimeout(6000);
    http.setTimeout(8000);
    http.begin(url);
    code = http.GET();
    if (code == HTTP_CODE_OK) break;
    Serial.printf("[WEB] Weather HTTP %d (percobaan %d/3)\n", code, attempt);
    http.end();
    delay(600 * attempt);
  }

  if (code != HTTP_CODE_OK) return false;

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(6144);   // di heap, BUKAN di stack (sebelumnya 8 KB di stack = overflow)
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("[WEB] Weather JSON error: ");
    Serial.println(err.c_str());
    return false;
  }

  JsonObject cur = doc["current"];
  if (cur.isNull()) return false;

  // Koordinat lokasi dari WeatherAPI dipakai juga untuk prakiraan, kualitas udara & jadwal sholat
  JsonObject loc = doc["location"];
  if (!loc.isNull()) {
    float la = loc["lat"] | 0.0f, lo = loc["lon"] | 0.0f;
    if ((la != 0.0f || lo != 0.0f) && (fabsf(la - geoLat) > 0.0001f || fabsf(lo - geoLon) > 0.0001f)) {
      geoLat = la;
      geoLon = lo;
      prefs.putFloat("geoLat", geoLat);
      prefs.putFloat("geoLon", geoLon);
      prayerYmd = 0;               // lokasi berubah: ambil ulang jadwal sholat
      nextFcAt = 0;
    }
  }

  wTemp = cur["temp_c"] | 0.0f;
  wHumid = cur["humidity"] | 0.0f;
  wFeels = cur["feelslike_c"] | 0.0f;
  wWind = cur["wind_kph"] | 0.0f;
  { StrLock l; wDesc = String((const char*)(cur["condition"]["text"] | "N/A")); }
  weatherValid = true;

  struct tm t;
  if (getLocalTime(&t, 5)) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    StrLock l;
    wLastUpdate = String(buf);
  } else {
    StrLock l;
    wLastUpdate = "--:--";
  }

  saveWeatherToPrefs();
  lastWeatherFetchMs = millis();
  nextWeatherAt = millis() + 1800000UL;     // perbarui otomatis tiap 30 menit

  Serial.println("[WEB] Weather updated");
  return true;
}

// ═══════════════════════════════════════════════════════════
// BANYAK WIFI TERSIMPAN + AP DARURAT
// ═══════════════════════════════════════════════════════════
void wifiListSave() {
  prefs.putInt("wlN", wifiListN);
  for (int i = 0; i < WIFI_MAX; i++) {
    char ks[8], kp[8];
    snprintf(ks, sizeof(ks), "wls%d", i);
    snprintf(kp, sizeof(kp), "wlp%d", i);
    if (i < wifiListN) {
      prefs.putString(ks, wifiListSsid[i]);
      prefs.putString(kp, wifiListPass[i]);
    } else {
      prefs.remove(ks);
      prefs.remove(kp);
    }
  }
}

void wifiListLoad() {
  wifiListN = constrain(prefs.getInt("wlN", 0), 0, WIFI_MAX);
  for (int i = 0; i < wifiListN; i++) {
    char ks[8], kp[8];
    snprintf(ks, sizeof(ks), "wls%d", i);
    snprintf(kp, sizeof(kp), "wlp%d", i);
    wifiListSsid[i] = prefs.getString(ks, "");
    wifiListPass[i] = prefs.getString(kp, "");
  }
  // Migrasi dari versi lama (hanya 1 WiFi tersimpan di "ssid"/"pass")
  if (wifiListN == 0 && wifiSSID.length() > 0) {
    wifiListSsid[0] = wifiSSID;
    wifiListPass[0] = wifiPASS;
    wifiListN = 1;
    wifiListSave();
  }
  if (wifiListN > 0) {
    wifiSSID = wifiListSsid[0];
    wifiPASS = wifiListPass[0];
  }
}

// Tambah / perbarui WiFi, lalu taruh di urutan paling depan
void wifiListAdd(const String &ssid, const String &pass) {
  int idx = -1;
  for (int i = 0; i < wifiListN; i++) if (wifiListSsid[i] == ssid) idx = i;
  if (idx < 0) idx = (wifiListN < WIFI_MAX) ? wifiListN++ : (WIFI_MAX - 1);   // penuh: buang yang paling lama
  for (int i = idx; i > 0; i--) {
    wifiListSsid[i] = wifiListSsid[i - 1];
    wifiListPass[i] = wifiListPass[i - 1];
  }
  wifiListSsid[0] = ssid;
  wifiListPass[0] = pass;
  wifiListSave();
}

void wifiListRemove(int idx) {
  if (idx < 0 || idx >= wifiListN) return;
  for (int i = idx; i < wifiListN - 1; i++) {
    wifiListSsid[i] = wifiListSsid[i + 1];
    wifiListPass[i] = wifiListPass[i + 1];
  }
  wifiListN--;
  wifiListSsid[wifiListN] = "";
  wifiListPass[wifiListN] = "";
  wifiListSave();
}

void startEmergencyAP(bool show) {
  if (apActive) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  apActive = true;
  Serial.print("[AP] Darurat aktif: ");
  Serial.print(AP_SSID);
  Serial.print(" / sandi ");
  Serial.print(AP_PASS);
  Serial.print(" -> http://");
  Serial.println(WiFi.softAPIP());
  if (show) {
    bootScreen("AP Darurat aktif", AP_SSID);
    delay(1800);
    bootScreen(String("Sandi: ") + AP_PASS, "Buka " + WiFi.softAPIP().toString());
    delay(2500);
  }
  if (!webStarted) startWebServer();
}

void stopEmergencyAP() {
  if (!apActive) return;
  WiFi.softAPdisconnect(true);        // matikan AP saja, koneksi STA tetap
  apActive = false;
  Serial.println("[AP] Darurat dimatikan (WiFi tersambung)");
}

bool wifiTryConnect(const String &ssid, const String &pass, uint32_t timeoutMs, bool show) {
  WiFi.disconnect(false, false);
  delay(100);
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    if (show) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.print("Connecting WiFi...");
      display.setCursor(0, 16);
      display.print(ssid);
      display.setCursor(0, 32);
      display.print("Tunggu...");
      display.display();
    }
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

// Scan, lalu coba WiFi tersimpan yang terlihat mulai dari sinyal terkuat
bool connectBestSavedWifi(bool show) {
  if (wifiListN == 0) return false;
  int rssi[WIFI_MAX];
  for (int i = 0; i < WIFI_MAX; i++) rssi[i] = -999;

  if (show) bootScreen("Mencari WiFi...", String(wifiListN) + " tersimpan");
  int n = WiFi.scanNetworks(false, true);
  bool scanOk = (n > 0);
  if (scanOk) {
    for (int j = 0; j < n; j++) {
      String s = WiFi.SSID(j);
      for (int i = 0; i < wifiListN; i++) {
        if (s == wifiListSsid[i] && WiFi.RSSI(j) > rssi[i]) rssi[i] = WiFi.RSSI(j);
      }
    }
  }
  WiFi.scanDelete();

  int order[WIFI_MAX];
  int cnt = 0;
  for (int i = 0; i < wifiListN; i++) {
    if (!scanOk || rssi[i] > -999) order[cnt++] = i;     // scan gagal: coba semua sesuai urutan
  }
  if (scanOk) {                                          // urut sinyal kuat -> lemah
    for (int a = 1; a < cnt; a++) {
      int key = order[a], b = a - 1;
      while (b >= 0 && rssi[order[b]] < rssi[key]) { order[b + 1] = order[b]; b--; }
      order[b + 1] = key;
    }
  }

  for (int k = 0; k < cnt; k++) {
    int i = order[k];
    Serial.println("[WIFI] Coba: " + wifiListSsid[i]);
    if (wifiTryConnect(wifiListSsid[i], wifiListPass[i], 12000, show)) {
      if (wifiSSID != wifiListSsid[i]) wifiSSID = wifiListSsid[i];
      if (wifiPASS != wifiListPass[i]) wifiPASS = wifiListPass[i];
      return true;
    }
  }
  return false;
}

// ═══════════════════════════════════════════════════════════
// PENYIMPANAN DATA ONLINE (supaya tampil walau belum/tidak ada internet)
// ═══════════════════════════════════════════════════════════
struct FcBlob { uint8_t valid; int16_t aqi; char last[6]; FcDay d[FC_DAYS]; };

void saveForecastToPrefs() {
  FcBlob b;
  memset(&b, 0, sizeof(b));
  b.valid = fcValid ? 1 : 0;
  b.aqi = aqiUS;
  fcLast.toCharArray(b.last, sizeof(b.last));
  memcpy(b.d, fcDay, sizeof(fcDay));
  prefs.putBytes("fcBlob", &b, sizeof(b));
}

void savePrayerToPrefs() {
  prefs.putBytes("prT", prayerMin, sizeof(prayerMin));
  prefs.putInt("prYmd", prayerYmd);
  prefs.putUInt("prEp", prayerEpoch);
}

void saveGoldToPrefs() {
  prefs.putFloat("goldRaw", (float)goldRawGram);
  prefs.putFloat("goldOpen", (float)goldOpenGram);
  prefs.putInt("goldY", goldOpenYmd);
  prefs.putString("goldL", goldLast);
  prefs.putFloat("goldX", (float)goldXauUsd);
  prefs.putFloat("goldFx", (float)goldUsdIdr);
}

void loadOnlineCache() {
  geoLat = prefs.getFloat("geoLat", -7.42f);
  geoLon = prefs.getFloat("geoLon", 109.25f);

  FcBlob b;
  if (prefs.getBytesLength("fcBlob") == sizeof(b) && prefs.getBytes("fcBlob", &b, sizeof(b)) == sizeof(b)) {
    fcValid = b.valid != 0;
    aqiUS = b.aqi;
    b.last[sizeof(b.last) - 1] = 0;
    fcLast = String(b.last);
    memcpy(fcDay, b.d, sizeof(fcDay));
  }

  if (prefs.getBytesLength("prT") == sizeof(prayerMin)) {
    prefs.getBytes("prT", prayerMin, sizeof(prayerMin));
    prayerYmd = prefs.getInt("prYmd", 0);
    prayerEpoch = prefs.getUInt("prEp", 0);
    prayerValid = prayerYmd > 0;
  }

  goldRawGram = prefs.getFloat("goldRaw", 0);
  goldOpenGram = prefs.getFloat("goldOpen", 0);
  goldOpenYmd = prefs.getInt("goldY", 0);
  goldLast = prefs.getString("goldL", "--:--");
  goldXauUsd = prefs.getFloat("goldX", 0);
  goldUsdIdr = prefs.getFloat("goldFx", 0);
  goldValid = goldRawGram > 0;
}

// ═══════════════════════════════════════════════════════════
// PENGAMBILAN DATA ONLINE (HTTPS, tanpa API key)
// ═══════════════════════════════════════════════════════════
bool dueAt(unsigned long t) { return (long)(millis() - t) >= 0; }

int todayYmd() { return curYear * 10000 + curMon * 100 + curDay; }

bool timeIsSynced() { return time(nullptr) > 1700000000; }

// GET https lalu parse JSON langsung dari stream (dengan filter agar hemat RAM)
bool httpsGetJsonOnce(const String &url, JsonDocument &doc, JsonDocument &filter) {
  WiFiClientSecure cl;
  cl.setInsecure();
  cl.setTimeout(8);                          // detik: batas tunggu data saat membaca
  HTTPClient http;
  http.setConnectTimeout(6000);
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.useHTTP10(true);                       // hindari chunked supaya bisa di-stream
  http.setUserAgent("ESP32-SmartDisplay/1.0");
  if (!http.begin(cl, url)) {
    Serial.println("[NET] begin gagal");
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.print("[NET] HTTP ");
    Serial.print(code);
    Serial.print(" heap=");
    Serial.println(ESP.getFreeHeap());
    http.end();
    return false;
  }
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.print("[NET] JSON error: ");
    Serial.println(err.c_str());
    return false;
  }
  return true;
}

// Dicoba sampai 2x supaya gangguan WiFi sesaat tidak langsung dianggap gagal (tidak lebih, agar tidak menahan memori TLS lama)
bool httpsGetJson(const String &url, JsonDocument &doc, JsonDocument &filter) {
  for (int attempt = 1; attempt <= 2; attempt++) {
    if (WiFi.status() != WL_CONNECTED) return false;
    doc.clear();
    if (httpsGetJsonOnce(url, doc, filter)) return true;
    Serial.printf("[NET] Gagal (percobaan %d/2)\n", attempt);
    vTaskDelay(pdMS_TO_TICKS(600 * attempt));
  }
  return false;
}

// ── Prakiraan 5 hari + UV (Open-Meteo) + AQI ────────────────
bool fetchForecast() {
  if (WiFi.status() != WL_CONNECTED) return false;
  Serial.println("[NET] Fetch prakiraan");
  char url[320];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,uv_index_max"
           "&timezone=Asia%%2FJakarta&forecast_days=%d", geoLat, geoLon, FC_DAYS);
  StaticJsonDocument<256> filter;
  JsonObject fd = filter.createNestedObject("daily");
  fd["time"] = true;
  fd["weather_code"] = true;
  fd["temperature_2m_max"] = true;
  fd["temperature_2m_min"] = true;
  fd["precipitation_probability_max"] = true;
  fd["uv_index_max"] = true;

  DynamicJsonDocument doc(3072);
  if (!httpsGetJson(String(url), doc, filter)) return false;

  JsonObject d = doc["daily"];
  JsonArray tm = d["time"];
  if (tm.isNull() || tm.size() == 0) return false;

  int n = min((int)tm.size(), FC_DAYS);
  for (int i = 0; i < FC_DAYS; i++) {
    FcDay &f = fcDay[i];
    memset(&f, 0, sizeof(f));
    if (i >= n) continue;
    int y = 0, m = 0, dd = 0;
    sscanf((const char*)(tm[i] | ""), "%d-%d-%d", &y, &m, &dd);
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = dd;
    t.tm_hour = 12;
    mktime(&t);
    f.mon = m;
    f.dom = dd;
    f.wday = t.tm_wday;
    f.code = (uint8_t)constrain((int)lroundf(d["weather_code"][i] | 0.0f), 0, 255);
    f.tmax = (int8_t)constrain((int)lroundf(d["temperature_2m_max"][i] | 0.0f), -50, 60);
    f.tmin = (int8_t)constrain((int)lroundf(d["temperature_2m_min"][i] | 0.0f), -50, 60);
    f.pop = (uint8_t)constrain((int)lroundf(d["precipitation_probability_max"][i] | 0.0f), 0, 100);
    f.uv10 = (uint8_t)constrain((int)lroundf((d["uv_index_max"][i] | 0.0f) * 10.0f), 0, 255);
  }
  fcValid = true;

  // Kualitas udara saat ini (gagal tidak membatalkan prakiraan)
  snprintf(url, sizeof(url),
           "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f"
           "&current=us_aqi,pm2_5&timezone=Asia%%2FJakarta", geoLat, geoLon);
  StaticJsonDocument<128> f2;
  JsonObject c2 = f2.createNestedObject("current");
  c2["us_aqi"] = true;
  c2["pm2_5"] = true;
  DynamicJsonDocument d2(512);
  if (httpsGetJson(String(url), d2, f2)) {
    JsonObject cur = d2["current"];
    if (!cur.isNull()) {
      float a = cur["us_aqi"] | -1.0f;
      aqiUS = (a >= 0) ? (int16_t)lroundf(a) : -1;
      pm25Now = cur["pm2_5"] | -1.0f;
    }
  }

  struct tm lt;
  if (getLocalTime(&lt, 5)) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);
    fcLast = String(buf);
  }
  saveForecastToPrefs();
  Serial.println("[NET] Prakiraan OK");
  return true;
}

int forecastStartIndex() {
  int today = curMon * 100 + curDay;
  for (int i = 0; i < FC_DAYS; i++) {
    if ((int)fcDay[i].mon * 100 + fcDay[i].dom >= today) return i;
  }
  return 0;
}

int fcUvToday10() {                           // UV hari ini x10, -1 jika belum ada data
  if (!fcValid) return -1;
  int today = curMon * 100 + curDay;
  for (int i = 0; i < FC_DAYS; i++) {
    if ((int)fcDay[i].mon * 100 + fcDay[i].dom == today) return fcDay[i].uv10;
  }
  return -1;
}

const char* wxCodeText(uint8_t c) {
  if (c <= 1) return "Cerah";
  if (c == 2) return "Berawan";
  if (c == 3) return "Mendung";
  if (c == 45 || c == 48) return "Kabut";
  if (c >= 51 && c <= 57) return "Gerimis";
  if (c >= 61 && c <= 67) return "Hujan";
  if (c >= 71 && c <= 77) return "Salju";
  if (c >= 80 && c <= 82) return "Hujan";
  if (c == 85 || c == 86) return "Salju";
  if (c >= 95) return "Badai";
  return "-";
}

// ── Jadwal sholat (Aladhan, metode 20 = Kemenag RI) ─────────
bool fetchPrayerTimes() {
  if (WiFi.status() != WL_CONNECTED || !timeIsSynced()) return false;
  Serial.println("[NET] Fetch jadwal sholat");
  char url[200];
  snprintf(url, sizeof(url),
           "https://api.aladhan.com/v1/timings/%02d-%02d-%04d?latitude=%.4f&longitude=%.4f&method=20&timezonestring=Asia/Jakarta",
           curDay, curMon, curYear, geoLat, geoLon);
  StaticJsonDocument<256> filter;
  JsonObject ft = filter.createNestedObject("data").createNestedObject("timings");
  ft["Fajr"] = true;
  ft["Sunrise"] = true;
  ft["Dhuhr"] = true;
  ft["Asr"] = true;
  ft["Maghrib"] = true;
  ft["Isha"] = true;

  DynamicJsonDocument doc(768);
  if (!httpsGetJson(String(url), doc, filter)) return false;
  JsonObject tm = doc["data"]["timings"];
  if (tm.isNull()) return false;

  const char* keys[6] = { "Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha" };
  int16_t tmp[6];
  for (int i = 0; i < 6; i++) {
    String s = String((const char*)(tm[keys[i]] | ""));
    int h, m;
    if (!parseHHMM(s.substring(0, 5), h, m)) return false;
    tmp[i] = h * 60 + m;
  }
  memcpy(prayerMin, tmp, sizeof(prayerMin));
  prayerValid = true;
  prayerYmd = todayYmd();
  prayerEpoch = (uint32_t)time(nullptr);
  savePrayerToPrefs();
  Serial.println("[NET] Jadwal sholat OK");
  return true;
}

// 0..4 = waktu sholat berikutnya (indeks di antara 5 sholat); melewati Isya -> Subuh besok
int nextPrayerSlot(long *secsLeft) {
  long nowS = curHour * 3600L + curMin * 60L + curSec;
  for (int k = 0; k < 5; k++) {
    long t = prayerMin[PR_IDX[k]] * 60L;
    if (t > nowS) { if (secsLeft) *secsLeft = t - nowS; return k; }
  }
  if (secsLeft) *secsLeft = prayerMin[PR_IDX[0]] * 60L + 86400L - nowS;
  return 0;
}

// ── Harga emas per gram ─────────────────────────────────────
double goldRatio(double v) { return v * (1.0 + goldAdjPct / 100.0); }
uint32_t goldPrice() { return (uint32_t)(goldRatio(goldRawGram) + 0.5); }

void goldPushHist(double raw) {
  if (goldHistCount > 0 && millis() - goldHistLastMs < 600000UL) {
    goldHist[goldHistCount - 1] = raw;                 // masih di jendela 10 menit yang sama
    return;
  }
  if (goldHistCount == GOLD_HIST_N) {
    for (int i = 1; i < GOLD_HIST_N; i++) goldHist[i - 1] = goldHist[i];
    goldHistCount--;
  }
  goldHist[goldHistCount++] = raw;
  goldHistLastMs = millis();
}

bool fetchGoldPrice() {
  if (WiFi.status() != WL_CONNECTED) return false;
  Serial.println("[NET] Fetch harga emas");
  double xau = 0, fx = goldUsdIdr, gram = 0;

  {
    StaticJsonDocument<64> f;
    f["price"] = true;
    DynamicJsonDocument d(384);
    if (httpsGetJson("https://api.gold-api.com/price/XAU", d, f)) xau = d["price"] | 0.0;
  }
  if (xau > 0 && (fx < 1000 || dueAt(nextFxAt))) {
    StaticJsonDocument<64> f;
    f["rates"]["IDR"] = true;
    DynamicJsonDocument d(384);
    if (httpsGetJson("https://open.er-api.com/v6/latest/USD", d, f)) {
      double r = d["rates"]["IDR"] | 0.0;
      if (r > 1000) { fx = r; nextFxAt = millis() + 6UL * 3600UL * 1000UL; }
    }
  }
  if (xau > 0 && fx > 1000) {
    gram = xau / 31.1034768 * fx;
  } else {                                    // sumber cadangan: harga per ons langsung dalam Rupiah
    StaticJsonDocument<96> f;
    f["items"][0]["xauPrice"] = true;
    DynamicJsonDocument d(512);
    if (httpsGetJson("https://data-asg.goldprice.org/dbXRates/IDR", d, f)) {
      double oz = d["items"][0]["xauPrice"] | 0.0;
      if (oz > 0) gram = oz / 31.1034768;
    }
  }
  if (gram <= 0) return false;

  if (xau > 0) goldXauUsd = xau;
  if (fx > 1000) goldUsdIdr = fx;
  goldRawGram = gram;
  goldValid = true;
  int ymd = todayYmd();
  if (goldOpenYmd != ymd || goldOpenGram <= 0) { goldOpenGram = gram; goldOpenYmd = ymd; }
  struct tm lt;
  if (getLocalTime(&lt, 5)) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);
    { StrLock l; goldLast = String(buf); }
  }
  goldPushHist(gram);
  saveGoldToPrefs();
  Serial.print("[NET] Emas OK: Rp ");
  Serial.println(goldPrice());
  return true;
}

String fmtRupiah(uint32_t v) {
  String s = String(v);
  String out;
  int len = s.length();
  for (int i = 0; i < len; i++) {
    out += s[i];
    int rem = len - 1 - i;
    if (rem > 0 && rem % 3 == 0) out += '.';
  }
  return out;
}

// Dipanggil dari loop(): maksimal satu pengambilan data per panggilan, hanya saat layar tidak sibuk
// ── Akses String bersama yang aman antar task ──
String getWDesc()      { StrLock l; return wDesc; }
String getWLast()      { StrLock l; return wLastUpdate; }
String getGoldLast()   { StrLock l; return goldLast; }

void updateOnlineData() {
  if (deepSleepMode || deepSleepPending || wifiValidating || WiFi.status() != WL_CONNECTED) return;
  if (gameActive || toolActive || prayerAlertActive) return;
  if (buzzerSirenActive || buzzerPatternActive || tmBeeping) return;
  if (ESP.getFreeHeap() < 45000 || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 28000) {
    static unsigned long lastWarn = 0;
    if (millis() - lastWarn > 30000UL) { lastWarn = millis(); Serial.printf("[NET] Ditunda, memori kurang: heap=%u blok=%u\n", (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)); }
    return;
  }
  if (!timeIsSynced()) return;

  if (dueAt(nextWeatherAt)) {
    bool ok = fetchWeatherFromAPI();
    if (!ok) nextWeatherAt = millis() + 90000UL;
    return;
  }
  if (dueAt(nextFcAt)) {
    bool ok = fetchForecast();
    nextFcAt = millis() + (ok ? 3600000UL : 120000UL);
    return;
  }
  if (dueAt(nextPrayerAt) && (!prayerValid || prayerYmd != todayYmd())) {
    bool ok = fetchPrayerTimes();
    nextPrayerAt = millis() + (ok ? 60000UL : 120000UL);
    return;
  }
  if (dueAt(nextGoldAt)) {
    bool ok = fetchGoldPrice();
    nextGoldAt = millis() + (ok ? 900000UL : 90000UL);
    return;
  }
}


// Task latar belakang (core 0, core 0): semua operasi jaringan yang bisa memakan waktu
void startOnlineTask() {
  if (onlineTaskHandle) return;
  xTaskCreatePinnedToCore(onlineTask, "OnlineFetch", 16384, NULL, 1, &onlineTaskHandle, 0);
}

void onlineTask(void *pv) {
  for (;;) {
    if (systemMode == WIFI_MODE && !deepSleepMode) {
      if (reqWifiFailover) {                               // pindah ke WiFi tersimpan lain tanpa membekukan OLED
        reqWifiFailover = false;
        wifiBusy = true;
        bool ok = connectBestSavedWifi(false);
        wifiBusy = false;
        if (ok) { wifiDownSince = 0; wifiRetryCount = 0; }
      }
      if (WiFi.status() == WL_CONNECTED && !setupActive) {      // selama setup awal, TLS hanya dipakai oleh setup
        if (reqNtpSync) { reqNtpSync = false; syncNTP(); }
        updateOnlineData();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(700));
  }
}

// ═══════════════════════════════════════════════════════════
// PENGINGAT WAKTU SHOLAT
// ═══════════════════════════════════════════════════════════
static const uint16_t PR_MELODY[] = { 880, 988, 1175, 988, 1175, 1319, 1175, 0 };   // 0 = jeda
const int PR_MEL_N = sizeof(PR_MELODY) / sizeof(PR_MELODY[0]);
const uint16_t PR_NOTE_MS = 280;
const int PR_REPEAT = 3;

void startPrayerAlert(uint8_t slot) {
  prayerAlertIdx = slot;
  prayerAlertStart = millis();
  prayerLastFreq = 0;
  prayerAlertActive = true;
  Serial.print("[SHOLAT] Waktu ");
  Serial.println(prayerNames[PR_IDX[slot]]);
}

void dismissPrayerAlert() {
  if (!prayerAlertActive) return;
  prayerAlertActive = false;
  if (prayerLastFreq) { buzzerOff(); prayerLastFreq = 0; }
}

void updatePrayerAlert() {
  if (!prayerAlertActive) return;
  uint32_t el = millis() - prayerAlertStart;
  if (el > 60000) { dismissPrayerAlert(); return; }       // popup hilang sendiri setelah 1 menit
  bool canSound = !buzzerSirenActive && !buzzerPatternActive && !tmBeeping;
  uint32_t total = (uint32_t)PR_MEL_N * PR_NOTE_MS * PR_REPEAT;
  if (el < total && canSound) {
    uint16_t f = PR_MELODY[(el / PR_NOTE_MS) % PR_MEL_N];
    if (f) {
      if (f != prayerLastFreq) { buzzerOn(f); prayerLastFreq = f; }
    } else if (prayerLastFreq) {
      buzzerOff();
      prayerLastFreq = 0;
    }
  } else if (prayerLastFreq) {
    buzzerOff();
    prayerLastFreq = 0;
  }
}

void updatePrayerReminder() {
  if (!prayerReminderOn || !prayerValid || prayerAlertActive) return;
  if (!timeIsSynced()) return;
  if (time(nullptr) - (time_t)prayerEpoch > 7L * 86400L) return;   // jadwal terlalu lama tidak diperbarui
  int nowMin = curHour * 60 + curMin;
  int ymd = todayYmd();
  for (int k = 0; k < 5; k++) {
    if (!(prayerMask & (1 << k))) continue;
    int t = prayerMin[PR_IDX[k]];
    if (nowMin >= t && nowMin <= t + 1 && prayerLastYmd[k] != ymd) {   // jendela 2 menit (tahan jeda singkat)
      prayerLastYmd[k] = ymd;
      startPrayerAlert(k);
      break;
    }
  }
}

void stopWebServer() {
  if (webStarted) {
    server.stop();
    webStarted = false;
    Serial.println("[WEB] Server stopped");
  }
}

void startWebServer() {
  if (!webRoutesConfigured) {
    setupWebServer();
    webRoutesConfigured = true;
  }
  server.begin();
  webStarted = true;
  Serial.println("[WEB] Server started");
}

// Pengaturan agar WiFi lebih stabil: auto-reconnect, daya pancar maksimum, dan log alasan putus
static bool wifiEvtReg = false;
void wifiStabilize() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  if (!wifiEvtReg) {
    wifiEvtReg = true;
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
      Serial.printf("[WIFI] Putus, alasan=%d RSSI terakhir=%d\n", info.wifi_sta_disconnected.reason, WiFi.RSSI());
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  }
}

void connectWiFiFromPrefs(bool showBootMessages) {
  if (wifiListN == 0) {
    Serial.println("[WIFI] Belum ada WiFi tersimpan -> AP darurat");
    if (showBootMessages) { bootScreen("Belum ada WiFi", "Mode AP darurat"); delay(800); }
    startEmergencyAP(showBootMessages);
    wifiStarted = false;
    saveModeToPrefs(WIFI_MODE);
    systemMode = WIFI_MODE;
    return;
  }

  WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.setSleep(false);
  wifiStabilize();

  bool ok = connectBestSavedWifi(showBootMessages);

  if (ok) {
    Serial.print("[WIFI] Connected IP: ");
    Serial.println(WiFi.localIP());
    if (showBootMessages) bootScreen("WiFi Connected", WiFi.localIP().toString());
    if (apActive) stopEmergencyAP();
    syncNTP();
    updateTimeFromNTP();
    nextWeatherAt = 0;                // cuaca diambil task latar belakang
    startWebServer();
    wifiStarted = true;
    staInitDone = true;
  } else {
    Serial.println("[WIFI] Semua WiFi tersimpan gagal, AP darurat menyala");
    if (showBootMessages) bootScreen("WiFi Failed", "Mode AP darurat");
    wifiStarted = false;
    startEmergencyAP(showBootMessages);
  }

  saveModeToPrefs(WIFI_MODE);
  systemMode = WIFI_MODE;
}

void ensureWiFiMode() {
  if (WiFi.status() != WL_CONNECTED && millis() - lastWiFiRetryMs > wifiRetryIntervalMs) {
    lastWiFiRetryMs = millis();
    Serial.println("[WIFI] Rekoneksi background");
    WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());
  }
}

// ── Mulai validasi WiFi baru (WEB mode) ────────────────────────
void startWifiValidation(const String &newSSID, const String &newPASS, bool fromBT) {
  wifiOldSSID    = wifiSSID;
  wifiOldPASS    = wifiPASS;
  wifiTrySSID    = newSSID;
  wifiTryPASS    = newPASS;
  wifiValidateBT = fromBT;
  wifiValidating = true;
  wifiTryMs      = millis();
  wifiLastResult = "";
  if (!apActive) webStarted = false;        // saat AP darurat aktif, web tetap melayani halaman setup
  Serial.println("[WIFI] Mulai validasi: " + newSSID);
  WiFi.disconnect(true);
  delay(300);
  WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(newSSID.c_str(), newPASS.c_str());
}

// ── Masuk WiFi mode khusus untuk validasi BT (tidak blocking) ──
void enterWiFiModeForValidation(const String &newSSID, const String &newPASS) {
  Serial.println("[MODE] Enter WIFI for validation");
  // Stop BT
  if (btEnabled) { SerialBT.end(); btEnabled = false; }
  stopWebServer();
  // Stop WiFi lama
  if (WiFi.getMode() != WIFI_OFF) { WiFi.disconnect(true, true); delay(400); }
  // Simpan mode
  systemMode = WIFI_MODE;
  saveModeToPrefs(WIFI_MODE);
  saveSystemSettings();
  setDisplayOn(true);
  // Mulai validasi (non-blocking, loop yang handle hasil)
  startWifiValidation(newSSID, newPASS, true);
}

void enterWiFiMode() {
  Serial.println("[MODE] Enter WIFI_MODE");
  if (btEnabled) {
    SerialBT.end();
    btEnabled = false;
    Serial.println("[BT] SerialBT stopped");
  }

  delay(500);
  WiFi.mode(WIFI_STA);
  connectWiFiFromPrefs(true);
  systemMode = WIFI_MODE;
  saveModeToPrefs(WIFI_MODE);
  saveSystemSettings();
  setDisplayOn(true);
  applyBrightnessForTime();
  setupEvaluate();                       // layar setup awal bila belum selesai
}

void enterNavMode() {
  Serial.println("[MODE] Enter NAV_MODE");
  gameActive = false;
  toolActive = false;
  dismissPrayerAlert();
  stopWebServer();
  apActive = false;
  staInitDone = false;
  if (WiFi.getMode() != WIFI_OFF) {
    WiFi.disconnect(true, true);
    delay(500);
    WiFi.mode(WIFI_OFF);
    Serial.println("[WIFI] WiFi OFF");
  }

  if (!btEnabled) {
    SerialBT.begin(BT_DEVICE_NAME);
    btEnabled = true;
    Serial.println("[BT] SerialBT started as " BT_DEVICE_NAME);
  }

  clearNavState();
  systemMode = NAV_MODE;
  saveModeToPrefs(NAV_MODE);
  saveSystemSettings();
  bootScreen("MotoNav Aktif", "BT: " BT_DEVICE_NAME);
  setDisplayOn(true);
}

void switchToNavMode() {
  Serial.println("[MODE] switchToNavMode()");
  enterNavMode();
}

void switchToWiFiMode() {
  Serial.println("[MODE] switchToWiFiMode()");
  enterWiFiMode();
}

void handleWebStatus() {
  StaticJsonDocument<1024> doc;
  doc["time"] = formatTimeString();
  doc["date"] = formatDateString();
  doc["mode"] = (int)displayMode;
  doc["auto"] = autoMode;
  doc["sleep"] = oledSleepState;
  doc["temp"] = wTemp;
  doc["humidity"] = wHumid;
  doc["feels"] = wFeels;
  doc["wind"] = wWind;
  doc["desc"] = getWDesc();
  doc["last"] = getWLast();
  doc["ip"] = (WiFi.status() != WL_CONNECTED && apActive) ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  doc["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -999;
  doc["alarm_en"] = alarmEnabled;
  doc["alarm"] = getCurrentAlarmText();
  doc["autoBright"] = autoBrightness;
  doc["brightness"] = autoBrightness ? (oledSleepState ? 0 : ((curHour >= 6 && curHour < 18) ? dayBrightness : nightBrightness)) : manualBrightness;
  doc["systemMode"] = getCurrentModeText();
  doc["wifi"] = (WiFi.status() == WL_CONNECTED);
  doc["sleepEn"] = sleepEnabled;
  doc["eyeMin"] = eyeMinSec;
  doc["eyeMax"] = eyeMaxSec;
  char swBuf[16];
  snprintf(swBuf, sizeof(swBuf), "%02d:%02d-%02d:%02d", sleepStartHour, sleepStartMin, sleepEndHour, sleepEndMin);
  doc["sleepWindow"] = String(swBuf);

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleWebMode() {
  if (server.hasArg("val")) {
    displayMode = constrain(server.arg("val").toInt(), 0, PAGE_COUNT - 1);
    autoMode = false;
    saveSystemSettings();
    Serial.print("[WEB] displayMode=");
    Serial.println(displayMode);
  }
  server.send(200, "text/plain", "OK");
}

void handleWebAuto() {
  if (server.hasArg("val")) {
    autoMode = (server.arg("val") == "1");
    saveSystemSettings();
    Serial.print("[WEB] autoMode=");
    Serial.println(autoMode ? "1" : "0");
  }
  server.send(200, "text/plain", autoMode ? "1" : "0");
}

void handleWebWeather() {
  nextWeatherAt = 0;                 // diambil di task latar belakang; yang tampil di sini data terakhir
  bool ok = weatherValid;
  StaticJsonDocument<256> doc;
  doc["ok"] = ok;
  doc["temp"] = wTemp;
  doc["humidity"] = wHumid;
  doc["feels"] = wFeels;
  doc["wind"] = wWind;
  doc["desc"] = getWDesc();
  doc["last"] = getWLast();
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);   // selalu 200: data baru diambil task latar, "ok" menandakan sudah ada data
}

void handleWebBrightness() {
  if (server.hasArg("val")) {
    manualBrightness = constrain(server.arg("val").toInt(), 0, 255);
    autoBrightness = false;
    saveSystemSettings();
    applyOLEDContrast((uint8_t)manualBrightness);
    Serial.print("[WEB] manualBrightness=");
    Serial.println(manualBrightness);
  }
  server.send(200, "text/plain", "OK");
}

void handleWebAutoBright() {
  if (server.hasArg("val")) {
    autoBrightness = (server.arg("val") == "1");
    saveSystemSettings();
    Serial.print("[WEB] autoBrightness=");
    Serial.println(autoBrightness ? "1" : "0");
  }
  server.send(200, "text/plain", autoBrightness ? "1" : "0");
}

void handleWebAlarm() {
  if (server.hasArg("hour")) alarmHour = constrain(server.arg("hour").toInt(), 0, 23);
  if (server.hasArg("min")) alarmMin = constrain(server.arg("min").toInt(), 0, 59);
  if (server.hasArg("en")) alarmEnabled = (server.arg("en") == "1");
  saveSystemSettings();
  Serial.print("[ALARM] Set ");
  Serial.print(alarmHour);
  Serial.print(":");
  Serial.print(alarmMin);
  Serial.print(" en=");
  Serial.println(alarmEnabled ? "1" : "0");
  server.send(200, "text/plain", "OK");
}

void handleWebSleep() {
  if (server.hasArg("start")) {
    int h, m;
    if (parseHHMM(server.arg("start"), h, m)) {
      sleepStartHour = h;
      sleepStartMin = m;
    } else {
      sleepStartHour = constrain(server.arg("start").toInt(), 0, 23);
      sleepStartMin = 0;
    }
  }
  if (server.hasArg("end")) {
    int h, m;
    if (parseHHMM(server.arg("end"), h, m)) {
      sleepEndHour = h;
      sleepEndMin = m;
    } else {
      sleepEndHour = constrain(server.arg("end").toInt(), 0, 23);
      sleepEndMin = 0;
    }
  }
  if (server.hasArg("en"))
  sleepEnabled = server.arg("en").toInt();
  saveSystemSettings();
  Serial.print("[SLEEP] ");
  Serial.print(sleepStartHour);
  Serial.print(" - ");
  Serial.println(sleepEndHour);
  server.send(200, "text/plain", "OK");
}

void handleWebBeep() {
  beepTest();
  server.send(200, "text/plain", "Beep");
}

void handleWebSwitchNav() {
  server.send(200, "text/plain", "Switching to NAV_MODE");
  delay(100);
  switchToNavMode();
}

void handleWebEyes() {
  if (server.hasArg("min")) eyeMinSec = constrain(server.arg("min").toInt(), 1, 600);
  if (server.hasArg("max")) eyeMaxSec = constrain(server.arg("max").toInt(), 1, 600);
  if (eyeMaxSec < eyeMinSec) eyeMaxSec = eyeMinSec;
  saveSystemSettings();
  eyeIdleUntilMs = millis() + eyeRandomIdleMs();   // terapkan langsung ke hitungan mundur yang sedang jalan
  Serial.print("[EYES] jeda acak ");
  Serial.print(eyeMinSec);
  Serial.print("-");
  Serial.print(eyeMaxSec);
  Serial.println(" detik");
  server.send(200, "text/plain", "OK");
}

// ── Fitur baru: data prakiraan / sholat / emas / halaman otomatis / daftar WiFi ──
void handleWebFeat() {
  DynamicJsonDocument doc(4096);
  doc["autoMask"] = autoPageMask;
  doc["aqi"] = aqiUS;
  doc["pm25"] = pm25Now;
  int uv10 = fcUvToday10();
  doc["uv"] = (uv10 >= 0) ? (uv10 / 10.0f) : -1.0f;
  doc["fcValid"] = fcValid;
  doc["fcLast"] = fcLast;
  JsonArray fc = doc.createNestedArray("fc");
  if (fcValid) {
    for (int i = forecastStartIndex(); i < FC_DAYS; i++) {
      const FcDay &f = fcDay[i];
      JsonObject o = fc.createNestedObject();
      o["d"] = dayNames[f.wday % 7];
      char dm[8];
      snprintf(dm, sizeof(dm), "%02d/%02d", (int)f.dom, (int)f.mon);
      o["dm"] = String(dm);
      o["c"] = wxCodeText(f.code);
      o["hi"] = f.tmax;
      o["lo"] = f.tmin;
      o["p"] = f.pop;
      o["uv"] = f.uv10 / 10.0f;
    }
  }

  JsonObject pr = doc.createNestedObject("pr");
  pr["on"] = prayerReminderOn;
  pr["mask"] = prayerMask;
  pr["valid"] = prayerValid;
  pr["next"] = nextPrayerSlot(nullptr);
  JsonArray tt = pr.createNestedArray("t");
  for (int i = 0; i < 6; i++) {
    char b[8];
    snprintf(b, sizeof(b), "%02d:%02d", prayerMin[i] / 60, prayerMin[i] % 60);
    tt.add(String(b));
  }

  JsonObject g = doc.createNestedObject("gold");
  g["valid"] = goldValid;
  g["gram"] = goldPrice();
  g["open"] = (uint32_t)(goldRatio(goldOpenGram) + 0.5);
  g["xau"] = goldXauUsd;
  g["fx"] = goldUsdIdr;
  g["adj"] = goldAdjPct;
  g["last"] = getGoldLast();
  JsonArray h = g.createNestedArray("h");
  for (int i = 0; i < goldHistCount; i++) h.add((uint32_t)(goldRatio(goldHist[i]) + 0.5));

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleWebAutoMask() {
  if (server.hasArg("val")) {
    autoPageMask = (uint8_t)(server.arg("val").toInt() & ((1 << PAGE_COUNT) - 1));
    prefs.putUChar("autoMask", autoPageMask);
    Serial.print("[WEB] autoMask=");
    Serial.println(autoPageMask);
  }
  server.send(200, "text/plain", String(autoPageMask));
}

void handleWebPrayer() {
  if (server.hasArg("on")) prayerReminderOn = (server.arg("on") == "1");
  if (server.hasArg("mask")) prayerMask = (uint8_t)(server.arg("mask").toInt() & 0x1F);
  prefs.putBool("prOn", prayerReminderOn);
  prefs.putUChar("prMask", prayerMask);
  server.send(200, "text/plain", "OK");
}

void handleWebPrayerRefresh() {
  prayerYmd = 0;                     // paksa ambil ulang (dikerjakan task latar belakang)
  nextPrayerAt = 0;
  server.send(200, "text/plain", "OK");
}

void handleWebForecastRefresh() {
  nextFcAt = 0;                      // dikerjakan task latar belakang
  server.send(200, "text/plain", "OK");
}

void handleWebGold() {
  if (server.hasArg("adj")) {
    goldAdjPct = constrain(server.arg("adj").toFloat(), -50.0f, 50.0f);
    prefs.putFloat("goldAdj", goldAdjPct);
  }
  if (server.hasArg("refresh") && server.arg("refresh") == "1") nextGoldAt = 0;   // dikerjakan task latar belakang
  server.send(200, "text/plain", "OK");
}

void handleWebWifis() {
  DynamicJsonDocument doc(1024);
  JsonArray list = doc.createNestedArray("list");
  for (int i = 0; i < wifiListN; i++) list.add(wifiListSsid[i]);
  doc["cur"] = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : String("");
  doc["conn"] = (WiFi.status() == WL_CONNECTED);
  doc["ap"] = apActive;
  doc["apName"] = AP_SSID;
  doc["apPass"] = AP_PASS;
  doc["busy"] = wifiValidating;
  doc["res"] = wifiLastResult;
  doc["max"] = WIFI_MAX;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleWebWifiDel() {
  int i = server.arg("i").toInt();
  if (!server.hasArg("i") || i < 0 || i >= wifiListN) {
    server.send(400, "text/plain", "Indeks salah");
    return;
  }
  wifiListRemove(i);
  server.send(200, "text/plain", "OK");
}

void handleWebWifiUse() {
  int i = server.arg("i").toInt();
  if (!server.hasArg("i") || i < 0 || i >= wifiListN) {
    server.send(400, "text/plain", "Indeks salah");
    return;
  }
  if (wifiValidating) {
    server.send(503, "text/plain", "BUSY:Sedang memvalidasi WiFi lain, tunggu sebentar");
    return;
  }
  String ssid = wifiListSsid[i], pass = wifiListPass[i];
  server.send(200, "text/plain", "TRYING:" + ssid);
  delay(10);
  startWifiValidation(ssid, pass, false);
}


void handleWebDeepSleep() {
  if (!requestDeepSleep()) {
    server.send(409, "text/plain", "Deep sleep tidak bisa diaktifkan sekarang");
    return;
  }
  server.send(200, "text/plain", "Deep sleep aktif sebentar lagi");
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    String page = FPSTR(DASH_STUB);
    page.replace("%U1%", DASH_URL);
    page.replace("%U2%", DASH_URL2);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html; charset=utf-8", page);
  });
  server.on("/setupreset", HTTP_GET, []() {       // setup ulang: hapus animasi mata, restart ke layar setup awal
    prefs.putBool("setupDone", false);
    prefs.putBool("eyeSkip", false);
    eyeReady = false;
    if (eyeFile) eyeFile.close();
    LittleFS.remove(EYE_BIN_PATH);
    server.send(200, "text/plain", "OK");
    delay(400);
    ESP.restart();
  });
  server.on("/status", HTTP_GET, handleWebStatus);
  server.on("/mode", HTTP_GET, handleWebMode);
  server.on("/auto", HTTP_GET, handleWebAuto);
  server.on("/weather", HTTP_GET, handleWebWeather);
  server.on("/brightness", HTTP_GET, handleWebBrightness);
  server.on("/autobright", HTTP_GET, handleWebAutoBright);
  server.on("/alarm", HTTP_GET, handleWebAlarm);
  server.on("/sleep", HTTP_GET, handleWebSleep);
  server.on("/beep", HTTP_GET, handleWebBeep);
  server.on("/switchnav", HTTP_GET, handleWebSwitchNav);
  server.on("/deepsleep", HTTP_GET, handleWebDeepSleep);
  server.on("/eyes", HTTP_GET, handleWebEyes);
  server.on("/feat", HTTP_GET, handleWebFeat);
  server.on("/automask", HTTP_GET, handleWebAutoMask);
  server.on("/prayer", HTTP_GET, handleWebPrayer);
  server.on("/prayerrefresh", HTTP_GET, handleWebPrayerRefresh);
  server.on("/forecast", HTTP_GET, handleWebForecastRefresh);
  server.on("/gold", HTTP_GET, handleWebGold);
  server.on("/wifis", HTTP_GET, handleWebWifis);
  server.on("/wifidel", HTTP_GET, handleWebWifiDel);
  server.on("/wifiuse", HTTP_GET, handleWebWifiUse);
  // Ganti WiFi via web – pakai sistem validasi (try before save)
  server.on("/setwifi", HTTP_GET, []() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    ssid.trim(); pass.trim();
    if (ssid.length() == 0) {
      server.send(400, "text/plain", "SSID kosong");
      return;
    }
    if (wifiValidating) {
      server.send(503, "text/plain", "BUSY:Sedang memvalidasi WiFi lain, tunggu sebentar");
      return;
    }
    // Balas dulu sebelum disconnect (supaya client terima response)
    server.send(200, "text/plain", "TRYING:" + ssid);
    delay(10);
    // Mulai validasi non-blocking
    startWifiValidation(ssid, pass, false);
  });

  server.onNotFound([]() {
    server.send(404, "text/plain", "Not Found");
  });
}

void drawCenteredText(const String &txt, int y, uint8_t size) {
  display.setTextSize(size);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - (int)w) / 2, y);
  display.print(txt);
}

void drawScrollingText(const String &txt, int x, int y, uint8_t size, int maxWidth) {
  display.setTextSize(size);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
  if ((int)w <= maxWidth) {
    display.setCursor(x, y);
    display.print(txt);
    return;
  }

  String padded = txt + "   ";
  int chars = padded.length();
  int windowChars = max(1, maxWidth / (6 * size));
  if (scrollIndex >= chars) scrollIndex = 0;

  String visible;
  for (int i = 0; i < windowChars; i++) {
    visible += padded[(scrollIndex + i) % chars];
  }
  display.setCursor(x, y);
  display.print(visible);
}

// ═══════════════════════════════════════════════════════════
// SMARTDISPLAY
// ═══════════════════════════════════════════════════════════
void drawSmartDisplay() {
  switch (displayMode) {
    case 0: drawClockScreen(); break;
    case 1: drawWeatherScreen(); break;
    case 2: drawRobotEyesScreen(); break;
    case 3: drawInfoScreen(); break;
    case 4: drawForecastScreen(); break;
    case 5: drawPrayerScreen(); break;
    case 6: drawGoldScreen(); break;
    default: drawClockScreen(); break;
  }
}

void drawClockScreen() {
  static unsigned long lastDraw = 0;
  if (millis() - lastDraw < 250) return;
  lastDraw = millis();

  display.clearDisplay();

  // ── Jam besar (tengah kiri) ──────────────────
  char timeBuf[6];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", curHour, curMin);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(3);
  int tw = strlen(timeBuf) * 18;
  display.setCursor((128 - tw) / 2, 4);
  display.print(timeBuf);

  // ── Detik kecil kanan ────────────────────────
  display.setTextSize(1);
  char secBuf[4];
  snprintf(secBuf, sizeof(secBuf), "%02d", curSec);
  display.setCursor(107, 28);
  display.print(secBuf);

  // ── Garis pemisah ───────────────────────────
  display.drawLine(0, 38, 128, 38, SSD1306_WHITE);

  // ── Tanggal ─────────────────────────────────
  display.setTextSize(1);
  String dateStr = formatDateString();
  drawCenteredText(dateStr, 42, 1);

  // ── WiFi signal bars (pixel, bukan Unicode) ──
  if (WiFi.status() == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    int bars = (rssi > -55) ? 4 : (rssi > -67) ? 3 : (rssi > -78) ? 2 : 1;
    for (int b = 0; b < 4; b++) {
      int bh = 3 + b * 3;
      int bx = 2 + b * 5;
      int by = 61 - bh;
      if (b < bars) display.fillRect(bx, by, 4, bh, SSD1306_WHITE);
      else          display.drawRect(bx, by, 4, bh, SSD1306_WHITE);
    }
  } else if (apActive) {                       // WiFi belum tersambung, hotspot darurat menyala
    display.setTextSize(1);
    display.setCursor(2, 54);
    display.print("AP");
  }

  // ── Suhu pojok kanan bawah ───────────────────
  display.setTextSize(1);
  display.setCursor(94, 54);
  if (weatherValid) {
    display.print(String(wTemp, 0));
    display.print("C");
  } else {
    display.print("--C");
  }

  display.display();
}

void drawWeatherScreen() {
  static unsigned long lastDraw = 0;
  if (millis() - lastDraw < 500) return;
  lastDraw = millis();

  display.clearDisplay();

  // ── Header bar ──────────────────────────────
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(3, 2);
  display.print("CUACA - ");
  display.print(cityDisplay);
  display.setTextColor(SSD1306_WHITE);

  // ── Suhu besar (kiri) ───────────────────────
  display.setTextSize(3);
  char tmpStr[5];
  snprintf(tmpStr, sizeof(tmpStr), "%.0f", wTemp);
  display.setCursor(4, 16);
  display.print(tmpStr);
  // Gambar simbol derajat manual (lingkaran kecil) lalu C
  int degX = 4 + strlen(tmpStr) * 18;
  display.drawCircle(degX + 3, 18, 3, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(degX + 9, 16);
  display.print("C");

  // ── Info kolom kanan ────────────────────────
  display.setTextSize(1);
  char humStr[10];  snprintf(humStr,  sizeof(humStr),  "H:%.0f%%", wHumid);
  char feelStr[10]; snprintf(feelStr, sizeof(feelStr), "R:%.0fC",  wFeels);
  char windStr[10]; snprintf(windStr, sizeof(windStr), "W:%.0fkm", wWind);
  display.setCursor(82, 16);  display.print(humStr);
  display.setCursor(82, 26);  display.print(feelStr);
  display.setCursor(82, 36);  display.print(windStr);

  // ── Garis pemisah ───────────────────────────
  display.drawLine(0, 41, 128, 41, SSD1306_WHITE);

  // ── Deskripsi cuaca ─────────────────────────
  display.setTextSize(1);
  String desc = getWDesc();
  if (desc.length() > 20) desc = desc.substring(0, 20);
  display.setCursor(0, 44);
  display.print(desc);

  // ── UV & kualitas udara (kiri bawah) ────────
  char ua[20];
  int uv10 = fcUvToday10();
  if (uv10 >= 0 && aqiUS >= 0)  snprintf(ua, sizeof(ua), "UV%d AQI%d", (uv10 + 5) / 10, (int)aqiUS);
  else if (uv10 >= 0)           snprintf(ua, sizeof(ua), "UV%d AQI--", (uv10 + 5) / 10);
  else if (aqiUS >= 0)          snprintf(ua, sizeof(ua), "UV-- AQI%d", (int)aqiUS);
  else                          snprintf(ua, sizeof(ua), "UV-- AQI--");
  display.setCursor(0, 54);
  display.print(ua);

  // ── Update time pojok kanan bawah ───────────
  display.setCursor(80, 54);
  display.print("Upd ");
  display.print(getWLast());

  display.display();
}

// ═══════════════════════════════════════════════════════════
// HALAMAN BARU: PRAKIRAAN, SHOLAT, HARGA EMAS (+ popup waktu sholat)
// ═══════════════════════════════════════════════════════════
void drawForecastScreen() {
  static unsigned long lastDraw = 0;
  if (millis() - lastDraw < 500) return;
  lastDraw = millis();

  display.clearDisplay();
  display.setTextSize(1);
  display.fillRect(0, 0, 128, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print("PRAKIRAAN");
  // UV & AQI hari ini di kanan header
  char hdr[20];
  int uv10 = fcUvToday10();
  if (uv10 >= 0 && aqiUS >= 0)  snprintf(hdr, sizeof(hdr), "UV%d AQI%d", (uv10 + 5) / 10, (int)aqiUS);
  else if (uv10 >= 0)           snprintf(hdr, sizeof(hdr), "UV%d AQI--", (uv10 + 5) / 10);
  else if (aqiUS >= 0)          snprintf(hdr, sizeof(hdr), "UV-- AQI%d", (int)aqiUS);
  else                          snprintf(hdr, sizeof(hdr), "UV-- AQI--");
  display.setCursor(126 - (int)strlen(hdr) * 6, 1);
  display.print(hdr);

  display.setTextColor(SSD1306_WHITE);
  if (!fcValid) {
    drawCenteredText("Belum ada data", 24, 1);
    drawCenteredText("Menunggu WiFi...", 38, 1);
    display.display();
    return;
  }

  int start = forecastStartIndex();
  int today = curMon * 100 + curDay;
  int row = 0;
  for (int i = start; i < FC_DAYS && row < 5; i++, row++) {
    const FcDay &f = fcDay[i];
    int y = 12 + row * 10;
    bool isToday = ((int)f.mon * 100 + f.dom == today);
    if (isToday) {
      display.fillRect(0, y - 1, 128, 10, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    char dn[4];
    snprintf(dn, sizeof(dn), "%.3s", dayNames[f.wday % 7]);
    display.setCursor(0, y);
    display.print(dn);
    display.setCursor(22, y);
    display.print(wxCodeText(f.code));
    char tb[10];
    snprintf(tb, sizeof(tb), "%d/%d", (int)f.tmin, (int)f.tmax);
    display.setCursor(68, y);
    display.print(tb);
    char pb[8];
    snprintf(pb, sizeof(pb), "%d%%", (int)f.pop);
    display.setCursor(126 - (int)strlen(pb) * 6, y);
    display.print(pb);
  }
  display.display();
}

void drawPrayerScreen() {
  static unsigned long lastDraw = 0;
  if (millis() - lastDraw < 500) return;
  lastDraw = millis();

  display.clearDisplay();
  display.setTextSize(1);
  display.fillRect(0, 0, 128, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print("SHOLAT");

  if (!prayerValid) {
    display.setTextColor(SSD1306_WHITE);
    drawCenteredText("Belum ada jadwal", 24, 1);
    drawCenteredText("Menunggu WiFi...", 38, 1);
    display.display();
    return;
  }

  long left = 0;
  int nk = nextPrayerSlot(&left);
  char cd[12];
  snprintf(cd, sizeof(cd), "-%02ld:%02ld:%02ld", left / 3600, (left / 60) % 60, left % 60);
  display.setCursor(126 - (int)strlen(cd) * 6, 1);
  display.print(cd);

  for (int r = 0; r < 6; r++) {
    int y = 12 + r * 8;
    bool isNext = (PR_IDX[nk] == r);
    if (isNext) {
      display.fillRect(0, y - 1, 128, 9, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(4, y);
    display.print(prayerNames[r]);
    if (r != 1 && prayerReminderOn) {                   // tanda * = pengingat aktif
      int k = (r == 0) ? 0 : (r - 1);
      if (prayerMask & (1 << k)) { display.setCursor(70, y); display.print("*"); }
    }
    char tb[8];
    snprintf(tb, sizeof(tb), "%02d:%02d", prayerMin[r] / 60, prayerMin[r] % 60);
    display.setCursor(92, y);
    display.print(tb);
  }
  display.display();
}

void drawGoldScreen() {
  static unsigned long lastDraw = 0;
  if (millis() - lastDraw < 500) return;
  lastDraw = millis();

  display.clearDisplay();
  display.setTextSize(1);
  display.fillRect(0, 0, 128, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print("EMAS 1 GRAM");
  display.setCursor(104, 1);
  display.print("IDR");
  display.setTextColor(SSD1306_WHITE);

  if (!goldValid) {
    drawCenteredText("Belum ada data", 24, 1);
    drawCenteredText("Menunggu WiFi...", 38, 1);
    display.display();
    return;
  }

  String p = fmtRupiah(goldPrice());
  uint8_t sz = (p.length() * 12 <= 126) ? 2 : 1;
  drawCenteredText(p, 14, sz);

  // Baris info: waktu update (kiri) + perubahan dari harga pembuka hari ini (kanan)
  display.setTextSize(1);
  display.setCursor(0, 33);
  display.print("Upd ");
  display.print(getGoldLast());
  if (goldOpenGram > 0) {
    double pct = (goldRawGram - goldOpenGram) / goldOpenGram * 100.0;
    char b[12];
    snprintf(b, sizeof(b), "%c%.2f%%", pct >= 0 ? '+' : '-', fabs(pct));
    int tx = 128 - (int)strlen(b) * 6;
    display.setCursor(tx, 33);
    display.print(b);
    int ax = tx - 9;
    if (pct > 0.004)       display.fillTriangle(ax, 40, ax + 6, 40, ax + 3, 34, SSD1306_WHITE);
    else if (pct < -0.004) display.fillTriangle(ax, 34, ax + 6, 34, ax + 3, 40, SSD1306_WHITE);
  }

  // Grafik mini riwayat harga
  const int gx = 2, gy = 45, gw = 124, gh = 18;
  if (goldHistCount >= 2) {
    double mn = goldHist[0], mx = goldHist[0];
    for (int i = 1; i < goldHistCount; i++) { mn = min(mn, goldHist[i]); mx = max(mx, goldHist[i]); }
    double span = mx - mn;
    int px = gx, py = gy + gh / 2;
    for (int i = 0; i < goldHistCount; i++) {
      int x = gx + (int)((double)i * (gw - 1) / (goldHistCount - 1));
      int y = (span < 1.0) ? (gy + gh / 2) : (gy + gh - 1 - (int)((goldHist[i] - mn) / span * (gh - 1)));
      if (i > 0) display.drawLine(px, py, x, y, SSD1306_WHITE);
      px = x; py = y;
    }
  } else {
    drawCenteredText("Mengumpulkan grafik...", 50, 1);
  }
  display.display();
}

void drawPrayerAlert() {
  static unsigned long lastDraw = 0;
  if (millis() - lastDraw < 200) return;
  lastDraw = millis();

  display.clearDisplay();
  display.setTextSize(1);
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  drawCenteredText("WAKTU SHOLAT", 2, 1);
  display.setTextColor(SSD1306_WHITE);
  if ((millis() / 500) % 2 == 0) display.drawRect(0, 14, 128, 50, SSD1306_WHITE);
  drawCenteredText(prayerNames[PR_IDX[prayerAlertIdx]], 21, 2);
  char tb[14];
  int t = prayerMin[PR_IDX[prayerAlertIdx]];
  snprintf(tb, sizeof(tb), "%02d:%02d WIB", t / 60, t % 60);
  drawCenteredText(tb, 41, 1);
  drawCenteredText("Klik: tutup", 52, 1);
  display.display();
}

// ═══════════════════════════════════════════════════════════
// ANIMASI MATA (GIF) - dibaca dari LittleFS
// Data animasi ada di /eyegifs.bin (dibuat dari EyeGifs.h oleh make_eyegifs_bin.py, diunduh dari GitHub
// saat setup awal) sehingga TIDAK memakan flash program. Dibaca per frame (buffer hanya ~0,7 KB).
// Sistem: mata dasar (blank) berulang beberapa detik -> satu animasi GIF acak
// diputar sekali sampai selesai -> kembali ke mata dasar. Begitu seterusnya.
// Bila file belum ada / rusak: mata sederhana (cadangan) ditampilkan.
// ═══════════════════════════════════════════════════════════
bool eyeLoadTable() {
  eyeReady = false;
  if (eyeFile) eyeFile.close();
  if (!eyeFsOk || !LittleFS.exists(EYE_BIN_PATH)) { eyeDlErr = "Berkas hilang"; return false; }
  eyeFile = LittleFS.open(EYE_BIN_PATH, "r");
  if (!eyeFile) return false;
  uint8_t h[16];
  if (eyeFile.read(h, 16) != 16 || memcmp(h, "EYG1", 4) != 0) { eyeDlErr = "Header salah"; eyeFile.close(); return false; }
  int n = h[4] | (h[5] << 8);
  int blank = h[6] | (h[7] << 8);
  int maxf = h[8] | (h[9] << 8);
  uint32_t total = (uint32_t)h[12] | ((uint32_t)h[13] << 8) | ((uint32_t)h[14] << 16) | ((uint32_t)h[15] << 24);
  if (n < 2 || n > 200 || blank >= n) { eyeDlErr = "Jumlah anim " + String(n); eyeFile.close(); return false; }
  if (maxf > EYE_FRAME_MAX) { eyeDlErr = "Frame maks " + String(maxf); eyeFile.close(); return false; }
  if (total != (uint32_t)eyeFile.size()) { eyeDlErr = "Ukuran tak cocok"; eyeFile.close(); return false; }
  EyeEnt *t = (EyeEnt*)realloc(eyeTab, n * sizeof(EyeEnt));
  if (!t) { eyeDlErr = "Memori tabel"; eyeFile.close(); return false; }
  eyeTab = t;
  uint8_t e[28];
  for (int i = 0; i < n; i++) {
    if (eyeFile.read(e, 28) != 28) { eyeFile.close(); return false; }
    memcpy(eyeTab[i].name, e, 16);
    eyeTab[i].name[15] = 0;
    eyeTab[i].frames = (uint16_t)(e[16] | (e[17] << 8));
    eyeTab[i].off  = (uint32_t)e[20] | ((uint32_t)e[21] << 8) | ((uint32_t)e[22] << 16) | ((uint32_t)e[23] << 24);
    eyeTab[i].size = (uint32_t)e[24] | ((uint32_t)e[25] << 8) | ((uint32_t)e[26] << 16) | ((uint32_t)e[27] << 24);
    if (eyeTab[i].frames == 0 || eyeTab[i].off + eyeTab[i].size > total) { eyeDlErr = "Tabel rusak #" + String(i); eyeFile.close(); return false; }
  }
  eyeCount = n;
  eyeBlank = blank;
  eyeCurIdx = blank;
  eyeFrames = 0;
  eyeFrameIdx = 0;
  eyeStarted = false;
  eyeLastExpr = -1;
  eyeReady = true;
  Serial.printf("[EYES] %d animasi siap dari LittleFS\n", n);
  return true;
}

static void eyeStart(int idx) {
  eyeCurIdx = idx;
  eyeFrames = eyeTab[idx].frames;
  eyeFrameIdx = 0;
  eyeFile.seek(eyeTab[idx].off);
  memset(eyeBuf, 0, sizeof(eyeBuf));   // frame pertama disimpan relatif terhadap layar kosong
}

// Baca + dekode 1 frame (delta XOR + RLE) ke eyeBuf. delayMs = lama tampil frame (ms). false = gagal baca.
static bool eyeDecodeNextFrame(uint16_t &delayMs) {
  uint8_t hd[3];
  if (eyeFile.read(hd, 3) != 3) return false;
  delayMs = (uint16_t)hd[0] * 10;
  uint16_t len = (uint16_t)hd[1] | ((uint16_t)hd[2] << 8);
  if (len > EYE_FRAME_MAX || eyeFile.read(eyeRle, len) != len) return false;
  const uint8_t* p = eyeRle;
  const uint8_t* end = p + len;
  uint8_t* o = eyeBuf;
  uint8_t* oEnd = eyeBuf + sizeof(eyeBuf);
  while (p < end && o < oEnd) {
    uint8_t c = *p++;
    if (c & 0x80) {                    // n byte tidak berubah
      o += (c & 0x7F) + 1;
    } else {                           // n byte literal (di-XOR ke frame sebelumnya)
      uint8_t n = c + 1;
      while (n-- && o < oEnd) *o++ ^= *p++;
    }
  }
  eyeFrameIdx++;
  return true;
}

// Jeda acak (ms) sebelum animasi mata berikutnya, sesuai pengaturan web (min..max detik)
unsigned long eyeRandomIdleMs() {
  long lo = (long)eyeMinSec * 1000L;
  long hi = (long)eyeMaxSec * 1000L;
  if (hi <= lo) return (unsigned long)lo;
  return (unsigned long)random(lo, hi + 1);
}

// Mata sederhana (cadangan) saat eyegifs.bin belum ada
static void drawFallbackEyes() {
  static unsigned long last = 0;
  unsigned long now = millis();
  if (now - last < 100) return;
  last = now;
  bool blink = (now % 4200) < 160;
  int h = blink ? 4 : 30;
  int dx = (int)(sin(now / 900.0) * 4);
  display.clearDisplay();
  display.fillRoundRect(22, 32 - h / 2, 30, h, 8, SSD1306_WHITE);
  display.fillRoundRect(76, 32 - h / 2, 30, h, 8, SSD1306_WHITE);
  if (!blink) {
    display.fillCircle(37 + dx, 32, 5, SSD1306_BLACK);
    display.fillCircle(91 + dx, 32, 5, SSD1306_BLACK);
  }
  display.display();
}

void drawRobotEyesScreen() {
  if (!eyeReady) { drawFallbackEyes(); return; }
  unsigned long now = millis();

  if (!eyeStarted) {                   // pertama kali: mulai dari mata dasar
    eyeStarted = true;
    eyeStart(eyeBlank);
    eyeIdleUntilMs = now + eyeRandomIdleMs();
    eyeNextMs = 0;
  }

  if (now < eyeNextMs) return;         // belum waktunya frame berikutnya

  if (eyeFrameIdx >= eyeFrames) {      // animasi yang sedang jalan sudah habis
    if (eyeCurIdx == eyeBlank) {
      if ((long)(now - eyeIdleUntilMs) >= 0) {
        // waktunya ekspresi acak (bukan blank, tidak sama dengan yang barusan)
        int pick;
        do { pick = random(0, eyeCount); }
        while (pick == eyeBlank || (pick == eyeLastExpr && eyeCount > 2));
        eyeLastExpr = pick;
        eyeStart(pick);
        Serial.print("[EYES] ");
        Serial.println(eyeTab[pick].name);
      } else {
        eyeStart(eyeBlank);            // ulangi mata dasar
      }
    } else {
      eyeStart(eyeBlank);              // balik ke mata dasar
      eyeIdleUntilMs = now + eyeRandomIdleMs();
    }
  }

  uint16_t delayMs = 0;
  if (!eyeDecodeNextFrame(delayMs)) {  // file rusak / terputus -> pakai mata cadangan
    Serial.println("[EYES] Gagal membaca eyegifs.bin -> mata cadangan");
    eyeReady = false;
    return;
  }
  static unsigned long eyeLastShowMs = 0;
  if (delayMs >= 100 || now - eyeLastShowMs >= 100) {   // batas tampil sekitar 10 fps, frame cepat (60 ms) dilewati bergantian
    memcpy(display.getBuffer(), eyeBuf, sizeof(eyeBuf));
    display.display();
    eyeLastShowMs = now;
  }
  eyeNextMs = now + delayMs;
}

// ═══════════════════════════════════════════════════════════
// SETUP AWAL (layar beranimasi di OLED)
// Dijalankan saat pertama kali (atau setelah "Setup ulang" di dashboard):
//   0) Hubungkan WiFi (AP darurat menyala otomatis bila belum ada WiFi tersimpan)
//   1) Unduh animasi mata (eyegifs.bin) dari GitHub -> LittleFS
//   2) Cek dashboard web di GitHub dapat dijangkau
//   3) Selesai -> mode normal (mata + dashboard siap dipakai)
// Tombol: tahan = lewati (saat WiFi / saat unduh gagal), klik = coba lagi (saat unduh gagal).
// ═══════════════════════════════════════════════════════════
void setupEvaluate() {
  bool done = prefs.getBool("setupDone", false);
  bool skip = prefs.getBool("eyeSkip", false);
  setupActive = (systemMode == WIFI_MODE) && !(done && (eyeReady || skip));
  setupStep = 0;
  eyeDlTry = 0;
  eyeDlFailed = false;
  setupStepMs = millis();
  if (setupActive) {
    Serial.println("[SETUP] Setup awal dimulai");
    setDisplayOn(true);
  }
}

static void setupHeader(uint8_t step) {
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(4, 2);
  display.print("SETUP AWAL");
  for (int i = 0; i < 4; i++) {
    int x = 92 + i * 9;
    if (i < step) display.fillCircle(x, 6, 2, SSD1306_BLACK);
    else if (i == step) {
      if ((millis() / 300) % 2) display.fillCircle(x, 6, 2, SSD1306_BLACK);
      else display.drawCircle(x, 6, 2, SSD1306_BLACK);
    } else display.drawPixel(x, 6, SSD1306_BLACK);
  }
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
}

static void setupDrawWifi() {
  unsigned long now = millis();
  display.clearDisplay();
  setupHeader(0);
  int cx = 20, cy = 38;                          // ikon sinyal: busur memancar dari titik
  int arcs = (now / 350) % 4;
  display.fillCircle(cx, cy, 2, SSD1306_WHITE);
  for (int i = 1; i <= 3; i++) {
    if (i <= arcs) {
      int r = 5 + i * 4;
      display.drawCircleHelper(cx, cy, r, 0x1 | 0x2, SSD1306_WHITE);
      display.drawCircleHelper(cx, cy, r + 1, 0x1 | 0x2, SSD1306_WHITE);
    }
  }
  if (apActive) {
    display.setCursor(42, 16); display.print("Hubungkan WiFi");
    display.setCursor(42, 27); display.print("lewat HP / PC");
    int pg = (now / 2400) % 4;
    String a, b;
    if (pg == 0)      { a = "Pilih WiFi:";        b = AP_SSID; }
    else if (pg == 1) { a = "Sandi:";             b = AP_PASS; }
    else if (pg == 2) { a = "Lalu buka browser:"; b = WiFi.softAPIP().toString(); }
    else              { a = "Tahan tombol 1 dtk"; b = "= lewati setup"; }
    display.setCursor(4, 44); display.print(a);
    display.setCursor(4, 55); display.print(b);
  } else {
    display.setCursor(42, 16); display.print("Menyambung");
    display.setCursor(42, 27); display.print("WiFi tersimpan");
    String s = wifiSSID;
    if (s.length() > 21) s = s.substring(0, 21);
    display.setCursor(4, 44); display.print(s);
    String dots = "";
    for (int i = 0; i < (int)((now / 400) % 4); i++) dots += ".";
    display.setCursor(4, 55); display.print(dots);
  }
  display.display();
}

static void setupDrawEyes() {
  unsigned long now = millis();
  display.clearDisplay();
  setupHeader(1);
  int ex1 = 8, ex2 = 28, ey = 28;                // ikon mata berkedip, pupil bergerak
  bool blink = (now % 2600) < 140;
  int h = blink ? 3 : 14;
  display.fillRoundRect(ex1, ey - h / 2, 15, h, 4, SSD1306_WHITE);
  display.fillRoundRect(ex2, ey - h / 2, 15, h, 4, SSD1306_WHITE);
  if (!blink) {
    int dx = (int)(sin(now / 500.0) * 2);
    display.fillRect(ex1 + 6 + dx, ey - 2, 4, 4, SSD1306_BLACK);
    display.fillRect(ex2 + 6 + dx, ey - 2, 4, 4, SSD1306_BLACK);
  }
  display.setCursor(50, 16); display.print("Mengunduh");
  display.setCursor(50, 27); display.print("animasi mata");
  int pct = eyeDlTotal ? (int)(((uint64_t)eyeDlGot * 100) / eyeDlTotal) : 0;
  display.drawRoundRect(4, 40, 120, 10, 4, SSD1306_WHITE);
  int w = (116 * pct) / 100;
  if (w > 0) {
    display.fillRoundRect(6, 42, w, 6, 2, SSD1306_WHITE);
    for (int x = 6; x < 6 + w; x++) if (((x + now / 50) % 8) < 2) display.drawFastVLine(x, 42, 6, SSD1306_BLACK);   // garis bergerak
  }
  if (eyeDlTotal) {
    char b[32];
    snprintf(b, sizeof(b), "%u/%u KB  %d%%", (unsigned)(eyeDlGot / 1024), (unsigned)(eyeDlTotal / 1024), pct);
    drawCenteredText(b, 54, 1);
  } else {
    String st = eyeDlErr.length() ? ("Ulang: " + eyeDlErr) : String("Menghubungi GitHub...");
    if (st.length() > 21) st = st.substring(0, 21);
    drawCenteredText(st, 54, 1);
  }
  display.display();
}

static void setupDrawFail() {
  display.clearDisplay();
  setupHeader(1);
  drawCenteredText("Gagal mengunduh", 17, 1);
  String e = eyeDlErr;
  if (e.length() > 21) e = e.substring(0, 21);
  drawCenteredText(e, 28, 1);
  drawCenteredText("Klik: coba lagi", 43, 1);
  drawCenteredText("Tahan: lewati", 54, 1);
  display.display();
}

static void setupDrawWebCheck() {
  unsigned long now = millis();
  display.clearDisplay();
  setupHeader(2);
  int cx = 64, cy = 31, head = (now / 100) % 8;  // spinner titik berputar dengan ekor
  for (int i = 0; i < 8; i++) {
    float a = i * 0.7853982f;
    int d = (i - head + 8) % 8;
    int r = d == 0 ? 3 : (d < 3 ? 2 : 1);
    display.fillCircle(cx + (int)(cosf(a) * 14), cy + (int)(sinf(a) * 14), r, SSD1306_WHITE);
  }
  drawCenteredText("Memeriksa dashboard", 54, 1);
  display.display();
}

static void setupDrawDone(unsigned long el) {
  unsigned long now = millis();
  display.clearDisplay();
  setupHeader(4);
  float p = el / 700.0f;
  if (p > 1.0f) p = 1.0f;
  float rr = p * 1.5f;
  if (rr > 1.0f) rr = 1.0f;
  display.drawCircle(64, 32, (int)(17 * rr), SSD1306_WHITE);
  // centang digambar bertahap: A -> B -> C
  int ax = 55, ay = 32, bx = 61, by = 38, cx = 74, cy = 25;
  float s1 = p < 0.4f ? p / 0.4f : 1.0f;
  display.drawLine(ax, ay, ax + (int)((bx - ax) * s1), ay + (int)((by - ay) * s1), SSD1306_WHITE);
  display.drawLine(ax, ay + 1, ax + (int)((bx - ax) * s1), ay + 1 + (int)((by - ay) * s1), SSD1306_WHITE);
  if (p > 0.4f) {
    float s2 = (p - 0.4f) / 0.6f;
    display.drawLine(bx, by, bx + (int)((cx - bx) * s2), by + (int)((cy - by) * s2), SSD1306_WHITE);
    display.drawLine(bx, by + 1, bx + (int)((cx - bx) * s2), by + 1 + (int)((cy - by) * s2), SSD1306_WHITE);
  }
  if (el > 700) {                                // bintang berkelip
    static const int8_t sx[6] = { 20, 36, 100, 112, 28, 104 };
    static const int8_t sy[6] = { 20, 42, 24, 44, 32, 34 };
    for (int i = 0; i < 6; i++) {
      if (((now / 160) + i * 3) % 7 < 3) {
        display.drawPixel(sx[i], sy[i], SSD1306_WHITE);
        display.drawPixel(sx[i] - 2, sy[i], SSD1306_WHITE);
        display.drawPixel(sx[i] + 2, sy[i], SSD1306_WHITE);
        display.drawPixel(sx[i], sy[i] - 2, SSD1306_WHITE);
        display.drawPixel(sx[i], sy[i] + 2, SSD1306_WHITE);
      }
    }
  }
  int pg = (el / 1500) % 3;
  String t = pg == 0 ? String("Selesai! Siap dipakai") : (pg == 1 ? WiFi.localIP().toString() : String(dashOk ? "Web siap" : "Web butuh internet HP"));
  drawCenteredText(t, 54, 1);
  display.display();
}

// Unduh eyegifs.bin ke LittleFS (lewat berkas sementara, lalu diganti setelah divalidasi).
static bool eyeDownload() {
  eyeDlErr = "";
  eyeDlGot = 0;
  eyeDlTotal = 0;
  if (!eyeFsOk) { eyeDlErr = "LittleFS gagal"; return false; }
  {
    uint32_t lb = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), fh = ESP.getFreeHeap();
    Serial.printf("[SETUP] heap free=%u largest=%u\n", (unsigned)fh, (unsigned)lb);
    if (lb < 22000 || fh < 40000) { eyeDlErr = "Memori " + String(lb / 1024) + "K"; return false; }
  }
  setupDrawEyes();
  WiFiClientSecure cl;
  cl.setInsecure();
  cl.setTimeout(12);
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(12000);
  http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(cl, EYE_BIN_URL)) { eyeDlErr = "URL salah"; return false; }
  int code = http.GET();
  if (code != 200) { eyeDlErr = code > 0 ? ("HTTP " + String(code)) : String("Koneksi gagal"); http.end(); return false; }
  int total = http.getSize();
  if (total < 4096 || total > 700000) { eyeDlErr = "Ukuran file aneh"; http.end(); return false; }
  LittleFS.remove(EYE_TMP_PATH);
  if ((long)(LittleFS.totalBytes() - LittleFS.usedBytes()) < (long)total + 16384) LittleFS.remove(EYE_BIN_PATH);   // ruang kurang: buang yang lama
  if ((long)(LittleFS.totalBytes() - LittleFS.usedBytes()) < (long)total + 16384) { eyeDlErr = "Ruang flash kurang"; http.end(); return false; }
  File f = LittleFS.open(EYE_TMP_PATH, "w");
  if (!f) { eyeDlErr = "Tulis gagal"; http.end(); return false; }
  uint8_t *buf = (uint8_t*)malloc(1460);
  if (!buf) { f.close(); LittleFS.remove(EYE_TMP_PATH); eyeDlErr = "Memori kurang"; http.end(); return false; }
  eyeDlTotal = (uint32_t)total;
  WiFiClient *st = http.getStreamPtr();
  unsigned long lastData = millis(), lastDraw = 0;
  bool ok = true;
  while (eyeDlGot < eyeDlTotal) {
    int av = st->available();
    if (av > 0) {
      int n = st->readBytes(buf, av > 1460 ? 1460 : av);
      if (n <= 0) { eyeDlErr = "Terputus"; ok = false; break; }
      if (f.write(buf, n) != (size_t)n) { eyeDlErr = "Tulis gagal"; ok = false; break; }
      eyeDlGot += n;
      lastData = millis();
    } else {
      if (!st->connected()) break;
      if (millis() - lastData > 15000) { eyeDlErr = "Timeout"; ok = false; break; }
      delay(2);
    }
    if (millis() - lastDraw > 80) { lastDraw = millis(); setupDrawEyes(); }
  }
  free(buf);
  f.close();
  http.end();
  if (ok && eyeDlGot != eyeDlTotal) { eyeDlErr = "Terputus"; ok = false; }
  if (!ok) { LittleFS.remove(EYE_TMP_PATH); return false; }
  File v = LittleFS.open(EYE_TMP_PATH, "r");        // validasi isi sebelum dipakai
  uint8_t h[16];
  bool good = v && v.read(h, 16) == 16 && memcmp(h, "EYG1", 4) == 0 &&
              ((uint32_t)h[12] | ((uint32_t)h[13] << 8) | ((uint32_t)h[14] << 16) | ((uint32_t)h[15] << 24)) == eyeDlTotal;
  if (v) v.close();
  if (!good) { LittleFS.remove(EYE_TMP_PATH); eyeDlErr = "File rusak"; return false; }
  LittleFS.remove(EYE_BIN_PATH);
  if (!LittleFS.rename(EYE_TMP_PATH, EYE_BIN_PATH)) { eyeDlErr = "Simpan gagal"; return false; }
  return true;
}

// Dashboard web diambil browser dari GitHub; di sini hanya dicek bahwa berkasnya ada dan dapat dijangkau.
static bool dashCheck() {
  const char *urls[2] = { DASH_URL, DASH_URL2 };
  for (int i = 0; i < 2; i++) {
    WiFiClientSecure cl;
    cl.setInsecure();
    cl.setTimeout(8);
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    http.useHTTP10(true);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(cl, urls[i])) continue;
    int code = http.GET();
    int sz = http.getSize();
    http.end();
    if (code == 200 && sz > 5000) return true;
  }
  return false;
}

static void setupFinish(bool skipped) {
  prefs.putBool("setupDone", true);
  if (skipped) prefs.putBool("eyeSkip", true);
  setupActive = false;
  eyeStarted = false;
  startOnlineTask();
  Serial.println(skipped ? "[SETUP] Dilewati" : "[SETUP] Selesai");
}

void setupWizardTick() {
  unsigned long now = millis();
  switch (setupStep) {
    case 0: {                                    // WiFi
      if (btn.takeLong()) { setupFinish(true); return; }
      btn.takeSingle(); btn.takeDouble();
      if (WiFi.status() == WL_CONNECTED) { setupStep = 1; setupStepMs = now; eyeDlTry = 0; eyeDlFailed = false; return; }
      if (!apActive && !wifiValidating && (wifiListN == 0 || (wifiDownSince && now - wifiDownSince > 25000))) startEmergencyAP(false);
      if (now - setupDrawMs >= 66) { setupDrawMs = now; setupDrawWifi(); }
      break;
    }
    case 1: {                                    // animasi mata
      if (eyeReady) { setupStep = 2; return; }
      if (WiFi.status() != WL_CONNECTED) { setupStep = 0; return; }
      if (eyeDlFailed) {
        if (btn.takeLong()) { setupFinish(true); return; }
        if (btn.takeSingle()) { eyeDlFailed = false; eyeDlTry = 0; return; }
        btn.takeDouble();
        if (now - setupDrawMs >= 100) { setupDrawMs = now; setupDrawFail(); }
        return;
      }
      eyeDlTry++;
      bool ok = eyeDownload();
      if (ok) { if (eyeLoadTable()) { setupStep = 2; return; } if (!eyeDlErr.length()) eyeDlErr = "Muat tabel gagal"; }
      Serial.println("[SETUP] Unduh gagal: " + eyeDlErr);
      if (eyeDlTry >= 3) {
        eyeDlFailed = true;
        btn.takeSingle(); btn.takeLong(); btn.takeDouble();
      } else {
        eyeDlGot = 0; eyeDlTotal = 0;
        unsigned long t0 = millis();
        while (millis() - t0 < 1800) { setupDrawEyes(); delay(80); }   // jeda sebelum coba lagi (pesan galat tampil)
      }
      break;
    }
    case 2: {                                    // cek dashboard
      unsigned long t0 = millis();
      setupDrawWebCheck();
      dashOk = dashCheck();
      Serial.printf("[SETUP] Dashboard GitHub %s\n", dashOk ? "terjangkau" : "tidak terjangkau");
      while (millis() - t0 < 1500) { setupDrawWebCheck(); delay(40); }
      setupStep = 3;
      setupStepMs = millis();
      btn.takeSingle(); btn.takeLong(); btn.takeDouble();
      break;
    }
    default: {                                   // selesai
      unsigned long el = now - setupStepMs;
      if (now - setupDrawMs >= 50) { setupDrawMs = now; setupDrawDone(el); }
      if (el > 5000 || (el > 1000 && btn.takeSingle())) setupFinish(false);
      break;
    }
  }
}

void drawInfoScreen() {
  static unsigned long lastDraw = 0;
  if (millis() - lastDraw < 500) return;
  lastDraw = millis();

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("INFO SISTEM");

  display.setCursor(0, 12);
  display.print("IP   : ");
  display.print((WiFi.status() != WL_CONNECTED && apActive) ? WiFi.softAPIP().toString() : WiFi.localIP().toString());

  display.setCursor(0, 22);
  display.print("RSSI : ");
  display.print(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -999);

  display.setCursor(0, 32);
  display.print("MODE : ");
  display.print(getCurrentModeText());

  display.setCursor(0, 42);
  display.print("SLEEP: ");
  display.print(getCurrentSleepText());

  display.setCursor(0, 52);
  if (apActive) {
    display.print("AP   : ");
    display.print(AP_SSID);
  } else {
    display.print("TEMP : ");
    display.print(String(wTemp, 1));
    display.print("C");
  }

  display.display();
}

// ═══════════════════════════════════════════════════════════
// TOMBOL DI MODE NAV
// Fitur pelacak jejak sudah dipisah ke sketch sendiri (ESP32_Jejak_Tracker). Di mode NAV tombol tidak dipakai:
// buang semua event supaya tidak "bocor" ke mode WiFi saat berpindah.
// ═══════════════════════════════════════════════════════════
void pollNavButtons() {
  btn.take(); btn.takeClick(); btn.takeLong(); btn.takeSingle(); btn.takeDouble(); btn.takeTriple();
}

// ═══════════════════════════════════════════════════════════
// NAVIGATION MODE
// ═══════════════════════════════════════════════════════════
void clearNavState() {
  btBuffer = "";
  navType = "";
  navDistance = "";
  navRoad = "";
  idleSpeed = "--";
  waitMessage = "";
  navGPSWeak = false;
  navArrived = false;
  btClientPrev = false;
  lastBTDataMs = millis();
  lastScrollMs = millis();
  lastBlinkMs = millis();
  lastNavFrameMs = 0;
  blinkState = false;
  scrollIndex = 0;
}

void drawNavWaitingScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(24, 2);
  display.print("[ MOTONAV v2.0 ]");
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
  display.setCursor(8, 18);
  display.print("Menunggu koneksi");
  display.setCursor(8, 28);
  display.print("Bluetooth...");
  display.drawLine(0, 40, 128, 40, SSD1306_WHITE);
  display.setCursor(4, 46);
  display.print("BT: " BT_DEVICE_NAME);
  display.setCursor(4, 56);
  display.print("Pair di HP Anda");
  display.display();
}

void drawNavConnectedFlash() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(16, 20);
  display.print("HP Terhubung!");
  display.setCursor(10, 34);
  display.print("Siap navigasi :)");
  display.display();
}

void drawNavIdleScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("IDLE");
  if (SerialBT.hasClient()) { display.setCursor(86, 0); display.print("BT:OK"); }
  display.drawLine(0, 9, 128, 9, SSD1306_WHITE);

  // Kecepatan font besar
  display.setTextSize(4);
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(idleSpeed, 0, 0, &bx, &by, &bw, &bh);
  display.setCursor((128 - bw) / 2, 13);
  display.print(idleSpeed);

  display.setTextSize(1);
  display.setCursor(46, 56);
  display.print("km/h");
  display.display();
}

void drawNavWaitScreen() {
  // GPS lemah – persis displayGPSWeak() MotoNav
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("GPS");
  if (SerialBT.hasClient()) { display.setCursor(90, 0); display.print("BT:OK"); }
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  // Animasi blink teks
  if (blinkState) {
    display.setTextSize(1);
    display.setCursor(14, 18);
    display.print("Mencari sinyal");
    display.setCursor(14, 30);
    display.print("GPS...");
  }

  // Ikon sinyal bar (cx=64, cy=48)
  display.setTextSize(1);
  display.setCursor(64 - 20, 48 - 8);
  display.print("GPS...");
  display.fillRect(64 - 10, 48 + 4, 4, 6, SSD1306_WHITE);
  display.drawRect(64 - 4,  48 + 2, 4, 8, SSD1306_WHITE);
  display.drawRect(64 + 2,  48,     4, 10, SSD1306_WHITE);
  display.display();
}

void drawNavArrivedScreen() {
  // Full screen TIBA dengan blink (persis displayTiba() MotoNav)
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Frame berkedip
  if (blinkState) {
    display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
    display.drawRect(2, 2, 124, 60, SSD1306_WHITE);
  }

  // Ikon centang kiri + teks TIBA kanan
  drawNavIconArrived(20, 26, 24);

  display.setTextSize(2);
  display.setCursor(52, 14);
  display.print("TIBA");

  display.drawFastHLine(0, 44, 128, SSD1306_WHITE);

  // Nama tujuan (centered bawah)
  display.setTextSize(1);
  String tujuan = navRoad;
  if (tujuan.length() == 0) tujuan = "Tujuan";
  if ((int)tujuan.length() > 21) tujuan = tujuan.substring(0, 21);
  int16_t tx, ty; uint16_t tw, th;
  display.getTextBounds(tujuan, 0, 0, &tx, &ty, &tw, &th);
  display.setCursor((128 - tw) / 2, 50);
  display.print(tujuan);

  display.display();
}

void drawNavArrowScreen() {
  // Persis displayNavigation() dari MotoNav_ESP32-1.ino
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  int arrowCX = 36, arrowCY = 19, arrowSZ = 17;

  // ── Gambar ikon sesuai arah ──────────────────────
  if      (navType == "KANAN")           drawNavIconArrowRight(arrowCX, arrowCY, arrowSZ);
  else if (navType == "KIRI")            drawNavIconArrowLeft (arrowCX, arrowCY, arrowSZ);
  else if (navType == "TIBA")            drawNavIconArrived   (arrowCX, arrowCY, arrowSZ);
  else if (navType == "BALIK")           drawNavIconUTurn     (arrowCX, arrowCY, arrowSZ);
  else if (navType == "BUNDARAN")        drawNavIconRoundabout(navType, arrowCX, arrowCY, arrowSZ);
  else if (navType == "BUNDARAN-KANAN")  drawNavIconRoundabout(navType, arrowCX, arrowCY, arrowSZ);
  else if (navType == "BUNDARAN-LURUS")  drawNavIconRoundabout(navType, arrowCX, arrowCY, arrowSZ);
  else if (navType == "BUNDARAN-KIRI")   drawNavIconRoundabout(navType, arrowCX, arrowCY, arrowSZ);
  else if (navType == "BUNDARAN-BALIK")  drawNavIconRoundabout(navType, arrowCX, arrowCY, arrowSZ);
  else if (navType == "SIAP-KANAN" || navType == "SIAP-KIRI") {
    if (blinkState) drawNavIconWarningOutline(navType, arrowCX, arrowCY, arrowSZ);
  }
  else if (navType.startsWith("SIAP-BUNDARAN")) {
    if (blinkState) drawNavIconRoundabout(navType, arrowCX, arrowCY, arrowSZ);
  }
  else                                   drawNavIconArrowUp   (arrowCX, arrowCY, arrowSZ);

  // ── Label arah (kiri bawah area atas) ───────────
  display.setTextSize(1);
  display.setCursor(2, 30);
  String labelArah = navType;
  if      (navType == "SIAP-KANAN")       labelArah = ">> KANAN";
  else if (navType == "SIAP-KIRI")        labelArah = "<< KIRI";
  else if (navType == "BALIK")            labelArah = "BALIK";
  else if (navType == "BUNDARAN")         labelArah = "BUNDARAN";
  else if (navType == "BUNDARAN-KANAN")   labelArah = "BDR-EXIT1";
  else if (navType == "BUNDARAN-LURUS")   labelArah = "BDR-LURUS";
  else if (navType == "BUNDARAN-KIRI")    labelArah = "BDR-EXIT3";
  else if (navType == "BUNDARAN-BALIK")   labelArah = "BDR-BALIK";
  display.print(labelArah);

  // ── Garis pemisah vertikal ───────────────────────
  display.drawFastVLine(78, 0, 40, SSD1306_WHITE);

  // ── Jarak (kanan atas) ───────────────────────────
  display.setTextSize(1);
  display.setCursor(82, 2);
  display.print("Jarak");
  if (navDistance.length() <= 5) {
    display.setTextSize(2);
    display.setCursor(82, 14);
  } else {
    display.setTextSize(1);
    display.setCursor(82, 14);
  }
  display.print(navDistance);

  // ── Garis pemisah horizontal ─────────────────────
  display.drawFastHLine(0, 40, 128, SSD1306_WHITE);

  // ── Nama jalan bawah (scroll) ────────────────────
  display.setTextSize(1);
  String jalan = (navRoad.length() > 0) ? navRoad : "---";
  if ((int)jalan.length() <= 21) {
    display.setCursor(0, 48);
    display.print(jalan);
  } else {
    if (millis() - lastScrollMs >= scrollIntervalMs) {
      lastScrollMs = millis();
      scrollIndex++;
    }
    String scrollText = jalan + "   ";
    int len = scrollText.length();
    if (scrollIndex >= len) scrollIndex = 0;
    String line = "";
    for (int i = 0; i < 21; i++) {
      line += scrollText[(scrollIndex + i) % len];
    }
    display.setCursor(0, 48);
    display.print(line);
  }

  // Indikator BT
  display.setTextSize(1);
  display.setCursor(110, 48);
  if (SerialBT.hasClient()) display.print("BT");

  display.display();
}

void drawNavGPSWeakScreen() {
  drawNavWaitScreen();  // Alias ke drawNavWaitScreen
}

void drawNavIconArrowUp(int cx, int cy, int sz) {
  display.fillTriangle(
    cx,            cy - sz,
    cx - sz*6/10,  cy,
    cx + sz*6/10,  cy,
    SSD1306_WHITE);
  int sw = max(4, sz/4);
  display.fillRect(cx - sw/2, cy, sw, sz/2, SSD1306_WHITE);
}

void drawNavIconArrowRight(int cx, int cy, int sz) {
  display.fillTriangle(
    cx + sz,  cy,
    cx,       cy - sz*6/10,
    cx,       cy + sz*6/10,
    SSD1306_WHITE);
  int thick = max(4, sz/4);
  display.fillRect(cx - sz/2, cy - thick/2, sz/2, thick, SSD1306_WHITE);
  display.fillRect(cx - sz/2 - thick/2, cy, thick, sz/2, SSD1306_WHITE);
}

void drawNavIconArrowLeft(int cx, int cy, int sz) {
  display.fillTriangle(
    cx - sz,  cy,
    cx,       cy - sz*6/10,
    cx,       cy + sz*6/10,
    SSD1306_WHITE);
  int thick = max(4, sz/4);
  display.fillRect(cx, cy - thick/2, sz/2, thick, SSD1306_WHITE);
  display.fillRect(cx + sz/2 - thick/2, cy, thick, sz/2, SSD1306_WHITE);
}

void drawNavIconUTurn(int cx, int cy, int sz) {
  // Gambar "U" dengan panah ke kiri di bawah (persis MotoNav)
  display.drawCircle(cx, cy - sz/4, sz/2, SSD1306_WHITE);
  display.drawCircle(cx, cy - sz/4, sz/2 - 1, SSD1306_WHITE);
  // Tutup setengah bawah lingkaran (bagian kanan)
  display.fillRect(cx, cy - sz/4, sz/2 + 2, sz/2 + 4, SSD1306_BLACK);
  // Tangkai bawah kanan
  display.fillRect(cx + sz/2 - 2, cy - sz/4, 4, sz/2 + 4, SSD1306_WHITE);
  // Anak panah ke kiri
  display.fillTriangle(
    cx - sz/3, cy + sz/4,
    cx + sz/6, cy,
    cx + sz/6, cy + sz/2,
    SSD1306_WHITE);
}

void drawNavIconRoundabout(const String &type, int cx, int cy, int sz) {
  // Lingkaran bundaran (persis MotoNav drawBundaran)
  display.drawCircle(cx, cy, sz/2, SSD1306_WHITE);
  display.drawCircle(cx, cy, sz/2 - 2, SSD1306_WHITE);

  int r = sz/2 + 5;
  if (type == "BUNDARAN-KANAN") {
    // Exit 1 → keluar kanan
    display.fillTriangle(cx+r, cy, cx+r-5, cy-4, cx+r-5, cy+4, SSD1306_WHITE);
  } else if (type == "BUNDARAN-LURUS") {
    // Exit 2 → keluar atas (lurus)
    display.fillTriangle(cx, cy-r, cx-4, cy-r+5, cx+4, cy-r+5, SSD1306_WHITE);
  } else if (type == "BUNDARAN-KIRI") {
    // Exit 3 → keluar kiri
    display.fillTriangle(cx-r, cy, cx-r+5, cy-4, cx-r+5, cy+4, SSD1306_WHITE);
  } else if (type == "BUNDARAN-BALIK") {
    // Exit 4+ → keluar bawah (balik)
    display.fillTriangle(cx, cy+r, cx-4, cy+r-5, cx+4, cy+r-5, SSD1306_WHITE);
  } else {
    // Tidak diketahui → tanda tanya
    display.setTextSize(1);
    display.setCursor(cx-3, cy-4);
    display.print("?");
  }
}

void drawNavIconArrived(int cx, int cy, int sz) {
  // Centang dalam lingkaran (persis MotoNav drawTiba)
  display.drawCircle(cx, cy, sz/2 + 3, SSD1306_WHITE);
  display.drawCircle(cx, cy, sz/2 + 4, SSD1306_WHITE);
  int x1 = cx - sz/4, y1 = cy;
  int x2 = cx,         y2 = cy + sz/4;
  int x3 = cx + sz/2,  y3 = cy - sz/4;
  for (int i = -1; i <= 1; i++) {
    display.drawLine(x1, y1+i, x2, y2+i, SSD1306_WHITE);
    display.drawLine(x2, y2+i, x3, y3+i, SSD1306_WHITE);
  }
}

void drawNavIconWarningOutline(const String &type, int cx, int cy, int sz) {
  // Panah outline (tidak solid) sebagai peringatan dini (persis MotoNav drawArrowSiap)
  int smallSz = sz - 4;
  if (type == "SIAP-KANAN") {
    display.drawTriangle(
      cx + smallSz, cy,
      cx,           cy - smallSz*6/10,
      cx,           cy + smallSz*6/10,
      SSD1306_WHITE);
    int thick = max(3, smallSz/4);
    display.drawRect(cx - smallSz/2, cy - thick/2, smallSz/2, thick, SSD1306_WHITE);
  } else {
    display.drawTriangle(
      cx - smallSz, cy,
      cx,           cy - smallSz*6/10,
      cx,           cy + smallSz*6/10,
      SSD1306_WHITE);
    int thick = max(3, smallSz/4);
    display.drawRect(cx, cy - thick/2, smallSz/2, thick, SSD1306_WHITE);
  }
}

String getNavAction(const String &packetType) {
  if (packetType.startsWith("SIAP-")) return "WARN";
  if (packetType.startsWith("BUNDARAN")) return "ROUND";
  if (packetType == "TIBA") return "ARRIVE";
  if (packetType == "WAIT") return "WAIT";
  if (packetType == "IDLE") return "IDLE";
  return "NAV";
}

void parseBTLine(const String &line) {
  String s = line;
  s.trim();
  if (s.length() == 0) return;

  Serial.print("[BT] RX: ");
  Serial.println(s);

  if (s == "SWITCH_MENU") {
    Serial.println("[BT] Command SWITCH_MENU");
    switchToWiFiMode();
    return;
  }

  // ── Ganti WiFi via Bluetooth – pakai sistem validasi ─────────
  // Format: SETWIFI:NamaWifi:password
  if (s.startsWith("SETWIFI:")) {
    String rest = s.substring(8);
    int col = rest.indexOf(':');
    if (col > 0) {
      String newSSID = rest.substring(0, col);
      String newPASS = rest.substring(col + 1);
      newSSID.trim(); newPASS.trim();
      if (newSSID.length() > 0) {
        Serial.println("[BT] SETWIFI validasi: " + newSSID);
        // Beritahu HP bahwa sedang diproses
        SerialBT.println("WIFI_TRYING:" + newSSID);
        // Pindah ke WiFi mode lalu validasi (credential belum disimpan ke NVS)
        // Jika gagal → otomatis balik ke NAV mode dengan credential lama
        enterWiFiModeForValidation(newSSID, newPASS);
      } else {
        SerialBT.println("WIFI_ERROR:SSID_KOSONG");
      }
    } else {
      SerialBT.println("WIFI_ERROR:FORMAT_SALAH");
    }
    return;
  }

  lastBTDataMs = millis();

  if (s.startsWith("NAV:")) {
    int p1 = s.indexOf(':');
    int p2 = s.indexOf(':', p1 + 1);
    int p3 = s.indexOf(':', p2 + 1);
    if (p2 > 0 && p3 > 0) {
      navType = s.substring(p1 + 1, p2);
      navDistance = s.substring(p2 + 1, p3);
      navRoad = s.substring(p3 + 1);
      navArrived = (navType == "TIBA");
      navGPSWeak = false;
      if (navType == "TIBA") {
        navDistance = "0m";
        navArrived = true;
      }
      return;
    }
  }

  if (s.startsWith("IDLE:")) {
    idleSpeed = s.substring(5);
    idleSpeed.trim();
    navType = "";
    navDistance = "";
    navRoad = "";
    navGPSWeak = false;
    navArrived = false;
    return;
  }

  if (s.startsWith("WAIT:")) {
    navType = "WAIT";
    navDistance = "";
    navRoad = "";
    waitMessage = s.substring(5);
    waitMessage.trim();
    navGPSWeak = true;
    navArrived = false;
    return;
  }

  if (s.startsWith("GPS_LEMAH")) {
    navType = "WAIT";
    navGPSWeak = true;
    waitMessage = s;
    return;
  }
}

void processBTInput() {
  while (SerialBT.available()) {
    char c = (char)SerialBT.read();
    if (c == '\n' || c == '\r') {
      if (btBuffer.length()) {
        parseBTLine(btBuffer);
        btBuffer = "";
      }
    } else {
      btBuffer += c;
      if (btBuffer.length() > 200) btBuffer = "";
    }
  }
}

void updateNavMode() {
  if (!btEnabled) {
    SerialBT.begin(BT_DEVICE_NAME);
    btEnabled = true;
    Serial.println("[BT] SerialBT lazy-started");
  }

  bool hasClient = SerialBT.hasClient();
  if (hasClient && !btClientPrev) {
    Serial.println("[BT] Client connected");
    drawNavConnectedFlash();
    delay(900);
    clearNavState();
  } else if (!hasClient && btClientPrev) {
    Serial.println("[BT] Client disconnected");
    clearNavState();
  }
  btClientPrev = hasClient;

  processBTInput();

  if (hasClient) {
    if (millis() - lastBTDataMs > navTimeoutMs) {
      if (navType != "IDLE") {
        navType = "IDLE";
        idleSpeed = "--";
        navRoad = "";
        navDistance = "";
      }
    }
  } else {
    static unsigned long lastWaitDraw = 0;
    if (millis() - lastWaitDraw >= 300) {
      lastWaitDraw = millis();
      drawNavWaitingScreen();
    }
    return;
  }

  if (millis() - lastBlinkMs >= blinkIntervalMs) {
    lastBlinkMs = millis();
    blinkState = !blinkState;
  }

  if (navType == "WAIT" || navGPSWeak) {
    drawNavGPSWeakScreen();
  } else if (navArrived) {
    drawNavArrivedScreen();
  } else if (navType == "IDLE") {
    drawNavIdleScreen();
  } else if (navType.length() > 0) {
    drawNavArrowScreen();
  } else {
    drawNavIdleScreen();
  }
}

// ═══════════════════════════════════════════════════════════
// UTILITAS WAKTU
// ═══════════════════════════════════════════════════════════
bool parseHHMM(const String &s, int &hOut, int &mOut) {
  int col = s.indexOf(':');
  if (col < 1) return false;
  String hStr = s.substring(0, col);
  String mStr = s.substring(col + 1);
  hStr.trim(); mStr.trim();
  if (hStr.length() == 0 || mStr.length() == 0) return false;
  int h = hStr.toInt();
  int m = mStr.toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  hOut = h; mOut = m;
  return true;
}

// ═══════════════════════════════════════════════════════════
// TOMBOL, MINI GAMES (CHROME DINO & FLAPPY BIRD), DEEP SLEEP
// ═══════════════════════════════════════════════════════════
// Kontrol SATU tombol (GPIO 4), hanya di WIFI_MODE:
//   SmartDisplay : tahan lama            -> buka Mini Games (juga saat Deep Sleep aktif)
//                  2x klik cepat         -> buka Timer & Stopwatch
//                  3x klik cepat         -> keluar Deep Sleep (hanya bekerja saat Deep Sleep aktif)
//   Alarm berbunyi / popup sholat : klik 1x -> matikan / tutup (mengalahkan fungsi lain)
//   Menu game    : klik                  -> ganti pilihan,  tahan lama -> pilih / mulai ("Keluar" kembali ke SmartDisplay)
//   Dalam game   : klik / tekan          -> lompat / flap,  tahan lama -> kembali ke menu game
//   Game over    : klik                  -> main lagi,      tahan lama -> kembali ke menu game
//   Timer/Stopwatch: lihat toolLoop()
// Deteksi 3x klik berjalan terpisah dari fungsi game, jadi fungsi tombol di game tidak berubah.
void pollButtons() {

  // Alarm sedang berbunyi: 1x tekan tombol langsung mematikannya. Sisa gesture tombol itu
  // (lepas / klik tertunda) dibuang sebentar supaya tidak ikut memicu fungsi lain.
  static uint32_t alarmBtnSwallowUntil = 0;
  if ((buzzerSirenActive || prayerAlertActive) && btn.take()) {
    if (buzzerSirenActive) {
      stopAlarmSiren();
      Serial.println("[ALARM] Dimatikan dengan tombol");
    } else {
      dismissPrayerAlert();
      Serial.println("[SHOLAT] Pengingat ditutup dengan tombol");
    }
    alarmBtnSwallowUntil = millis() + 900;
  }
  if ((int32_t)(alarmBtnSwallowUntil - millis()) > 0) {
    btn.take(); btn.takeClick(); btn.takeLong(); btn.takeSingle(); btn.takeDouble(); btn.takeTriple();
    return;
  }

  bool triple = btn.takeTriple();
  bool idle = !gameActive && !toolActive;
  bool lng = false, dbl = false;
  if (idle) {                                  // di dalam game / timer, event dipakai gameLoop() / toolLoop()
    lng = btn.takeLong();
    dbl = btn.takeDouble();
  } else if (gameActive) {
    btn.takeSingle();                          // klik ganda/tunggal tertunda tidak dipakai game
    btn.takeDouble();
  }

  if (triple && deepSleepMode) {
    exitDeepSleepMode();
  }

  bool canOpen = !oledSleepState && !wifiValidating;
  if (lng && canOpen)      openGames();
  else if (dbl && canOpen) openTools();

  // Di luar game & timer, event tombol tidak dipakai fungsi lain: buang agar tidak "bocor"
  if (!gameActive && !toolActive) { btn.take(); btn.takeClick(); btn.takeLong(); btn.takeSingle(); btn.takeDouble(); }
}

void openGames() {
  Serial.println("[GAME] Buka Mini Games");
  gameMenuSel = 0;
  gameState = GS_MENU;
  gameLastFrame = 0;
  gameActive = true;
  btn.take();
  btn.takeClick();
  btn.takeLong();
  btn.takeSingle();
  btn.takeDouble();
}

// ---------- Helper gambar ----------
void gameCentered(const char* s, int y, int size) {
  int16_t x1, y1; uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.print(s);
}

void gameDrawOverlay(uint32_t sc, uint32_t hi) {
  display.fillRect(10, 5, 108, 54, SSD1306_BLACK);
  display.drawRect(10, 5, 108, 54, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE);
  gameCentered("GAME OVER", 10, 1);
  char buf[28];
  snprintf(buf, sizeof(buf), "Skor %u  Hi %u", (unsigned)sc, (unsigned)hi);
  gameCentered(buf, 22, 1);
  gameCentered("Klik: Ulang", 34, 1);
  gameCentered("Tahan: Menu", 45, 1);
  display.display();
}

// =====================================================
//                     CHROME DINO
// =====================================================
void dinoReset() {
  dinoY = GROUND_Y - DINO_H;
  dinoVy = 0;
  dinoOnGround = true;
  dSpeed = DINO_START_SPEED;
  dScore = 0;
  dFrame = 0;
  dDistSince = 0;
  dNextGap = 110;
  dGroundScroll = 0;
  for (auto &c : cacti) c.active = false;
}

void drawDino(int x, int y, bool onGround, uint32_t frame) {
  display.fillRect(x + 2, y + 4, 8, 6, SSD1306_WHITE);     // badan
  display.fillRect(x + 6, y, 6, 6, SSD1306_WHITE);         // kepala
  display.drawPixel(x + 9, y + 1, SSD1306_BLACK);          // mata
  display.fillRect(x, y + 6, 3, 3, SSD1306_WHITE);         // ekor
  display.fillRect(x + 9, y + 7, 2, 1, SSD1306_WHITE);     // tangan
  if (onGround && ((frame / 4) % 2 == 0)) {
    display.fillRect(x + 3, y + 10, 2, 4, SSD1306_WHITE);
    display.fillRect(x + 7, y + 10, 2, 2, SSD1306_WHITE);
  } else if (onGround) {
    display.fillRect(x + 3, y + 10, 2, 2, SSD1306_WHITE);
    display.fillRect(x + 7, y + 10, 2, 4, SSD1306_WHITE);
  } else {
    display.fillRect(x + 3, y + 10, 2, 4, SSD1306_WHITE);
    display.fillRect(x + 7, y + 10, 2, 4, SSD1306_WHITE);
  }
}

void drawCactus(int x, int y, int w, int h) {
  int mid = x + w / 2;
  display.fillRect(mid - 1, y, 3, h, SSD1306_WHITE);                        // batang
  display.fillRect(x, y + h / 3, w / 2, 2, SSD1306_WHITE);                  // lengan kiri
  display.fillRect(x, y + h / 3 - 3, 2, 4, SSD1306_WHITE);
  display.fillRect(mid + 1, y + h / 2, w - w / 2 - 1, 2, SSD1306_WHITE);    // lengan kanan
  display.fillRect(x + w - 2, y + h / 2 - 3, 2, 4, SSD1306_WHITE);
}

bool rectHit(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

// return true jika dino mati
bool dinoUpdate(bool jump) {
  if (jump && dinoOnGround) { dinoVy = -6.8; dinoOnGround = false; }
  dinoVy += 0.6;
  dinoY += dinoVy;
  if (dinoY >= GROUND_Y - DINO_H) {
    dinoY = GROUND_Y - DINO_H; dinoVy = 0; dinoOnGround = true;
  }

  dFrame++;
  dScore += dSpeed * 0.05;
  dSpeed = min(DINO_MAX_SPEED, DINO_START_SPEED + dScore / 100.0f);   // makin tinggi skor makin cepat
  dGroundScroll += dSpeed;

  // spawn kaktus
  dDistSince += dSpeed;
  if (dDistSince >= dNextGap) {
    // Setelah skor tertentu, kaktus kadang muncul berkelompok (2 sekaligus), peluangnya naik seiring skor
    int toSpawn = (dScore > 120 && random(0, 100) < min(35, (int)(dScore / 12))) ? 2 : 1;
    float nextX = SCREEN_WIDTH;
    for (auto &c : cacti) {
      if (toSpawn <= 0) break;
      if (!c.active) {
        c.active = true;
        c.x = nextX;
        c.w = random(8, 15);
        c.h = random(12, 21);
        nextX += c.w + random(10, 16);
        toSpawn--;
      }
    }
    dDistSince = 0;
    // Jarak antar kaktus (dalam satuan "frame") makin rapat dan makin acak seiring skor
    float gapFrames = max(15.0f, 24.0f - dScore / 70.0f);
    dNextGap = dSpeed * gapFrames + 20 + random(0, 40 + (int)min(dScore / 4.0f, 60.0f));
  }

  // gerak + tabrakan
  for (auto &c : cacti) {
    if (!c.active) continue;
    c.x -= dSpeed;
    if (c.x + c.w < 0) { c.active = false; continue; }
    if (rectHit(DINO_X + 2, (int)dinoY + 2, 9, 12,
                (int)c.x + 1, GROUND_Y - c.h, c.w - 2, c.h)) return true;
  }
  return false;
}

void dinoDraw() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  char buf[32];
  snprintf(buf, sizeof(buf), "HI %05u %05u", (unsigned)hiDino, (unsigned)dScore);
  display.setCursor(SCREEN_WIDTH - 13 * 6, 0);
  display.print(buf);

  display.drawFastHLine(0, GROUND_Y, SCREEN_WIDTH, SSD1306_WHITE);
  for (int i = 0; i < 6; i++) {
    int gx = ((i * 29 + 7) - (int)dGroundScroll) % SCREEN_WIDTH;
    if (gx < 0) gx += SCREEN_WIDTH;
    display.drawPixel(gx, GROUND_Y + 3 + (i % 2) * 2, SSD1306_WHITE);
  }
  for (auto &c : cacti)
    if (c.active) drawCactus((int)c.x, GROUND_Y - c.h, c.w, c.h);
  drawDino(DINO_X, (int)dinoY, dinoOnGround, dFrame);
}

// =====================================================
//                    FLAPPY BIRD
// =====================================================
void flapReset() {
  birdY = 28; birdVy = 0; fScore = 0; fFrame = 0;
  for (int i = 0; i < 3; i++) {
    pipes[i].x = 150 + i * PIPE_SPACING;
    pipes[i].gapY = random(8, 25);
    pipes[i].scored = false;
  }
}

void drawBird(int y, bool wingUp) {
  display.fillCircle(BIRD_X, y, 4, SSD1306_WHITE);
  display.drawPixel(BIRD_X + 2, y - 1, SSD1306_BLACK);                    // mata
  display.fillRect(BIRD_X + 4, y, 3, 2, SSD1306_WHITE);                   // paruh
  display.drawFastHLine(BIRD_X - 3, y + (wingUp ? 0 : 2), 3, SSD1306_BLACK); // sayap
}

// return true jika burung mati
bool flapUpdate(bool flap) {
  fFrame++;
  if (flap) birdVy = -2.5;
  birdVy += 0.2;
  if (birdVy > 3.5) birdVy = 3.5;
  birdY += birdVy;

  if (birdY - 4 < 0 || birdY + 4 >= FLOOR_Y) return true;

  float speed = 1.4f + min(0.8f, fScore / 50.0f);
  for (int i = 0; i < 3; i++) {
    Pipe &p = pipes[i];
    p.x -= speed;

    if (!p.scored && p.x + PIPE_W < BIRD_X - 4) { p.scored = true; fScore++; }

    if (p.x + PIPE_W < -3) {            // daur ulang pipa
      float maxX = 0;
      for (int j = 0; j < 3; j++) if (pipes[j].x > maxX) maxX = pipes[j].x;
      p.x = maxX + PIPE_SPACING;
      p.gapY = random(8, 25);
      p.scored = false;
    }

    if (BIRD_X + 3 > p.x && BIRD_X - 3 < p.x + PIPE_W) {
      if (birdY - 3 < p.gapY || birdY + 3 > p.gapY + GAP_H) return true;
    }
  }
  return false;
}

void flapDraw() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  for (int i = 0; i < 3; i++) {
    int x = (int)pipes[i].x, g = pipes[i].gapY;
    display.drawRect(x, 0, PIPE_W, g, SSD1306_WHITE);                       // pipa atas
    display.fillRect(x - 2, g - 5, PIPE_W + 4, 5, SSD1306_WHITE);
    display.drawRect(x, g + GAP_H, PIPE_W, FLOOR_Y - (g + GAP_H), SSD1306_WHITE);  // pipa bawah
    display.fillRect(x - 2, g + GAP_H, PIPE_W + 4, 5, SSD1306_WHITE);
  }
  display.drawFastHLine(0, FLOOR_Y, SCREEN_WIDTH, SSD1306_WHITE);
  drawBird((int)birdY, birdVy < 0);

  char buf[8];
  snprintf(buf, sizeof(buf), "%u", (unsigned)fScore);
  display.fillRect(0, 0, 30, 10, SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print(buf);
}

// =====================================================
//                    MENU GAME
// =====================================================
void drawGameMenu() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  gameCentered("MINI GAMES", 0, 1);
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);

  const char* names[3] = { "Chrome Dino", "Flappy Bird", "Keluar" };
  uint32_t his[3] = { hiDino, hiFlap, 0 };
  for (int i = 0; i < 3; i++) {
    int y = 16 + i * 13;
    display.setTextSize(1);
    if (i == gameMenuSel) {
      display.fillRect(4, y - 2, 120, 12, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(10, y);
    display.print(names[i]);
    if (i < 2) {
      char b[12];
      snprintf(b, sizeof(b), "%u", (unsigned)his[i]);
      display.setCursor(118 - strlen(b) * 6, y);
      display.print(b);
    }
  }
  display.setTextColor(SSD1306_WHITE);
  gameCentered("Klik:Pilih Tahan:OK", 56, 1);
}

// Dipanggil dari loop() selama gameActive. Tombol di-update oleh pollButtons().
void gameLoop() {
  uint32_t now = millis();
  if (now - gameLastFrame < GAME_FRAME_MS) { delay(1); return; }
  gameLastFrame = now;

  bool press = btn.take();        // tombol baru ditekan (langsung): lompat / flap
  bool click = btn.takeClick();   // klik pendek (dilepas): ganti pilihan / main lagi
  bool lng   = btn.takeLong();    // tahan lama: pilih / kembali ke menu
  // Klik sisa dari lompatan terakhir tidak boleh langsung me-restart game
  bool overReady = (now - gameOverMs) > 700;

  switch (gameState) {
    case GS_MENU:
      if (click) gameMenuSel = (gameMenuSel + 1) % 3;
      if (lng) {
        if (gameMenuSel == 0)      { dinoReset(); gameState = GS_DINO; }
        else if (gameMenuSel == 1) { flapReset(); gameState = GS_FLAP_WAIT; }
        else {                      // Keluar -> kembali ke SmartDisplay
          gameActive = false;
          Serial.println("[GAME] Keluar Mini Games");
          return;
        }
      }
      drawGameMenu();
      display.display();
      break;

    case GS_DINO:
      if (lng) { gameState = GS_MENU; break; }
      if (dinoUpdate(press)) {
        uint32_t s = (uint32_t)dScore;
        if (s > hiDino) { hiDino = s; gamePrefs.putUInt("dino", hiDino); }
        dinoDraw();
        gameDrawOverlay(s, hiDino);
        gameOverMs = now;
        gameState = GS_DINO_OVER;
      } else {
        dinoDraw();
        display.display();
      }
      break;

    case GS_DINO_OVER:
      if (lng) gameState = GS_MENU;
      else if (click && overReady) { dinoReset(); gameState = GS_DINO; }
      break;

    case GS_FLAP_WAIT:
      if (lng) { gameState = GS_MENU; break; }
      birdY = 28 + sin(millis() / 200.0) * 3;     // burung melayang
      birdVy = 0;
      flapDraw();
      gameCentered("Tekan tombol", 40, 1);
      display.display();
      if (press) { birdVy = -2.5; gameState = GS_FLAP; }
      break;

    case GS_FLAP:
      if (lng) { gameState = GS_MENU; break; }
      if (flapUpdate(press)) {
        if (fScore > hiFlap) { hiFlap = fScore; gamePrefs.putUInt("flap", hiFlap); }
        flapDraw();
        gameDrawOverlay(fScore, hiFlap);
        gameOverMs = now;
        gameState = GS_FLAP_OVER;
      } else {
        flapDraw();
        display.display();
      }
      break;

    case GS_FLAP_OVER:
      if (lng) gameState = GS_MENU;
      else if (click && overReady) { flapReset(); gameState = GS_FLAP_WAIT; }
      break;
  }
}

// ═══════════════════════════════════════════════════════════
// TIMER & STOPWATCH (dibuka dengan 2x klik cepat dari SmartDisplay)
//   Menu       : klik = ganti pilihan, tahan lama = pilih ("Keluar" kembali ke SmartDisplay)
//   Stopwatch  : klik = mulai / jeda, 2x klik = reset, tahan lama = ke menu (tetap berjalan)
//   Timer      : atur   -> klik = ganti durasi, 2x klik = mulai
//                jalan  -> klik = jeda, 2x klik = batal
//                jeda   -> klik = lanjut, 2x klik = batal
//                selesai-> klik = matikan bunyi
//                tahan lama = ke menu (timer tetap berjalan & berbunyi di latar belakang)
// ═══════════════════════════════════════════════════════════
void openTools() {
  Serial.println("[TOOL] Buka Timer & Stopwatch");
  toolMenuSel = 0;
  toolState = TL_MENU;
  toolLastFrame = 0;
  toolActive = true;
  btn.take(); btn.takeClick(); btn.takeLong(); btn.takeSingle(); btn.takeDouble();
}

uint32_t swElapsedMs() { return swAccumMs + (swRunning ? (millis() - swStartMs) : 0); }

uint32_t tmRemainNowMs() {
  if (tmPhase == TM_RUN) {
    int32_t r = (int32_t)(tmEndMs - millis());
    return r > 0 ? (uint32_t)r : 0;
  }
  if (tmPhase == TM_PAUSE) return tmRemainMs;
  if (tmPhase == TM_DONE)  return 0;
  return (uint32_t)TIMER_PRESET_SEC[tmPreset] * 1000UL;
}

void timerStopBeep() {
  if (tmBeeping) { tmBeeping = false; buzzerOff(); }
}

// Dipanggil setiap loop(), di mode apa pun
void timerTick() {
  if (tmPhase == TM_RUN && (int32_t)(millis() - tmEndMs) >= 0) {
    tmPhase = TM_DONE;
    tmRemainMs = 0;
    tmBeeping = true;
    tmBeepStart = millis();
    Serial.println("[TIMER] Selesai");
  }
  if (tmBeeping) {
    uint32_t el = millis() - tmBeepStart;
    if (el > 10000) { timerStopBeep(); return; }          // bunyi maksimum 10 detik
    if ((el % 600) < 250) buzzerOn(2600); else buzzerOff();
  }
}

void toolFmtMs(char* out, size_t n, uint32_t ms, bool centi) {
  uint32_t tot = ms / 1000;
  uint32_t h = tot / 3600, m = (tot / 60) % 60, s = tot % 60;
  if (centi) {
    if (h > 0) snprintf(out, n, "%u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
    else       snprintf(out, n, "%02u:%02u.%02u", (unsigned)m, (unsigned)s, (unsigned)((ms % 1000) / 10));
  } else {
    snprintf(out, n, "%02u:%02u", (unsigned)(tot / 60), (unsigned)s);
  }
}

void toolDrawMenu() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  gameCentered("TIMER & STOPWATCH", 0, 1);
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);
  const char* names[3] = { "Stopwatch", "Timer", "Keluar" };
  for (int i = 0; i < 3; i++) {
    int y = 16 + i * 13;
    display.setTextSize(1);
    if (i == toolMenuSel) {
      display.fillRect(4, y - 2, 120, 12, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(10, y);
    display.print(names[i]);
    char b[14] = "";
    if (i == 0 && (swRunning || swAccumMs > 0)) toolFmtMs(b, sizeof(b), swElapsedMs(), false);
    if (i == 1 && tmPhase != TM_SET) {
      if (tmPhase == TM_DONE) snprintf(b, sizeof(b), "SELESAI");
      else toolFmtMs(b, sizeof(b), (tmRemainNowMs() + 999) / 1000 * 1000, false);
    }
    if (b[0]) { display.setCursor(118 - strlen(b) * 6, y); display.print(b); }
  }
  display.setTextColor(SSD1306_WHITE);
  gameCentered("Klik:Pilih Tahan:OK", 56, 1);
}

void toolDrawStopwatch() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  gameCentered(swRunning ? "STOPWATCH  >" : "STOPWATCH", 0, 1);
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);
  char b[16];
  toolFmtMs(b, sizeof(b), swElapsedMs(), true);
  gameCentered(b, 21, 2);
  gameCentered(swRunning ? "Klik:Jeda  2x:Reset" : (swAccumMs ? "Klik:Lanjut 2x:Reset" : "Klik:Mulai"), 44, 1);
  gameCentered("Tahan:Menu", 55, 1);
}

void toolDrawTimer() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  gameCentered(tmPhase == TM_RUN ? "TIMER  >" : (tmPhase == TM_PAUSE ? "TIMER (JEDA)" : "TIMER"), 0, 1);
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);
  char b[16];
  toolFmtMs(b, sizeof(b), (tmRemainNowMs() + 999) / 1000 * 1000, false);
  if (tmPhase == TM_DONE) {
    if ((millis() / 400) % 2 == 0) gameCentered("SELESAI!", 20, 2);
    gameCentered("Klik: Matikan", 44, 1);
  } else {
    gameCentered(b, 18, 3);
    switch (tmPhase) {
      case TM_SET:   gameCentered("Klik:Ganti 2x:Mulai", 44, 1); break;
      case TM_RUN:   gameCentered("Klik:Jeda  2x:Batal", 44, 1); break;
      default:       gameCentered("Klik:Lanjut 2x:Batal", 44, 1); break;
    }
  }
  gameCentered("Tahan:Menu", 55, 1);
}

void toolLoop() {
  uint32_t now = millis();
  if (now - toolLastFrame < 40) { delay(1); return; }
  toolLastFrame = now;

  btn.take();
  bool click  = btn.takeClick();
  bool single = btn.takeSingle();
  bool dbl    = btn.takeDouble();
  bool lng    = btn.takeLong();

  switch (toolState) {
    case TL_MENU:
      if (click) toolMenuSel = (toolMenuSel + 1) % 3;
      if (lng) {
        if (toolMenuSel == 0)      toolState = TL_STOPWATCH;
        else if (toolMenuSel == 1) toolState = TL_TIMER;
        else { toolActive = false; Serial.println("[TOOL] Keluar"); return; }
      }
      toolDrawMenu();
      break;

    case TL_STOPWATCH:
      if (lng) { toolState = TL_MENU; break; }
      if (click) {                                  // klik langsung merespons (presisi)
        if (swRunning) { swAccumMs += millis() - swStartMs; swRunning = false; }
        else           { swStartMs = millis(); swRunning = true; }
      }
      if (dbl) { swRunning = false; swAccumMs = 0; }   // 2 klik tadi hanya mulai/jeda; reset menghapus semuanya
      toolDrawStopwatch();
      break;

    case TL_TIMER:
      if (lng) { toolState = TL_MENU; break; }
      if (tmPhase == TM_DONE) {
        if (click || single || dbl) { timerStopBeep(); tmPhase = TM_SET; }
      } else if (tmPhase == TM_SET) {
        if (single) tmPreset = (tmPreset + 1) % TIMER_PRESET_N;
        if (dbl) {
          tmEndMs = millis() + (uint32_t)TIMER_PRESET_SEC[tmPreset] * 1000UL;
          tmPhase = TM_RUN;
        }
      } else if (tmPhase == TM_RUN) {
        if (single) { tmRemainMs = tmRemainNowMs(); tmPhase = TM_PAUSE; }
        if (dbl) tmPhase = TM_SET;
      } else {   // TM_PAUSE
        if (single) { tmEndMs = millis() + tmRemainMs; tmPhase = TM_RUN; }
        if (dbl) tmPhase = TM_SET;
      }
      toolDrawTimer();
      break;
  }
  display.display();
}

// ═══════════════════════════════════════════════════════════
// DEEP SLEEP (ultra hemat daya, hanya di WIFI_MODE)
// ═══════════════════════════════════════════════════════════
// Minta masuk Deep Sleep. Masuk sebenarnya ditunda sebentar supaya balasan
// web sempat terkirim sebelum WiFi dimatikan.
bool requestDeepSleep() {
  if (deepSleepMode || deepSleepPending) return false;
  if (systemMode != WIFI_MODE || wifiValidating) return false;
  deepSleepPending = true;
  deepSleepReqMs = millis();
  Serial.println("[DEEPSLEEP] Diminta, menunggu balasan terkirim...");
  return true;
}

void updateDeepSleepPending() {
  if (!deepSleepPending) return;
  unsigned long el = millis() - deepSleepReqMs;
  if (el < 1500) return;
  enterDeepSleepMode();
}

void enterDeepSleepMode() {
  deepSleepPending = false;
  if (deepSleepMode) return;
  deepSleepMode = true;              // task latar berhenti akses jaringan

  stopWebServer();
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  wifiStarted = false;
  apActive = false;                  // AP darurat ikut mati
  staInitDone = false;

  deepSleepPrevCpuMhz = getCpuFrequencyMhz();
  setCpuFrequencyMhz(80);            // tanpa WiFi, 80 MHz cukup (APB tetap 80 MHz, jam & I2C aman)

  Serial.println("[DEEPSLEEP] AKTIF: WiFi & web mati. Keluar: 3x klik tombol GPIO 4");
  bootScreen("Deep Sleep AKTIF", "WiFi/Web OFF");
  delay(1200);
}

void exitDeepSleepMode() {
  if (!deepSleepMode) return;
  deepSleepMode = false;

  setCpuFrequencyMhz(deepSleepPrevCpuMhz);
  WiFi.mode(WIFI_STA);
  wifiStabilize();
  WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());
  lastWiFiRetryMs = millis();
  wifiDownSince = 0;
  wifiRetryCount = 0;
  staInitDone = false;
  webStarted = false;                // loop() akan sync NTP, update cuaca & start web setelah WiFi tersambung

  Serial.println("[DEEPSLEEP] NONAKTIF: menyambung WiFi kembali");
  bootScreen("Deep Sleep OFF", "Menyambung WiFi...");
  delay(800);
}

// ═══════════════════════════════════════════════════════════
// END
// ═══════════════════════════════════════════════════════════

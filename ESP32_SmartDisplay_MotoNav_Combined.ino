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
#include <UniversalTelegramBot.h>   // Library: "Universal Telegram Bot Library" by Brian Lough
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "EyeGifs.h"            // data animasi mata (file harus ada satu folder dengan .ino)
#include "WebUI.h"             // halaman dashboard web (INDEX_HTML), satu folder dengan .ino
#include <LittleFS.h>            // penyimpanan PNG jejak sebelum diunggah ke GitHub

// WAJIB di sini (di atas SET_LOOP_TASK_STACK_SIZE): makro itu berubah jadi definisi fungsi, sehingga Arduino
// menaruh semua prototipe fungsi otomatis tepat di atasnya. Tipe yang dipakai di parameter/return fungsi
// harus sudah dideklarasikan sebelum titik itu, kalau tidak -> "error: 'TrkMap' does not name a type".
struct TrkMap { float s, cx, cy, mx, my; };   // skala, pusat jejak (meter), pusat kotak (piksel)

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

// Telegram Bot config
#define BOT_TOKEN      "8966511363:AAFao22yeim9O-fRqZT5OOuqwWYW7UnX87w"
#define OWNER_CHAT_ID  "5987304194"   // dari @userinfobot

// ═══════════════════════════════════════════════════════════
// OBJEK GLOBAL
// ═══════════════════════════════════════════════════════════
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
BluetoothSerial SerialBT;
WebServer server(80);
Preferences prefs;
WiFiClientSecure botSecuredClient;
UniversalTelegramBot bot(BOT_TOKEN, botSecuredClient);

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

// ── Telegram Bot & Alarm Sleep Mode ─────────────────────────
void setupTelegramBot();
void telegramBotTask(void *pvParameters);
void processTelegramIncoming();
void processBotCommand(const String &chatId, const String &text);
void enterAlarmSleepMode();
void exitAlarmSleepMode();
bool parseHHMM(const String &s, int &hOut, int &mOut);
void sendOwnerMessage(const String &text);
void sendReplyMessage(const String &chatId, const String &text);
void handleModeCommand(const String &arg);
void handleBrightnessCommand(const String &arg);
void handleSleepWindowCommand(const String &arg);

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

// Telegram bot / alarm sleep mode
struct BotIncomingMsg {
  char chatId[32];
  char text[220];
};
struct BotOutgoingMsg {
  char chatId[32];
  char text[420];
};

QueueHandle_t botIncomingQueue = NULL;
QueueHandle_t botOutgoingQueue = NULL;
TaskHandle_t  botTaskHandle    = NULL;
bool botTaskStarted = false;

bool alarmSleepMode = false;              // true = mode hemat daya aktif (dipicu /alarm)
const unsigned long botPollIntervalMs = 1500;

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
bool staInitDone = false;                    // sync NTP / cuaca / web / bot sudah dijalankan untuk koneksi ini
bool apClosedMsgPending = false;

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

struct TrkPt { float x, y; };                 // meter dari titik awal (x = timur, y = utara)
const int TRK_MAX = 1500;                     // maks titik di RAM (penuh -> titik dijarangkan separuh)
const float TRK_MAX_ACC = 35.0f;              // fix GPS dengan akurasi lebih buruk dari ini diabaikan (m)
const float TRK_MAX_SPEED = 70.0f;            // lompatan lebih cepat dari ini dianggap GPS error (m/s)

TrkPt *trkPts = nullptr;                      // dialokasikan saat rekam dimulai (hemat RAM statis), dibebaskan setelah selesai
int trkN = 0;
bool trkRec = false;                          // sedang merekam
bool trkScreen = false;                       // layar jejak sedang tampil (mode NAV)
double trkLat0 = 0, trkLon0 = 0, trkCosLat = 1;
float trkDist = 0;                            // total jarak (m)
float trkStep = 5;                            // jarak minimum antar titik (m)
float trkCurX = 0, trkCurY = 0;               // posisi terakhir yang valid
bool trkHaveCur = false;
float trkLastAcc = 0;
uint32_t trkLastFixMs = 0;                    // kapan fix GPS terakhir diterima
uint32_t trkLastAddMs = 0;

// (struct TrkMap dipindah ke atas, sebelum SET_LOOP_TASK_STACK_SIZE - lihat catatan di sana)

// Pengaturan unggah ke GitHub (disimpan di prefs, diisi lewat dashboard)
String ghOwner = "", ghRepo = "", ghBranch = "main", ghDir = "tracks", ghToken = "";
String trkStatus = "";                        // pesan status unggah terakhir
String trkMsg = "";                           // pesan singkat di layar jejak
uint32_t trkMsgUntil = 0;
bool trkFsOk = false;
int trkPending = -1;                          // jumlah PNG menunggu unggah (-1 = belum dipindai)
String trkFiles[8];
int trkFilesN = 0;
unsigned long nextTrkUpAt = 0;
String trkPhone = "";                         // HP penyedia GPS: alamat Bluetooth (AA:BB:CC:DD:EE:FF) atau nama Bluetooth HP
bool trkMaster = false;                       // ESP32 sedang jadi "klien" Bluetooth yang menyambung ke HP
bool trkConnecting = false;
uint32_t trkLastTry = 0;
String nmeaBuf = "";
float nmeaHdop = 3.0f;


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
const EyeGif* eyeCur = NULL;
int eyeCurIdx = EYE_GIF_BLANK;
int eyeLastExpr = -1;
const uint8_t* eyePtr = NULL;
uint16_t eyeFrameIdx = 0;
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
// Matikan WiFi, web server & bot Telegram. OLED + jam tetap jalan,
// data terakhir (cuaca dll) tetap dipakai. Keluar: 3x klik cepat tombol GPIO 4.
volatile bool deepSleepMode = false;
bool deepSleepPending = false;               // menunggu pesan Telegram terakhir terkirim
unsigned long deepSleepReqMs = 0;
uint32_t deepSleepPrevCpuMhz = 240;
volatile bool botNetBusy = false;            // true selama task bot sedang akses jaringan

// ═══════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("[BOOT] ESP32 SmartDisplay + MotoNav start");
  randomSeed(esp_random());

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Sambungkan BUZZER_PIN ke peripheral LEDC. Core ESP32 3.x (IDF5) sudah
  // menghapus ledcSetup/ledcAttachPin (versi channel-based); ganti ledcAttach
  // yang langsung memakai nomor pin.
  ledcAttach(BUZZER_PIN, 2000, 8); // pin, base freq 2000Hz, resolusi 8-bit

  btn.begin();


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

  bootScreen("Booting ESP32...", "Mohon tunggu");
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
    if (WiFi.status() == WL_CONNECTED) {
      setupTelegramBot();
    }
  }

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
  updateBuzzerTask();
  handleRuntimeSerial();
  updateTimeFromNTP();
  if (systemMode == WIFI_MODE) {
    pollButtons();
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
        if ((!webStarted || !staInitDone) && !alarmSleepMode) {
          Serial.println("[WIFI] Connected, starting web server");
          syncNTP();
          fetchWeatherFromAPI();
          if (!webStarted) startWebServer();
          staInitDone = true;
          if (!botTaskStarted) setupTelegramBot();      // bot ikut menyala walau WiFi baru tersambung setelah boot
          if (apClosedMsgPending) {
            apClosedMsgPending = false;
            sendOwnerMessage("📶 WiFi tersambung: " + wifiSSID + "\nIP: " + WiFi.localIP().toString());
          }
        }
      } else {
        staInitDone = false;
        if (wifiDownSince == 0) wifiDownSince = millis();
        if (millis() - lastWiFiRetryMs > wifiRetryIntervalMs) {
          lastWiFiRetryMs = millis();
          wifiRetryCount++;
          if (!apActive && wifiListN > 1 && wifiRetryCount % 3 == 0) {
            Serial.println("[WIFI] Coba WiFi tersimpan lainnya");   // pindah otomatis ke WiFi cadangan
            connectBestSavedWifi(true);
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

    // Ambil pesan yang sudah difetch task Telegram (non-blocking, cepat)
    processTelegramIncoming();

    // Alarm checker HARUS tetap jalan walau alarmSleepMode aktif
    updateAlarmState();
    timerTick();               // timer tetap berjalan & berbunyi walau OLED sedang menampilkan hal lain
    updatePrayerReminder();    // pengingat sholat memakai jadwal tersimpan (jalan juga saat Deep Sleep)
    updatePrayerAlert();

    if (!alarmSleepMode) {
      updateSleepState();
      applyBrightnessForTime();
      updateAutoMode();

      if (webStarted) server.handleClient();
      updateOnlineData();      // prakiraan / jadwal sholat / harga emas / cuaca berkala
      updateTrackUpload();     // unggah PNG jejak yang tertunda ke GitHub

      if (oledSleepState) {
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
    }
    // else: OLED dan web dashboard sengaja dibiarkan mati,
    // hanya menunggu alarm bunyi atau command /bangun dari Telegram
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
  if (oledSleepState && !alarmSleepMode) return;
  if (curHour == alarmHour && curMin == alarmMin && curSec < 3) {
    int ymd = curYear * 10000 + curMon * 100 + curDay;
    if (alarmLastTriggeredYMD != ymd) {
      alarmLastTriggeredYMD = ymd;
      Serial.println("[ALARM] Triggered");
      if (alarmSleepMode) {
        exitAlarmSleepMode();
        sendOwnerMessage("⏰ Alarm bunyi! Sistem kembali normal.");
      }
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
  ghOwner  = prefs.getString("ghOwner", "");
  ghRepo   = prefs.getString("ghRepo", "");
  ghBranch = prefs.getString("ghBranch", "main");
  ghDir    = prefs.getString("ghDir", "tracks");
  ghToken  = prefs.getString("ghToken", "");
  trkPhone = prefs.getString("trkPhone", "");
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
  wDesc = String((const char*)(cur["condition"]["text"] | "N/A"));
  weatherValid = true;

  struct tm t;
  if (getLocalTime(&t, 5)) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    wLastUpdate = String(buf);
  } else {
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
  apClosedMsgPending = true;
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
      wifiSSID = wifiListSsid[i];
      wifiPASS = wifiListPass[i];
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
  cl.setTimeout(10);                          // detik: batas tunggu data saat membaca
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
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

// Dicoba sampai 3x supaya gangguan WiFi sesaat (DNS/handshake timeout) tidak langsung dianggap gagal
bool httpsGetJson(const String &url, JsonDocument &doc, JsonDocument &filter) {
  for (int attempt = 1; attempt <= 3; attempt++) {
    if (WiFi.status() != WL_CONNECTED) return false;
    doc.clear();
    if (httpsGetJsonOnce(url, doc, filter)) return true;
    Serial.printf("[NET] Gagal (percobaan %d/3)\n", attempt);
    delay(700 * attempt);
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
    goldLast = String(buf);
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
void updateOnlineData() {
  if (deepSleepMode || deepSleepPending || wifiValidating || WiFi.status() != WL_CONNECTED) return;
  if (gameActive || toolActive || prayerAlertActive) return;
  if (buzzerSirenActive || buzzerPatternActive || tmBeeping) return;
  if (botNetBusy) return;
  if (ESP.getFreeHeap() < 70000) return;
  if (!timeIsSynced()) return;

  if (dueAt(nextWeatherAt)) {
    if (!fetchWeatherFromAPI()) nextWeatherAt = millis() + 90000UL;
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
  if (!prayerReminderOn || !prayerValid || alarmSleepMode || prayerAlertActive) return;
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
    fetchWeatherFromAPI();
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
  if (WiFi.status() == WL_CONNECTED) {
    setupTelegramBot();
  }
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
    Serial.println("[BT] SerialBT started as MotoNav");
  }

  clearNavState();
  trkScreen = false;
  trkMaster = false;
  systemMode = NAV_MODE;
  saveModeToPrefs(NAV_MODE);
  saveSystemSettings();
  bootScreen("MotoNav Aktif", "BT: MotoNav");
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
  doc["desc"] = wDesc;
  doc["last"] = wLastUpdate;
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
  bool ok = fetchWeatherFromAPI();
  StaticJsonDocument<256> doc;
  doc["ok"] = ok;
  doc["temp"] = wTemp;
  doc["humidity"] = wHumid;
  doc["feels"] = wFeels;
  doc["wind"] = wWind;
  doc["desc"] = wDesc;
  doc["last"] = wLastUpdate;
  String out;
  serializeJson(doc, out);
  server.send(ok ? 200 : 500, "application/json", out);
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
  g["last"] = goldLast;
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
  bool ok = fetchPrayerTimes();
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Gagal ambil jadwal sholat");
}

void handleWebForecastRefresh() {
  bool ok = fetchForecast();
  nextFcAt = millis() + (ok ? 3600000UL : 300000UL);
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Gagal ambil prakiraan");
}

void handleWebGold() {
  if (server.hasArg("adj")) {
    goldAdjPct = constrain(server.arg("adj").toFloat(), -50.0f, 50.0f);
    prefs.putFloat("goldAdj", goldAdjPct);
  }
  bool ok = true;
  if (server.hasArg("refresh") && server.arg("refresh") == "1") {
    ok = fetchGoldPrice();
    nextGoldAt = millis() + (ok ? 900000UL : 180000UL);
  }
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Gagal ambil harga emas");
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
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
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
  server.on("/trk", HTTP_GET, handleWebTrk);
  server.on("/trkcfg", HTTP_POST, handleWebTrkCfg);
  server.on("/trkup", HTTP_GET, handleWebTrkUp);
  server.on("/trkdel", HTTP_GET, handleWebTrkDel);
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
  String desc = wDesc;
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
  display.print(wLastUpdate);

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
  display.print(goldLast);
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
// ANIMASI MATA (GIF)
// Data animasi ada di EyeGifs.h (dibuat dari file .gif oleh make_eye_gifs.py),
// disimpan di flash program sehingga ikut masuk merged.bin.
// Sistem: mata dasar (blank) berulang beberapa detik -> satu animasi GIF acak
// diputar sekali sampai selesai -> kembali ke mata dasar. Begitu seterusnya.
// ═══════════════════════════════════════════════════════════
static void eyeStart(int idx) {
  eyeCur = &EYE_GIFS[idx];
  eyeCurIdx = idx;
  eyePtr = eyeCur->data;
  eyeFrameIdx = 0;
  memset(eyeBuf, 0, sizeof(eyeBuf));   // frame pertama disimpan relatif terhadap layar kosong
}

// Dekode 1 frame (delta XOR + RLE) ke eyeBuf. Return: lama tampil frame (ms).
static uint16_t eyeDecodeNextFrame() {
  const uint8_t* p = eyePtr;
  uint16_t delayMs = (uint16_t)p[0] * 10;
  uint16_t len = (uint16_t)p[1] | ((uint16_t)p[2] << 8);
  p += 3;
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
  eyePtr = end;
  eyeFrameIdx++;
  return delayMs;
}

// Jeda acak (ms) sebelum animasi mata berikutnya, sesuai pengaturan web (min..max detik)
unsigned long eyeRandomIdleMs() {
  long lo = (long)eyeMinSec * 1000L;
  long hi = (long)eyeMaxSec * 1000L;
  if (hi <= lo) return (unsigned long)lo;
  return (unsigned long)random(lo, hi + 1);
}

void drawRobotEyesScreen() {
  unsigned long now = millis();

  if (eyeCur == NULL) {                // pertama kali: mulai dari mata dasar
    eyeStart(EYE_GIF_BLANK);
    eyeIdleUntilMs = now + eyeRandomIdleMs();
    eyeNextMs = 0;
  }

  if (now < eyeNextMs) return;         // belum waktunya frame berikutnya

  if (eyeFrameIdx >= eyeCur->frames) { // animasi yang sedang jalan sudah habis
    if (eyeCurIdx == EYE_GIF_BLANK) {
      if ((long)(now - eyeIdleUntilMs) >= 0) {
        // waktunya ekspresi acak (bukan blank, tidak sama dengan yang barusan)
        int pick;
        do { pick = random(0, EYE_GIF_COUNT); }
        while (pick == EYE_GIF_BLANK || (pick == eyeLastExpr && EYE_GIF_COUNT > 2));
        eyeLastExpr = pick;
        eyeStart(pick);
        Serial.print("[EYES] ");
        Serial.println(EYE_GIFS[pick].name);
      } else {
        eyeStart(EYE_GIF_BLANK);       // ulangi mata dasar
      }
    } else {
      eyeStart(EYE_GIF_BLANK);         // balik ke mata dasar
      eyeIdleUntilMs = now + eyeRandomIdleMs();
    }
  }

  uint16_t delayMs = eyeDecodeNextFrame();
  memcpy(display.getBuffer(), eyeBuf, sizeof(eyeBuf));
  display.display();
  eyeNextMs = now + delayMs;
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
// PELACAK JEJAK
// Titik GPS dari HP (dikirim lewat Bluetooth sebagai "GPS:lat:lng:akurasi") digambar sebagai
// garis di OLED lengkap dengan skala jarak. Setelah selesai, jejak dibuat jadi file PNG dan
// diunggah ke GitHub.
// ═══════════════════════════════════════════════════════════
void trkReset() {
  trkN = 0;
  trkDist = 0;
  trkStep = 5;
  trkCurX = trkCurY = 0;
  trkHaveCur = false;
  trkLastAcc = 0;
  trkLastFixMs = 0;
  trkLastAddMs = 0;
}

// Titik penuh: buang tiap titik ke-2 (bentuk jalan tetap terjaga) dan lebarkan jarak minimum
void trkCompact() {
  int j = 0;
  for (int i = 0; i < trkN; i += 2) trkPts[j++] = trkPts[i];
  if ((trkN - 1) % 2 != 0) trkPts[j++] = trkPts[trkN - 1];
  trkN = j;
  trkStep *= 2;
}

void trkAddFix(double lat, double lon, float acc) {
  trkLastAcc = acc;
  trkLastFixMs = millis();
  if (!trkRec || acc > TRK_MAX_ACC) return;
  if (!trkPts) {
    trkPts = (TrkPt *)malloc(sizeof(TrkPt) * TRK_MAX);
    if (!trkPts) { Serial.println("[TRK] RAM kurang untuk menyimpan titik"); return; }
  }
  uint32_t now = millis();
  if (trkN == 0) {
    trkLat0 = lat;
    trkLon0 = lon;
    trkCosLat = cos(lat * 0.017453292519943295);
    trkPts[0].x = 0;
    trkPts[0].y = 0;
    trkN = 1;
    trkCurX = trkCurY = 0;
    trkHaveCur = true;
    trkLastAddMs = now;
    return;
  }
  float x = (float)((lon - trkLon0) * 111320.0 * trkCosLat);
  float y = (float)((lat - trkLat0) * 110540.0);
  float dx = x - trkPts[trkN - 1].x, dy = y - trkPts[trkN - 1].y;
  float d = sqrtf(dx * dx + dy * dy);
  float dt = (now - trkLastAddMs) / 1000.0f;
  if (d > 50.0f && dt > 0 && d / dt > TRK_MAX_SPEED) return;     // lompatan mustahil -> abaikan
  trkCurX = x;
  trkCurY = y;
  trkHaveCur = true;
  float minStep = trkStep > acc * 0.6f ? trkStep : acc * 0.6f;    // GPS goyang saat diam tidak jadi titik baru
  if (d < minStep) return;
  if (trkN >= TRK_MAX) trkCompact();
  trkPts[trkN++] = { x, y };
  trkDist += d;
  trkLastAddMs = now;
}

void trkFmtDist(char *buf, size_t n, float m) {
  if (m < 1000.0f) snprintf(buf, n, "%d m", (int)(m + 0.5f));
  else snprintf(buf, n, "%.2f km", m / 1000.0f);
}

// Gambar jejak ke kanvas/layar mana pun (OLED atau kanvas PNG) di dalam kotak (ax,ay,aw,ah).
// Mengembalikan skala (piksel per meter) supaya bisa dipakai untuk bar skala.

TrkMap trkFit(int ax, int ay, int aw, int ah, int pad) {
  float minx = trkPts[0].x, maxx = minx, miny = trkPts[0].y, maxy = miny;
  for (int i = 1; i < trkN; i++) {
    if (trkPts[i].x < minx) minx = trkPts[i].x;
    if (trkPts[i].x > maxx) maxx = trkPts[i].x;
    if (trkPts[i].y < miny) miny = trkPts[i].y;
    if (trkPts[i].y > maxy) maxy = trkPts[i].y;
  }
  if (trkHaveCur) {
    if (trkCurX < minx) minx = trkCurX;
    if (trkCurX > maxx) maxx = trkCurX;
    if (trkCurY < miny) miny = trkCurY;
    if (trkCurY > maxy) maxy = trkCurY;
  }
  float w = maxx - minx, h = maxy - miny;
  if (w < 30.0f) w = 30.0f;                    // jangan terlalu zoom saat baru mulai
  if (h < 30.0f) h = 30.0f;
  float sx = (aw - 2 * pad) / w, sy = (ah - 2 * pad) / h;
  TrkMap m;
  m.s = sx < sy ? sx : sy;
  m.cx = (minx + maxx) / 2;
  m.cy = (miny + maxy) / 2;
  m.mx = ax + aw / 2.0f;
  m.my = ay + ah / 2.0f;
  return m;
}
inline int trkSX(const TrkMap &m, float x) { return (int)lroundf(m.mx + (x - m.cx) * m.s); }
inline int trkSY(const TrkMap &m, float y) { return (int)lroundf(m.my - (y - m.cy) * m.s); }

void trkThickLine(Adafruit_GFX &g, int x0, int y0, int x1, int y1, int thick, uint16_t c) {
  g.drawLine(x0, y0, x1, y1, c);
  if (thick >= 2) { g.drawLine(x0 + 1, y0, x1 + 1, y1, c); g.drawLine(x0, y0 + 1, x1, y1 + 1, c); }
  if (thick >= 3) { g.drawLine(x0 - 1, y0, x1 - 1, y1, c); g.drawLine(x0, y0 - 1, x1, y1 - 1, c); }
}

// Panjang bar skala "bulat" (1, 2, 5, 10 ... m) terbesar yang masih muat dalam maxPx piksel
float trkNiceLen(float s, float maxPx) {
  static const float L[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000 };
  float best = L[0];
  for (unsigned i = 0; i < sizeof(L) / sizeof(L[0]); i++) if (L[i] * s <= maxPx) best = L[i];
  return best;
}

void trkLenLabel(char *buf, size_t n, float meters) {
  if (meters < 1000.0f) snprintf(buf, n, "%d m", (int)meters);
  else snprintf(buf, n, "%d km", (int)(meters / 1000.0f));
}

void trkDrawPath(Adafruit_GFX &g, const TrkMap &m, int thick, int markR, uint16_t c) {
  for (int i = 1; i < trkN; i++)
    trkThickLine(g, trkSX(m, trkPts[i - 1].x), trkSY(m, trkPts[i - 1].y), trkSX(m, trkPts[i].x), trkSY(m, trkPts[i].y), thick, c);
  int sx0 = trkSX(m, trkPts[0].x), sy0 = trkSY(m, trkPts[0].y);
  g.drawCircle(sx0, sy0, markR + 1, c);                       // awal = lingkaran kosong
  int ex = trkSX(m, trkHaveCur ? trkCurX : trkPts[trkN - 1].x);
  int ey = trkSY(m, trkHaveCur ? trkCurY : trkPts[trkN - 1].y);
  g.fillCircle(ex, ey, markR, c);                             // posisi sekarang / akhir = lingkaran penuh
}

void trkDrawScaleBar(Adafruit_GFX &g, int x, int y, float s, float maxPx, uint8_t textSize, uint16_t c) {
  float L = trkNiceLen(s, maxPx);
  int len = (int)lroundf(L * s);
  if (len < 2) len = 2;
  int tk = 2 * textSize + 1;
  g.drawFastHLine(x, y, len + 1, c);
  g.drawFastVLine(x, y - tk, 2 * tk + 1, c);
  g.drawFastVLine(x + len, y - tk, 2 * tk + 1, c);
  char lb[12];
  trkLenLabel(lb, sizeof(lb), L);
  g.setTextSize(textSize);
  g.setTextColor(c);
  g.setCursor(x + len + 4 * textSize, y - 3 * textSize);
  g.print(lb);
}

// ── PNG 1-bit tanpa library: deflate "stored" (tanpa kompresi) ──
static uint32_t trkCrcTab[16];
static bool trkCrcInit = false;
uint32_t trkCrc(uint32_t crc, const uint8_t *p, size_t n) {
  if (!trkCrcInit) {
    for (uint32_t i = 0; i < 16; i++) {
      uint32_t c = i;
      for (int k = 0; k < 4; k++) c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
      trkCrcTab[i] = c;
    }
    trkCrcInit = true;
  }
  while (n--) {
    crc ^= *p++;
    crc = trkCrcTab[crc & 15] ^ (crc >> 4);
    crc = trkCrcTab[crc & 15] ^ (crc >> 4);
  }
  return crc;
}
static void trkBe32(uint8_t *b, uint32_t v) { b[0] = v >> 24; b[1] = v >> 16; b[2] = v >> 8; b[3] = v; }

static void trkPngChunk(Print &out, const char *type, const uint8_t *data, uint32_t len) {
  uint8_t h[4];
  trkBe32(h, len);
  out.write(h, 4);
  out.write((const uint8_t *)type, 4);
  uint32_t crc = trkCrc(0xFFFFFFFFUL, (const uint8_t *)type, 4);
  if (len) { out.write(data, len); crc = trkCrc(crc, data, len); }
  trkBe32(h, ~crc);
  out.write(h, 4);
}

// bits = buffer 1-bit (MSB dulu, 1 = piksel menyala), dibalik saat ditulis: latar putih, garis hitam
bool trkWritePng(Print &out, const uint8_t *bits, int w, int h) {
  const uint32_t rb = (w + 7) / 8;
  const uint32_t raw = (uint32_t)h * (rb + 1);
  const uint32_t nblk = (raw + 65534) / 65535;
  const uint32_t idatLen = 2 + raw + 5 * nblk + 4;
  static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
  out.write(sig, 8);
  uint8_t ihdr[13];
  trkBe32(ihdr, w);
  trkBe32(ihdr + 4, h);
  ihdr[8] = 1; ihdr[9] = 0; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;   // 1-bit, grayscale
  trkPngChunk(out, "IHDR", ihdr, 13);

  uint8_t hd[4];
  trkBe32(hd, idatLen);
  out.write(hd, 4);
  uint32_t crc = trkCrc(0xFFFFFFFFUL, (const uint8_t *)"IDAT", 4);
  out.write((const uint8_t *)"IDAT", 4);
  const uint8_t zh[2] = { 0x78, 0x01 };
  out.write(zh, 2);
  crc = trkCrc(crc, zh, 2);

  uint32_t a = 1, b = 0, idx = 0;
  uint8_t buf[256];
  while (idx < raw) {
    uint32_t blk = raw - idx > 65535 ? 65535 : raw - idx;
    uint8_t bh[5] = { (uint8_t)(idx + blk == raw ? 1 : 0), (uint8_t)blk, (uint8_t)(blk >> 8), (uint8_t)~blk, (uint8_t)(~blk >> 8) };
    out.write(bh, 5);
    crc = trkCrc(crc, bh, 5);
    uint32_t done = 0;
    while (done < blk) {
      uint32_t n = blk - done > sizeof(buf) ? sizeof(buf) : blk - done;
      for (uint32_t k = 0; k < n; k++) {
        uint32_t i = idx + done + k;
        uint32_t row = i / (rb + 1), col = i % (rb + 1);
        uint8_t v = col == 0 ? 0 : (uint8_t)~bits[row * rb + col - 1];
        buf[k] = v;
        a = (a + v) % 65521;
        b = (b + a) % 65521;
      }
      out.write(buf, n);
      crc = trkCrc(crc, buf, n);
      done += n;
    }
    idx += blk;
  }
  uint8_t ad[4];
  trkBe32(ad, (b << 16) | a);
  out.write(ad, 4);
  crc = trkCrc(crc, ad, 4);
  trkBe32(hd, ~crc);
  out.write(hd, 4);
  trkPngChunk(out, "IEND", nullptr, 0);
  return true;
}

// Gambar jejak penuh ke kanvas 1-bit (judul, grid per kotak skala, bar skala) lalu tulis sebagai PNG.
// Mengembalikan false (tanpa menulis apa pun) bila memori kanvas tidak cukup.
bool trkRenderPng(Print &out, int w, int h, const char *title) {
  if (trkN < 2) return false;
  GFXcanvas1 cv(w, h);
  if (!cv.getBuffer()) return false;
  const int ts = (w >= 400) ? 2 : 1;           // ukuran teks
  const int thick = (w >= 400) ? 3 : 2;
  const int mr = (w >= 400) ? 6 : 4;
  const int top = 18 * ts + 14, bot = 14 * ts + 18, side = 8;
  cv.fillScreen(0);
  cv.setTextColor(1);
  cv.setTextSize(ts);
  cv.setCursor(side, 6);
  cv.print(title);
  char dl[28], inf[48];
  trkFmtDist(dl, sizeof(dl), trkDist);
  snprintf(inf, sizeof(inf), "Jarak %s | %d titik", dl, trkN);
  cv.setCursor(side, 6 + 9 * ts);
  cv.print(inf);

  int ax = side, ay = top, aw = w - 2 * side, ah = h - top - bot;
  cv.drawRect(ax, ay, aw, ah, 1);
  TrkMap m = trkFit(ax, ay, aw, ah, 12 * ts);

  // Grid putus-putus tiap panjang bar skala (1 kotak = L meter)
  float L = trkNiceLen(m.s, aw / 3.0f);
  float gp = L * m.s;
  if (gp >= 12) {
    float x0 = trkSX(m, 0) - floorf((trkSX(m, 0) - ax) / gp) * gp;
    for (float gx = x0; gx < ax + aw; gx += gp)
      for (int yy = ay + 2; yy < ay + ah - 1; yy += 4) cv.drawPixel((int)gx, yy, 1);
    float y0 = trkSY(m, 0) - floorf((trkSY(m, 0) - ay) / gp) * gp;
    for (float gy = y0; gy < ay + ah; gy += gp)
      for (int xx = ax + 2; xx < ax + aw - 1; xx += 4) cv.drawPixel(xx, (int)gy, 1);
  }
  trkDrawPath(cv, m, thick, mr, 1);

  // Label awal & akhir
  cv.setTextSize(1);
  cv.setCursor(trkSX(m, trkPts[0].x) + mr + 4, trkSY(m, trkPts[0].y) - 4);
  cv.print("START");
  float lx = trkHaveCur ? trkCurX : trkPts[trkN - 1].x, ly = trkHaveCur ? trkCurY : trkPts[trkN - 1].y;
  cv.setCursor(trkSX(m, lx) + mr + 4, trkSY(m, ly) + 2);
  cv.print("FINISH");

  // Bar skala + keterangan kotak
  int by = h - bot / 2 - 2;
  trkDrawScaleBar(cv, side + 2, by, m.s, aw / 3.0f, ts, 1);
  char kb[24], lb[12];
  trkLenLabel(lb, sizeof(lb), L);
  snprintf(kb, sizeof(kb), "1 kotak = %s", lb);
  int16_t bx, bY; uint16_t bw, bh;
  cv.setTextSize(ts);
  cv.getTextBounds(kb, 0, 0, &bx, &bY, &bw, &bh);
  cv.setCursor(w - side - 2 - bw, by - 3 * ts);
  cv.print(kb);
  // Penunjuk utara
  cv.setTextSize(1);
  cv.setCursor(w - side - 14, ay + 6);
  cv.print("U");
  cv.drawFastVLine(w - side - 11, ay + 16, 14, 1);
  cv.drawLine(w - side - 11, ay + 16, w - side - 14, ay + 21, 1);
  cv.drawLine(w - side - 11, ay + 16, w - side - 8, ay + 21, 1);
  return trkWritePng(out, cv.getBuffer(), w, h);
}

// ── Penyimpanan PNG (LittleFS) + unggah ke GitHub ────────────
bool trkFsInit() {
  if (!trkFsOk) trkFsOk = LittleFS.begin(true);
  return trkFsOk;
}

// Hitung PNG yang menunggu diunggah (nama diawali "trk_", berakhiran ".png")
void trkScanPending() {
  trkPending = 0;
  trkFilesN = 0;
  if (!trkFsInit()) return;
  File root = LittleFS.open("/");
  if (!root) return;
  File f = root.openNextFile();
  while (f) {
    String n = f.name();
    int sl = n.lastIndexOf('/');
    if (sl >= 0) n = n.substring(sl + 1);
    if (n.startsWith("trk_") && n.endsWith(".png")) {
      trkPending++;
      if (trkFilesN < 8) trkFiles[trkFilesN++] = n;
    }
    f = root.openNextFile();
  }
}

String trkSanitize(String s) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' || c == '/') o += c;
  }
  while (o.startsWith("/")) o.remove(0, 1);
  while (o.endsWith("/")) o.remove(o.length() - 1);
  return o;
}

String trkMakeName() {
  char b[56];
  if (timeIsSynced()) {
    snprintf(b, sizeof(b), "trk_%04d%02d%02d_%02d%02d%02d_%dm.png", curYear, curMon, curDay, curHour, curMin, curSec, (int)trkDist);
  } else {
    uint32_t seq = prefs.getUInt("trkSeq", 0) + 1;
    prefs.putUInt("trkSeq", seq);
    snprintf(b, sizeof(b), "trk_n%lu_%dm.png", (unsigned long)seq, (int)trkDist);
  }
  return String(b);
}

static const char TRK_B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
size_t trkB64(const uint8_t *in, size_t n, char *out) {      // base64; n kelipatan 3 kecuali potongan terakhir
  size_t o = 0, i = 0;
  for (; i + 2 < n; i += 3) {
    uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
    out[o++] = TRK_B64[v >> 18]; out[o++] = TRK_B64[(v >> 12) & 63];
    out[o++] = TRK_B64[(v >> 6) & 63]; out[o++] = TRK_B64[v & 63];
  }
  if (n - i == 1) {
    uint32_t v = (uint32_t)in[i] << 16;
    out[o++] = TRK_B64[v >> 18]; out[o++] = TRK_B64[(v >> 12) & 63]; out[o++] = '='; out[o++] = '=';
  } else if (n - i == 2) {
    uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
    out[o++] = TRK_B64[v >> 18]; out[o++] = TRK_B64[(v >> 12) & 63]; out[o++] = TRK_B64[(v >> 6) & 63]; out[o++] = '=';
  }
  return o;
}

// Unggah 1 file PNG ke GitHub (Contents API), dikirim bertahap supaya hemat RAM
bool trkUploadOne(const String &name, String &err) {
  if (ghOwner.length() == 0 || ghRepo.length() == 0 || ghToken.length() == 0) {
    err = "Isi pengaturan GitHub dulu";
    return false;
  }
  if (!trkFsInit()) { err = "Penyimpanan gagal"; return false; }
  File f = LittleFS.open("/" + name, "r");
  if (!f) { err = "File tidak ditemukan"; return false; }
  size_t size = f.size();

  String path = (ghDir.length() ? ghDir + "/" : String("")) + name;
  String pre = "{\"message\":\"Tambah jejak " + name + "\",\"branch\":\"" + ghBranch + "\",\"content\":\"";
  const char *suf = "\"}";
  size_t b64len = ((size + 2) / 3) * 4;
  size_t bodyLen = pre.length() + b64len + 2;

  WiFiClientSecure cl;
  cl.setInsecure();
  cl.setTimeout(15);
  if (!cl.connect("api.github.com", 443)) { f.close(); err = "Tidak bisa terhubung ke GitHub"; return false; }

  cl.print("PUT /repos/" + ghOwner + "/" + ghRepo + "/contents/" + path + " HTTP/1.1\r\n");
  cl.print("Host: api.github.com\r\n");
  cl.print("User-Agent: ESP32-SmartDisplay\r\n");
  cl.print("Accept: application/vnd.github+json\r\n");
  cl.print("X-GitHub-Api-Version: 2022-11-28\r\n");
  cl.print("Authorization: Bearer " + ghToken + "\r\n");
  cl.print("Content-Type: application/json\r\n");
  cl.print("Content-Length: " + String((unsigned long)bodyLen) + "\r\n");
  cl.print("Connection: close\r\n\r\n");
  cl.print(pre);

  uint8_t raw[600];                              // 600 byte -> 800 karakter base64
  char enc[804];
  size_t left = size;
  while (left > 0) {
    size_t n = f.read(raw, left > sizeof(raw) ? sizeof(raw) : left);
    if (n == 0) break;
    size_t e = trkB64(raw, n, enc);
    cl.write((const uint8_t *)enc, e);
    left -= n;
    delay(1);
  }
  f.close();
  cl.print(suf);

  uint32_t t0 = millis();
  while (!cl.available() && cl.connected() && millis() - t0 < 20000) delay(20);
  String status = cl.readStringUntil('\n');
  int code = status.length() >= 12 ? status.substring(9, 12).toInt() : 0;
  while (cl.connected() || cl.available()) {     // lewati header
    String l = cl.readStringUntil('\n');
    if (l.length() == 0 || l == "\r") break;
  }
  String body;
  while (cl.available() && body.length() < 500) body += (char)cl.read();
  cl.stop();
  Serial.printf("[TRK] Unggah %s -> HTTP %d\n", name.c_str(), code);

  if (code == 200 || code == 201) return true;
  if (code == 422 && body.indexOf("sha") >= 0) return true;     // sudah ada dari percobaan sebelumnya
  if (code == 401) err = "Token ditolak (401)";
  else if (code == 403) err = "Akses ditolak / batas GitHub (403)";
  else if (code == 404) err = "Repo atau token tidak punya akses (404)";
  else if (code == 0) err = "Tidak ada balasan dari GitHub";
  else err = "GitHub membalas " + String(code);
  int mp = body.indexOf("\"message\":\"");
  if (mp >= 0) {
    int me = body.indexOf('"', mp + 11);
    if (me > mp) err += ": " + body.substring(mp + 11, me);
  }
  return false;
}

// Dipanggil dari loop() (mode WiFi): unggah satu PNG tertunda saat WiFi tersambung dan perangkat idle
void updateTrackUpload() {
  if (deepSleepMode || WiFi.status() != WL_CONNECTED || wifiValidating) return;
  if (gameActive || toolActive || prayerAlertActive || botNetBusy) return;
  if (buzzerSirenActive || buzzerPatternActive || tmBeeping) return;
  if (!dueAt(nextTrkUpAt)) return;
  if (trkPending < 0) trkScanPending();
  if (trkPending == 0) { nextTrkUpAt = millis() + 60000UL; return; }
  if (ESP.getFreeHeap() < 60000) { nextTrkUpAt = millis() + 15000UL; return; }
  if (ghOwner.length() == 0 || ghRepo.length() == 0 || ghToken.length() == 0) {
    trkStatus = "Isi pengaturan GitHub agar jejak terunggah";
    nextTrkUpAt = millis() + 120000UL;
    return;
  }
  String name = trkFiles[0];
  if (name.length() == 0) { trkScanPending(); return; }
  String err;
  trkStatus = "Mengunggah " + name + "...";
  if (trkUploadOne(name, err)) {
    LittleFS.remove("/" + name);
    trkStatus = "Terunggah: " + name;
    String url = "https://github.com/" + ghOwner + "/" + ghRepo + "/blob/" + ghBranch + "/" + (ghDir.length() ? ghDir + "/" : String("")) + name;
    if (botTaskStarted) sendOwnerMessage("📍 Jejak terunggah ke GitHub:\n" + url);
    trkScanPending();
    nextTrkUpAt = millis() + 3000UL;
  } else {
    trkStatus = "Gagal: " + err;
    nextTrkUpAt = millis() + 300000UL;           // coba lagi 5 menit kemudian (atau tekan "Unggah sekarang")
  }
}

// ── Selesai: jejak -> PNG -> LittleFS, lalu pindah ke mode WiFi untuk unggah ──
void trkFinish() {
  trkRec = false;
  if (trkN < 2 || trkDist < 20.0f) {
    trkMsg = "Jejak terlalu pendek";
    trkMsgUntil = millis() + 2500;
    return;
  }
  bootScreen("Membuat PNG...", "Jangan matikan");
  String name = trkMakeName();
  char title[40];
  if (timeIsSynced()) snprintf(title, sizeof(title), "TRACK %02d-%02d-%04d %02d:%02d", curDay, curMon, curYear, curHour, curMin);
  else snprintf(title, sizeof(title), "TRACK");

  bool ok = false;
  if (trkFsInit()) {
    File f = LittleFS.open("/" + name, "w");
    if (f) {
      ok = trkRenderPng(f, 512, 384, title);                 // resolusi penuh
      if (!ok) ok = trkRenderPng(f, 256, 192, title);        // RAM kurang -> resolusi kecil
      f.close();
      if (!ok) LittleFS.remove("/" + name);
    }
  }
  if (!ok) {
    bootScreen("Gagal membuat PNG", "Memori/penyimpanan");
    delay(1800);
    trkMsg = "PNG gagal dibuat";
    trkMsgUntil = millis() + 2500;
    return;
  }
  Serial.println("[TRK] PNG tersimpan: " + name);
  trkReset();
  free(trkPts);
  trkPts = nullptr;
  trkScreen = false;
  trkMaster = false;
  trkStatus = "PNG dibuat, menunggu WiFi: " + name;
  trkPending = -1;
  nextTrkUpAt = 0;
  bootScreen("Jejak disimpan", "Pindah ke WiFi...");
  delay(900);
  switchToWiFiMode();                                         // unggah dilakukan di mode WiFi
}

// ── GPS dari HP lewat Bluetooth (aplikasi "Bluetooth GPS Output" / BlueNMEA): format NMEA ──
// Aplikasi itu jadi "server" serial Bluetooth di HP, jadi ESP32 yang menyambung ke HP.
double nmeaCoord(const String &v, const String &hemi) {
  double x = v.toDouble();
  int deg = (int)(x / 100.0);
  double r = deg + (x - deg * 100.0) / 60.0;
  return (hemi == "S" || hemi == "W") ? -r : r;
}

String nmeaField(const String &s, int n) {
  int idx = 0, start = 0;
  for (int i = 0; i <= (int)s.length(); i++) {
    if (i == (int)s.length() || s[i] == ',' || s[i] == '*') {
      if (idx == n) return s.substring(start, i);
      idx++;
      start = i + 1;
      if (i < (int)s.length() && s[i] == '*') break;
    }
  }
  return "";
}

bool nmeaChecksumOk(const String &s) {
  int en = s.indexOf('*');
  if (en < 0) return true;                       // tanpa checksum: terima
  if (en + 3 > (int)s.length()) return false;
  uint8_t x = 0;
  for (int i = 1; i < en; i++) x ^= (uint8_t)s[i];
  return x == (uint8_t)strtol(s.substring(en + 1, en + 3).c_str(), nullptr, 16);
}

void nmeaParse(const String &s) {
  if (s.length() < 20 || s[0] != '$' || !nmeaChecksumOk(s)) return;
  String type = s.substring(3, 6);
  if (type == "GGA") {                           // $..GGA,waktu,lat,N,lon,E,fix,satelit,hdop,...
    if (nmeaField(s, 6).toInt() <= 0) return;
    float hd = nmeaField(s, 8).toFloat();
    if (hd > 0) nmeaHdop = hd;
    trkAddFix(nmeaCoord(nmeaField(s, 2), nmeaField(s, 3)), nmeaCoord(nmeaField(s, 4), nmeaField(s, 5)), nmeaHdop * 5.0f);
  } else if (type == "RMC") {                    // $..RMC,waktu,A,lat,N,lon,E,kecepatan,arah,tanggal,...
    if (nmeaField(s, 2) != "A") return;
    trkAddFix(nmeaCoord(nmeaField(s, 3), nmeaField(s, 4)), nmeaCoord(nmeaField(s, 5), nmeaField(s, 6)), nmeaHdop * 5.0f);
  }
}

// Ganti peran Bluetooth: on = klien (menyambung ke HP), off = kembali jadi "MotoNav" untuk aplikasi navigasi
void trkBtMaster(bool on) {
  SerialBT.end();
  delay(300);
  if (on) {
    SerialBT.begin("MotoNav-Track", true);
    trkMaster = true;
  } else {
    SerialBT.begin(BT_DEVICE_NAME);
    trkMaster = false;
    clearNavState();
  }
  btEnabled = true;
  trkLastTry = 0;
  nmeaBuf = "";
}

bool trkConnectPhone() {
  unsigned int a[6];
  if (sscanf(trkPhone.c_str(), "%x:%x:%x:%x:%x:%x", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) == 6) {
    uint8_t addr[6];
    for (int i = 0; i < 6; i++) addr[i] = (uint8_t)a[i];
    return SerialBT.connect(addr);               // pakai alamat: tidak perlu HP dalam mode "terlihat"
  }
  return SerialBT.connect(trkPhone);             // pakai nama: HP harus terlihat saat dicari
}

void trkNmeaPoll() {
  if (!trkMaster) return;
  if (!SerialBT.connected()) {
    if (trkPhone.length() == 0) return;
    if (trkLastTry == 0 || millis() - trkLastTry >= 6000) {
      trkConnecting = true;
      drawTrackScreen();                         // tampilkan "Menghubungkan..." sebelum proses (bisa ~10 detik)
      bool ok = trkConnectPhone();
      trkConnecting = false;
      trkLastTry = millis();
      Serial.println(ok ? "[TRK] Terhubung ke HP" : "[TRK] Gagal menyambung ke HP");
    }
    return;
  }
  while (SerialBT.available()) {
    char c = (char)SerialBT.read();
    if (c == '$') nmeaBuf = "$";
    else if (c == '\n' || c == '\r') {
      if (nmeaBuf.length()) { nmeaParse(nmeaBuf); nmeaBuf = ""; }
    } else if (nmeaBuf.length() > 0 && nmeaBuf.length() < 120) nmeaBuf += c;
    else nmeaBuf = "";
  }
}

// ── Layar jejak di OLED (mode NAV) ──
void drawTrackScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.fillRect(0, 0, 128, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print(trkRec ? "REC" : (trkN ? "JEDA" : "JEJAK"));
  if (trkRec && (millis() / 500) % 2 == 0) display.fillCircle(30, 5, 2, SSD1306_BLACK);
  char d[16];
  trkFmtDist(d, sizeof(d), trkDist);
  display.setCursor(126 - (int)strlen(d) * 6, 1);
  display.print(d);
  display.setTextColor(SSD1306_WHITE);

  bool linked = trkMaster && SerialBT.connected();
  bool fresh = trkLastFixMs != 0 && millis() - trkLastFixMs < 10000;
  if (trkN == 0) {
    const char *st;
    if (trkPhone.length() == 0) st = "HP belum diatur (web)";
    else if (trkConnecting) st = "Menghubungkan ke HP...";
    else if (!linked) st = "HP belum terhubung";
    else st = fresh ? "GPS HP: OK" : "Menunggu GPS HP...";
    drawCenteredText(st, 14, 1);
    if (trkRec) {
      drawCenteredText("Mulai bergerak", 34, 1);
    } else {
      drawCenteredText("Klik: mulai rekam", 26, 1);
      drawCenteredText("Tahan: selesai+PNG", 38, 1);
      drawCenteredText("2x klik: kembali", 50, 1);
    }
  } else {
    TrkMap m = trkFit(0, 11, 128, 43, 3);
    trkDrawPath(display, m, 1, 2, SSD1306_WHITE);
    trkDrawScaleBar(display, 3, 60, m.s, 52, 1, SSD1306_WHITE);
    char a[12];
    if (!linked) snprintf(a, sizeof(a), "BT?");
    else if (fresh) snprintf(a, sizeof(a), "acc%d", (int)trkLastAcc);
    else snprintf(a, sizeof(a), "GPS?");
    display.setCursor(126 - (int)strlen(a) * 6, 56);
    display.print(a);
  }
  if (trkMsgUntil && (int32_t)(trkMsgUntil - millis()) > 0) {
    display.fillRect(0, 22, 128, 20, SSD1306_BLACK);
    display.drawRect(0, 22, 128, 20, SSD1306_WHITE);
    drawCenteredText(trkMsg.c_str(), 28, 1);
  }
  display.display();
}

// Tombol di mode NAV (sebelumnya tidak dipakai): 2x klik buka/tutup layar jejak;
// di layar jejak: klik = mulai/jeda rekam, tahan = selesai (hanya saat jeda) -> PNG -> GitHub
void pollNavButtons() {
  btn.update();
  bool dbl = btn.takeDouble(), lng = btn.takeLong(), sgl = btn.takeSingle();
  btn.take(); btn.takeClick(); btn.takeTriple();
  if (!trkScreen) {
    if (dbl) {
      trkScreen = true;
      bootScreen("Pelacak jejak", "Menyiapkan Bluetooth");
      trkBtMaster(true);
    }
    return;
  }
  if (dbl) {
    if (trkRec) { trkMsg = "Jeda dulu (klik)"; trkMsgUntil = millis() + 2000; return; }
    trkScreen = false;
    trkBtMaster(false);
    return;
  }
  if (sgl) {
    trkRec = !trkRec;
    Serial.println(trkRec ? "[TRK] Rekam dimulai" : "[TRK] Rekam dijeda");
  } else if (lng) {
    if (trkRec) { trkMsg = "Klik dulu untuk jeda"; trkMsgUntil = millis() + 2000; }
    else if (trkN < 2) { trkMsg = "Belum ada jejak"; trkMsgUntil = millis() + 2000; }
    else trkFinish();
  }
}

// ── Web: status & pengaturan pelacak ──
void handleWebTrk() {
  if (trkPending < 0) trkScanPending();
  DynamicJsonDocument doc(1536);
  doc["owner"] = ghOwner;
  doc["repo"] = ghRepo;
  doc["branch"] = ghBranch;
  doc["dir"] = ghDir;
  doc["phone"] = trkPhone;
  doc["tokenSet"] = ghToken.length() > 0;
  doc["pending"] = trkPending < 0 ? 0 : trkPending;
  doc["status"] = trkStatus;
  doc["n"] = trkN;
  doc["dist"] = (int)trkDist;
  doc["rec"] = trkRec;
  JsonArray fs = doc.createNestedArray("files");
  for (int i = 0; i < trkFilesN; i++) fs.add(trkFiles[i]);
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleWebTrkCfg() {
  if (server.hasArg("owner")) ghOwner = trkSanitize(server.arg("owner"));
  if (server.hasArg("repo")) ghRepo = trkSanitize(server.arg("repo"));
  if (server.hasArg("branch")) { ghBranch = trkSanitize(server.arg("branch")); if (ghBranch.length() == 0) ghBranch = "main"; }
  if (server.hasArg("dir")) ghDir = trkSanitize(server.arg("dir"));
  if (server.hasArg("phone")) { trkPhone = server.arg("phone"); trkPhone.trim(); }
  if (server.hasArg("token") && server.arg("token").length() > 0) {
    String t = server.arg("token");
    t.trim();
    ghToken = t;
  }
  if (server.hasArg("cleartoken") && server.arg("cleartoken") == "1") ghToken = "";
  prefs.putString("ghOwner", ghOwner);
  prefs.putString("ghRepo", ghRepo);
  prefs.putString("ghBranch", ghBranch);
  prefs.putString("ghDir", ghDir);
  prefs.putString("ghToken", ghToken);
  prefs.putString("trkPhone", trkPhone);
  nextTrkUpAt = 0;
  server.send(200, "text/plain", "OK");
}

void handleWebTrkUp() {
  nextTrkUpAt = 0;
  trkPending = -1;
  trkStatus = "Menunggu giliran unggah...";
  server.send(200, "text/plain", "OK");
}

void handleWebTrkDel() {
  String f = server.arg("f");
  if (!f.startsWith("trk_") || !f.endsWith(".png") || f.indexOf('/') >= 0 || !trkFsInit()) {
    server.send(400, "text/plain", "Nama file salah");
    return;
  }
  LittleFS.remove("/" + f);
  trkScanPending();
  server.send(200, "text/plain", "OK");
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
  display.print("Nama: " BT_DEVICE_NAME);
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
  if (trkScreen) {                       // layar pelacak jejak: ESP32 menyambung ke HP dan membaca NMEA
    trkNmeaPoll();
    static unsigned long lastTrkDraw = 0;
    if (!trkConnecting && millis() - lastTrkDraw >= 250) {
      lastTrkDraw = millis();
      drawTrackScreen();
    }
    return;
  }
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
// TELEGRAM BOT + ALARM-TRIGGERED POWER-SAVE MODE
// ═══════════════════════════════════════════════════════════
void setupTelegramBot() {
  if (botTaskStarted) return;  // jangan buat task dobel kalau dipanggil ulang

  botSecuredClient.setInsecure();  // skip cert validation, hemat memori

  botIncomingQueue = xQueueCreate(6, sizeof(BotIncomingMsg));
  botOutgoingQueue = xQueueCreate(6, sizeof(BotOutgoingMsg));

  if (botIncomingQueue == NULL || botOutgoingQueue == NULL) {
    Serial.println("[BOT] Gagal membuat queue, bot tidak diaktifkan");
    return;
  }

  // Task Telegram dijalankan di CORE 0 (loop() utama tetap di core 1),
  // supaya network call yang blocking (getUpdates/sendMessage) TIDAK PERNAH
  // menghentikan update OLED, jam, dan animasi robot.
  xTaskCreatePinnedToCore(
    telegramBotTask,
    "TelegramBot",
    16384,   // TLS handshake butuh stack besar; 8192 rawan overflow
    NULL,
    1,
    &botTaskHandle,
    0
  );

  botTaskStarted = true;
  Serial.println("[BOT] Telegram bot task dimulai di core 0");
  sendOwnerMessage("🤖 ESP32 SmartDisplay online.\nMode: " + getCurrentModeText());
}

// ═══════════════════════════════════════════════════════════
// TASK TERPISAH — semua network I/O Telegram terjadi di sini,
// TIDAK PERNAH mengakses OLED/I2C secara langsung.
// ═══════════════════════════════════════════════════════════
void telegramBotTask(void *pvParameters) {
  for (;;) {
    if (!deepSleepMode && WiFi.status() == WL_CONNECTED) {
      botNetBusy = true;
      // ── Kirim pesan keluar yang sedang antre ──────────────
      BotOutgoingMsg outMsg;
      while (xQueueReceive(botOutgoingQueue, &outMsg, 0) == pdTRUE) {
        bot.sendMessage(String(outMsg.chatId), String(outMsg.text), "");
      }

      // ── Ambil pesan masuk baru ─────────────────────────────
      int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      while (numNewMessages) {
        for (int i = 0; i < numNewMessages; i++) {
          BotIncomingMsg inMsg;
          bot.messages[i].chat_id.toCharArray(inMsg.chatId, sizeof(inMsg.chatId));
          bot.messages[i].text.toCharArray(inMsg.text, sizeof(inMsg.text));
          xQueueSend(botIncomingQueue, &inMsg, 0);
        }
        numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      }
      botNetBusy = false;
    }
    vTaskDelay(pdMS_TO_TICKS(botPollIntervalMs));
  }
}

// ── Dipanggil dari loop() utama — cuma ambil dari queue, CEPAT & non-blocking
void processTelegramIncoming() {
  if (botIncomingQueue == NULL) return;
  BotIncomingMsg msg;
  while (xQueueReceive(botIncomingQueue, &msg, 0) == pdTRUE) {
    processBotCommand(String(msg.chatId), String(msg.text));
  }
}

void sendOwnerMessage(const String &text) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (botOutgoingQueue == NULL) return;
  sendReplyMessage(String(OWNER_CHAT_ID), text);
}

void sendReplyMessage(const String &chatId, const String &text) {
  if (botOutgoingQueue == NULL) return;
  BotOutgoingMsg outMsg;
  chatId.toCharArray(outMsg.chatId, sizeof(outMsg.chatId));
  text.toCharArray(outMsg.text, sizeof(outMsg.text));
  xQueueSend(botOutgoingQueue, &outMsg, 0);
}

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

void enterAlarmSleepMode() {
  if (alarmSleepMode) return;
  alarmSleepMode = true;
  gameActive = false;
  autoMode = false;
  setDisplayOn(false);
  stopWebServer();
  Serial.println("[ALARM-SLEEP] Mode hemat daya AKTIF");
}

void exitAlarmSleepMode() {
  if (!alarmSleepMode) return;
  alarmSleepMode = false;
  setDisplayOn(true);
  applyBrightnessForTime();
  startWebServer();
  Serial.println("[ALARM-SLEEP] Mode hemat daya NONAKTIF, sistem normal kembali");
}

void handleModeCommand(const String &arg) {
  String a = arg; a.trim(); a.toLowerCase();
  int val = -1;
  if (a == "jam" || a == "clock")        val = 0;
  else if (a == "cuaca" || a == "weather") val = 1;
  else if (a == "wajah" || a == "face")    val = 2;
  else if (a == "info")                    val = 3;
  else if (a == "prakiraan" || a == "forecast") val = 4;
  else if (a == "sholat" || a == "shalat")  val = 5;
  else if (a == "emas" || a == "gold")      val = 6;

  if (val < 0) {
    sendOwnerMessage("Format salah. Pilihan: jam, cuaca, wajah, info, prakiraan, sholat, emas");
    return;
  }
  displayMode = (uint8_t)val;
  autoMode = false;
  saveSystemSettings();
  sendOwnerMessage("🖥️ Mode OLED diganti ke: " + a);
}

void handleBrightnessCommand(const String &arg) {
  String a = arg; a.trim();
  int val = a.toInt();
  if (a.length() == 0 || val < 0 || val > 255) {
    sendOwnerMessage("Format salah. Contoh: /brightness 180 (0-255)");
    return;
  }
  manualBrightness = val;
  autoBrightness = false;
  saveSystemSettings();
  if (!alarmSleepMode) applyOLEDContrast((uint8_t)manualBrightness);
  sendOwnerMessage("💡 Brightness manual diset ke: " + String(val));
}

void handleSleepWindowCommand(const String &arg) {
  // Format: /sleep HH:MM HH:MM  (mulai, selesai)
  String a = arg; a.trim();
  int sp = a.indexOf(' ');
  if (sp < 1) {
    sendOwnerMessage("Format salah. Contoh: /sleep 22:00 06:00");
    return;
  }
  String startStr = a.substring(0, sp);
  String endStr   = a.substring(sp + 1);
  startStr.trim(); endStr.trim();

  int sh, sm, eh, em;
  if (!parseHHMM(startStr, sh, sm) || !parseHHMM(endStr, eh, em)) {
    sendOwnerMessage("Format salah. Contoh: /sleep 22:00 06:00");
    return;
  }
  sleepStartHour = sh;
  sleepStartMin  = sm;
  sleepEndHour   = eh;
  sleepEndMin    = em;
  sleepEnabled   = true;
  saveSystemSettings();
  char buf[20];
  snprintf(buf, sizeof(buf), "%02d:%02d-%02d:%02d", sh, sm, eh, em);
  sendOwnerMessage("😴 Jendela auto-sleep OLED diset: " + String(buf));
}

void processBotCommand(const String &chatIdIn, const String &textIn) {
  String chatId = chatIdIn;

  // Whitelist: abaikan chat selain owner
  if (chatId != OWNER_CHAT_ID) {
    sendReplyMessage(chatId, "⛔ Akses ditolak.");
    return;
  }

  String text = textIn;
  text.trim();
  Serial.println("[BOT] RX: " + text);

  // ── /alarm HH:MM atau /alarm off ───────────────────────
  if (text.startsWith("/alarm")) {
    String arg = text.substring(6);
    arg.trim();

    if (arg.equalsIgnoreCase("off")) {
      alarmEnabled = false;
      saveSystemSettings();
      exitAlarmSleepMode();
      sendOwnerMessage("🔕 Alarm dibatalkan. Sistem kembali normal.");
      return;
    }

    int h, m;
    if (parseHHMM(arg, h, m)) {
      alarmHour = h;
      alarmMin  = m;
      alarmEnabled = true;
      alarmLastTriggeredYMD = -1;  // penting: reset supaya alarm baru bisa bunyi
                                    // walau masih di tanggal yang sama dengan
                                    // alarm sebelumnya yang sudah pernah bunyi
      saveSystemSettings();
      enterAlarmSleepMode();
      char buf[8];
      snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
      sendOwnerMessage("⏰ Alarm diset jam " + String(buf) +
                        ".\n💤 Sistem masuk mode hemat daya (OLED off, dashboard off).\n" +
                        "Kirim /bangun untuk keluar manual, atau /alarm off untuk batalkan.");
    } else {
      sendOwnerMessage("Format salah. Contoh: /alarm 06:30  atau  /alarm off");
    }
    return;
  }

  // ── /bangun — keluar sleep mode & matikan siren jika masih bunyi ───
  if (text.equalsIgnoreCase("/bangun")) {
    bool didSomething = false;
    if (buzzerSirenActive) {
      stopAlarmSiren();
      didSomething = true;
    }
    if (alarmSleepMode) {
      exitAlarmSleepMode();
      didSomething = true;
    }
    if (didSomething) {
      sendOwnerMessage("☀️ Oke, siren dimatikan & keluar dari mode hemat daya. Alarm tetap aktif jika masih diset.");
    } else {
      sendOwnerMessage("Sistem memang sedang tidak dalam mode hemat daya.");
    }
    return;
  }

  // ── /deepsleep — matikan WiFi/web/bot, OLED & jam tetap jalan ───
  if (text.equalsIgnoreCase("/deepsleep")) {
    if (requestDeepSleep()) {
      sendOwnerMessage("💤 Deep sleep diaktifkan. WiFi, web & bot dimatikan.\n"
                       "OLED & jam tetap jalan. Keluar: klik tombol GPIO 4 tiga kali cepat.");
    } else {
      sendOwnerMessage("Deep sleep tidak bisa diaktifkan sekarang (cek: mode hemat daya alarm / validasi WiFi?).");
    }
    return;
  }

  // ── /status ─────────────────────────────────────────
  if (text.equalsIgnoreCase("/status")) {
    String msg = "📊 STATUS\n";
    msg += "Waktu: " + formatTimeString() + "\n";
    msg += "Tanggal: " + formatDateString() + "\n";
    msg += "Mode: " + getCurrentModeText() + "\n";
    msg += "Sleep mode alarm: " + String(alarmSleepMode ? "AKTIF" : "nonaktif") + "\n";
    msg += "Alarm: " + String(alarmEnabled ? getCurrentAlarmText() : "off") + "\n";
    msg += "Suhu: " + String(wTemp, 1) + "C\n";
    msg += "RSSI: " + String(WiFi.RSSI()) + " dBm";
    sendOwnerMessage(msg);
    return;
  }

  // ── /cuaca ──────────────────────────────────────────
  if (text.equalsIgnoreCase("/cuaca")) {
    bool ok = fetchWeatherFromAPI();
    if (ok) {
      sendOwnerMessage("🌤️ " + wDesc + ", " + String(wTemp, 1) + "C, kelembapan " +
                        String(wHumid, 0) + "%, update " + wLastUpdate);
    } else {
      sendOwnerMessage("Gagal ambil data cuaca (cek koneksi WiFi).");
    }
    return;
  }

  // ── /mode jam|cuaca|wajah|info ──────────────────────
  if (text.startsWith("/mode")) {
    if (alarmSleepMode) {
      sendOwnerMessage("Sistem sedang dalam mode hemat daya. Kirim /bangun dulu.");
      return;
    }
    handleModeCommand(text.substring(5));
    return;
  }

  // ── /brightness <0-255> ─────────────────────────────
  if (text.startsWith("/brightness")) {
    handleBrightnessCommand(text.substring(11));
    return;
  }

  // ── /sleep HH:MM HH:MM ───────────────────────────────
  if (text.startsWith("/sleep")) {
    handleSleepWindowCommand(text.substring(6));
    return;
  }

  // ── /start atau /help ────────────────────────────────
  if (text.equalsIgnoreCase("/start") || text.equalsIgnoreCase("/help")) {
    sendOwnerMessage(
      "🤖 ESP32 SmartDisplay Bot\n\n"
      "/alarm HH:MM - set alarm + mode hemat daya\n"
      "/alarm off - batalkan alarm\n"
      "/bangun - matikan siren & keluar mode hemat daya manual\n"
      "/status - info sistem\n"
      "/cuaca - cek cuaca sekarang\n"
      "/mode jam|cuaca|wajah|info|prakiraan|sholat|emas - ganti tampilan OLED\n"
      "/brightness 0-255 - atur kecerahan\n"
      "/sleep HH:MM HH:MM - atur jadwal auto-sleep OLED\n"
      "/deepsleep - matikan WiFi/web/bot (keluar: 3x klik tombol GPIO 4)"
    );
    return;
  }

  // Command tidak dikenal
  sendOwnerMessage("Command tidak dikenal. Kirim /help untuk daftar perintah.");
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
  btn.update();

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

  bool canOpen = !alarmSleepMode && !oledSleepState && !wifiValidating;
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
// web/Telegram sempat terkirim sebelum WiFi dimatikan.
bool requestDeepSleep() {
  if (deepSleepMode || deepSleepPending) return false;
  if (systemMode != WIFI_MODE || wifiValidating || alarmSleepMode) return false;
  deepSleepPending = true;
  deepSleepReqMs = millis();
  Serial.println("[DEEPSLEEP] Diminta, menunggu balasan terkirim...");
  return true;
}

void updateDeepSleepPending() {
  if (!deepSleepPending) return;
  unsigned long el = millis() - deepSleepReqMs;
  if (el < 1500) return;
  bool botIdle = (botOutgoingQueue == NULL) ||
                 (uxQueueMessagesWaiting(botOutgoingQueue) == 0 && !botNetBusy);
  if (botIdle || el > 10000) enterDeepSleepMode();
}

void enterDeepSleepMode() {
  deepSleepPending = false;
  if (deepSleepMode) return;
  deepSleepMode = true;              // task bot berhenti akses jaringan

  stopWebServer();
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  wifiStarted = false;
  apActive = false;                  // AP darurat ikut mati
  staInitDone = false;

  deepSleepPrevCpuMhz = getCpuFrequencyMhz();
  setCpuFrequencyMhz(80);            // tanpa WiFi, 80 MHz cukup (APB tetap 80 MHz, jam & I2C aman)

  Serial.println("[DEEPSLEEP] AKTIF: WiFi, web & bot Telegram mati. Keluar: 3x klik tombol GPIO 4");
  bootScreen("Deep Sleep AKTIF", "WiFi/Web/Bot OFF");
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

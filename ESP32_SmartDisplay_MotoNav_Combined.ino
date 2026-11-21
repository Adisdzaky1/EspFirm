// ═══════════════════════════════════════════════════════════════════════════
//  ESP32 JEJAK TRACKER  (sketch mandiri, dipisah dari ESP32_SmartDisplay_MotoNav_Combined)
//
//  Merekam jalur gerak dari GPS HP (Bluetooth, format NMEA) -> digambar di OLED dengan skala jarak
//  -> saat selesai dibuat file PNG (disimpan di LittleFS) -> diunggah ke repo GitHub lewat WiFi.
//
//  Perangkat : ESP32 DevKit V1, OLED SSD1306 128x64 I2C (SDA 21, SCL 22), SATU tombol di GPIO 4
//  HP        : aplikasi Play Store yang jadi "server" GPS Bluetooth (mis. "Bluetooth GPS Output" / BlueNMEA),
//              HP sudah dipasangkan (pairing) dengan ESP32 dan aplikasinya menyala.
//
//  Tombol    : klik          mulai / jeda rekam
//              tahan         (saat jeda) selesai -> buat PNG -> unggah ke GitHub
//                            (bila belum ada jejak: unggah PNG yang tertunda)
//              2x klik       (saat jeda) buang jejak yang sedang direkam
//
//  Pengaturan di bagian "PENGATURAN" di bawah. Bluetooth dan WiFi dipakai bergantian (tidak bersamaan)
//  supaya RAM cukup. Skema partisi: sama seperti sketch utama (huge_app, ada partisi SPIFFS/LittleFS).
// ═══════════════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BluetoothSerial.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Tipe data jejak: harus di sini (sebelum semua prototipe fungsi yang dibuat otomatis oleh Arduino)
struct TrkPt { float x, y; };                 // meter dari titik awal (x = timur, y = utara)
struct TrkMap { float s, cx, cy, mx, my; };   // skala, pusat jejak (meter), pusat kotak (piksel)

// ─────────────────────────── PENGATURAN ───────────────────────────
#define BT_DEVICE_NAME    "ESP32 Jejak"            // nama Bluetooth ESP32
#define TRK_PHONE_DEFAULT "08:10:93:38:97:24"      // alamat Bluetooth HP penyedia GPS (atau nama HP)

#define WIFI_SSID   "Adis 4G"                             // WiFi untuk sinkron jam & unggah ke GitHub
#define WIFI_PASS   "monalisa"

#define GH_OWNER    "Adisdzaky1"                             // username GitHub pemilik repo
#define GH_REPO     "Ino"                           // nama repo
#define GH_BRANCH   "main"
#define GH_DIR      "tracks"                       // folder tujuan di repo
#define GH_TOKEN    "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"                             // fine-grained token, izin Contents: Read and write (hanya repo ini)
// ───────────────────────────────────────────────────────────────────

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDR     0x3C
#define PIN_BTN       4

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
BluetoothSerial SerialBT;
Preferences prefs;

const int TRK_MAX = 1500;                     // maks titik di RAM (penuh -> titik dijarangkan separuh)
const float TRK_MAX_ACC = 35.0f;              // fix GPS dengan akurasi lebih buruk dari ini diabaikan (m)
const float TRK_MAX_SPEED = 70.0f;            // lompatan lebih cepat dari ini dianggap GPS error (m/s)

TrkPt *trkPts = nullptr;                      // dialokasikan saat rekam dimulai, dibebaskan setelah selesai
int trkN = 0;
bool trkRec = false;                          // sedang merekam
double trkLat0 = 0, trkLon0 = 0, trkCosLat = 1;
float trkDist = 0;                            // total jarak (m)
float trkStep = 5;                            // jarak minimum antar titik (m)
float trkCurX = 0, trkCurY = 0;               // posisi terakhir yang valid
bool trkHaveCur = false;
float trkLastAcc = 0;
uint32_t trkLastFixMs = 0;                    // kapan fix GPS terakhir diterima
uint32_t trkLastAddMs = 0;

String ghOwner = GH_OWNER, ghRepo = GH_REPO, ghBranch = GH_BRANCH, ghDir = GH_DIR, ghToken = GH_TOKEN;
String trkMsg = "";                           // pesan singkat di layar
uint32_t trkMsgUntil = 0;
bool trkFsOk = false;
int trkPending = 0;                           // jumlah PNG menunggu unggah
String trkPhone = TRK_PHONE_DEFAULT;
bool trkMaster = false;                       // ESP32 sedang jadi "klien" Bluetooth yang menyambung ke HP
bool trkConnecting = false;
uint32_t trkLastTry = 0;
String nmeaBuf = "";
float nmeaHdop = 3.0f;

int curYear = 2026, curMon = 1, curDay = 1, curHour = 0, curMin = 0, curSec = 0;

// ── Tombol: satu tombol, klik / tahan / klik ganda ─────────────
const uint16_t MULTI_CLICK_GAP_MS = 600;   // jeda maks antar klik pendek agar dihitung 1 rangkaian
const uint16_t LONG_PRESS_MS      = 700;   // lama tahan agar dianggap "tahan lama"

struct Btn {
  uint8_t pin;
  bool stable, lastRaw, longFired;
  bool longHit, single, dbl;
  uint8_t clickCount;
  uint32_t tChange, pressStart, lastClickMs;
  Btn(uint8_t p) : pin(p), stable(false), lastRaw(false), longFired(false),
                   longHit(false), single(false), dbl(false),
                   clickCount(0), tChange(0), pressStart(0), lastClickMs(0) {}
  void begin() { pinMode(pin, INPUT_PULLUP); }
  void update() {
    uint32_t now = millis();
    bool raw = (digitalRead(pin) == LOW);
    if (raw != lastRaw) { lastRaw = raw; tChange = now; }
    if (now - tChange > 15 && raw != stable) {
      stable = raw;
      if (stable) {                                   // ditekan
        pressStart = now;
        longFired = false;
      } else if (!longFired) {                        // dilepas sebelum batas tahan = klik pendek
        if (now - lastClickMs > MULTI_CLICK_GAP_MS) clickCount = 0;
        lastClickMs = now;
        clickCount++;
      }
    }
    if (!stable && clickCount > 0 && now - lastClickMs > MULTI_CLICK_GAP_MS) {
      if (clickCount == 1) single = true;
      else dbl = true;
      clickCount = 0;
    }
    if (stable && !longFired && now - pressStart >= LONG_PRESS_MS) {   // masih ditahan
      longFired = true;
      longHit = true;
      clickCount = 0;
    }
  }
  bool takeLong()   { bool v = longHit; longHit = false; return v; }
  bool takeSingle() { bool v = single;  single = false;  return v; }
  bool takeDouble() { bool v = dbl;     dbl = false;     return v; }
};
Btn btn(PIN_BTN);

// Tombol dibaca tiap 5 ms oleh task sendiri supaya klik tidak terlewat saat loop() sibuk (sambung HP, buat PNG, unggah)
void btnTask(void *pv) {
  for (;;) {
    btn.update();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ── Utilitas layar & waktu ─────────────────────────────────────
void drawCenteredText(const String &txt, int y, uint8_t size) {
  display.setTextSize(size);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - (int)w) / 2, y);
  display.print(txt);
}

void bootScreen(const String &line1, const String &line2) {
  display.clearDisplay();
  display.fillRect(0, 0, 128, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(5, 3);
  display.print("ESP32 Jejak");
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(5, 22);
  display.print(line1);
  display.setCursor(5, 38);
  display.print(line2);
  display.display();
}

bool timeIsSynced() { return time(nullptr) > 1700000000L; }

void readClock() {
  struct tm t;
  if (!getLocalTime(&t, 10)) return;
  curYear = t.tm_year + 1900; curMon = t.tm_mon + 1; curDay = t.tm_mday;
  curHour = t.tm_hour; curMin = t.tm_min; curSec = t.tm_sec;
}

// ═══════════════════════════════════════════════════════════
// PELACAK JEJAK (kode yang sama persis dengan yang dulu ada di sketch utama)
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

// ── Penyimpanan PNG (LittleFS) ────────────────────────────────
bool trkFsInit() {
  if (!trkFsOk) trkFsOk = LittleFS.begin(true);
  return trkFsOk;
}

// Hitung PNG yang menunggu diunggah (nama diawali "trk_", berakhiran ".png")
void trkScanPending() {
  int cnt = 0;
  if (trkFsInit()) {
    File root = LittleFS.open("/");
    if (root) {
      File f = root.openNextFile();
      while (f) {
        String n = f.name();
        int sl = n.lastIndexOf('/');
        if (sl >= 0) n = n.substring(sl + 1);
        if (n.startsWith("trk_") && n.endsWith(".png")) cnt++;
        f = root.openNextFile();
      }
    }
  }
  trkPending = cnt;
}

// Nama file PNG pertama yang menunggu diunggah ("" bila tidak ada)
String trkFirstPending() {
  if (!trkFsInit()) return "";
  File root = LittleFS.open("/");
  if (!root) return "";
  File f = root.openNextFile();
  while (f) {
    String n = f.name();
    int sl = n.lastIndexOf('/');
    if (sl >= 0) n = n.substring(sl + 1);
    if (n.startsWith("trk_") && n.endsWith(".png")) return n;
    f = root.openNextFile();
  }
  return "";
}

String trkMakeName() {
  char b[56];
  if (timeIsSynced()) {
    readClock();
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

// ── Layar jejak di OLED ──
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
    if (trkPhone.length() == 0) st = "HP belum diatur";
    else if (trkConnecting) st = "Menghubungkan ke HP...";
    else if (!linked) st = "HP belum terhubung";
    else st = fresh ? "GPS HP: OK" : "Menunggu GPS HP...";
    drawCenteredText(st, 14, 1);
    if (trkRec) {
      drawCenteredText("Mulai bergerak", 34, 1);
    } else {
      drawCenteredText("Klik: mulai rekam", 26, 1);
      drawCenteredText("Tahan: selesai+PNG", 38, 1);
      if (trkPending > 0) { char pb[28]; snprintf(pb, sizeof(pb), "%d PNG belum terunggah", trkPending); drawCenteredText(pb, 50, 1); }
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


// ── Bluetooth & WiFi dipakai bergantian ────────────────────────
// on = klien (menyambung ke HP penyedia GPS), off = Bluetooth dimatikan (RAM dibebaskan untuk WiFi/TLS)
void trkBtMaster(bool on) {
  if (SerialBT.connected()) {
    SerialBT.disconnect();
    delay(600);
  }
  SerialBT.end();
  delay(300);
  if (on) {
    SerialBT.begin(BT_DEVICE_NAME, true);
    trkMaster = true;
  } else {
    trkMaster = false;
  }
  trkLastTry = 0;
  nmeaBuf = "";
}

// Sambung WiFi (maks ~20 dtk) dan sinkronkan jam
bool trkWifiUp() {
  if (strlen(WIFI_SSID) == 0) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    char b[24];
    snprintf(b, sizeof(b), "Tunggu %d / 20 dtk", (int)((millis() - t0) / 1000));
    bootScreen("Menyambung WiFi...", b);
    delay(300);
  }
  if (WiFi.status() != WL_CONNECTED) { WiFi.disconnect(true, false); WiFi.mode(WIFI_OFF); return false; }
  configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");   // WIB
  t0 = millis();
  while (!timeIsSynced() && millis() - t0 < 6000) delay(200);
  return true;
}

void trkWifiDown() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}

// Unggah semua PNG tertunda. withBt = Bluetooth sedang menyala dan harus dimatikan dulu lalu dinyalakan lagi.
// syncOnly = tetap sambung WiFi walau tidak ada PNG tertunda (untuk sinkron jam saat boot).
void trkUploadAll(bool withBt, bool syncOnly) {
  trkScanPending();
  if (trkPending == 0 && !syncOnly) return;
  if (trkPending > 0 && (ghOwner.length() == 0 || ghRepo.length() == 0 || ghToken.length() == 0)) {
    trkMsg = "Isi GH_* di sketch"; trkMsgUntil = millis() + 3000;
    return;
  }
  if (withBt) trkBtMaster(false);
  if (!trkWifiUp()) {
    bootScreen("WiFi gagal", "PNG tetap tersimpan");
    delay(2000);
  } else {
    for (int guard = 0; guard < 20; guard++) {
      String name = trkFirstPending();
      if (name.length() == 0) break;
      char b[24];
      snprintf(b, sizeof(b), "Sisa %d file", trkPending);
      bootScreen("Mengunggah ke GitHub", b);
      String err;
      if (trkUploadOne(name, err)) {
        LittleFS.remove("/" + name);
        trkScanPending();
        bootScreen("Terunggah", name.substring(0, 20));
        delay(900);
      } else {
        bootScreen("Gagal unggah", err.substring(0, 21));
        Serial.println("[TRK] Gagal: " + err);
        delay(2500);
        break;
      }
    }
  }
  trkWifiDown();
  trkScanPending();
  if (withBt) trkBtMaster(true);
}

// ── Selesai: jejak -> PNG -> LittleFS -> unggah ke GitHub ──────
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
  bootScreen("Jejak disimpan", name.substring(0, 21));
  delay(900);
  trkUploadAll(true, false);
}

// ── Tombol ─────────────────────────────────────────────────────
void pollButtons() {
  bool dbl = btn.takeDouble(), lng = btn.takeLong(), sgl = btn.takeSingle();
  if (sgl) {
    trkRec = !trkRec;
    Serial.println(trkRec ? "[TRK] Rekam dimulai" : "[TRK] Rekam dijeda");
  } else if (lng) {
    if (trkRec) { trkMsg = "Klik dulu untuk jeda"; trkMsgUntil = millis() + 2000; }
    else if (trkN >= 2) trkFinish();
    else if (trkPending > 0) trkUploadAll(true, false);
    else { trkMsg = "Belum ada jejak"; trkMsgUntil = millis() + 2000; }
  } else if (dbl) {
    if (trkRec) { trkMsg = "Jeda dulu (klik)"; trkMsgUntil = millis() + 2000; }
    else if (trkN > 0) { trkReset(); trkMsg = "Jejak dibuang"; trkMsgUntil = millis() + 2000; }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[BOOT] ESP32 Jejak Tracker");
  prefs.begin("jejak", false);
  btn.begin();
  xTaskCreatePinnedToCore(btnTask, "Btn", 4096, NULL, 2, NULL, 1);

  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[BOOT] OLED tidak ditemukan");
    while (true) { delay(500); }
  }
  display.setTextColor(SSD1306_WHITE);
  bootScreen("Booting...", "Mohon tunggu");

  trkFsInit();
  trkScanPending();
  if (strlen(WIFI_SSID) > 0) {                     // sinkron jam + unggah PNG yang tertunda, lalu WiFi dimatikan
    trkUploadAll(false, true);
  }
  bootScreen("Pelacak jejak", "Menyiapkan Bluetooth");
  trkBtMaster(true);
}

void loop() {
  trkNmeaPoll();                                   // sambung ke HP (bila belum) dan baca NMEA
  pollButtons();
  static unsigned long lastDraw = 0;
  if (!trkConnecting && millis() - lastDraw >= 250) {
    lastDraw = millis();
    drawTrackScreen();
  }
  delay(2);
}

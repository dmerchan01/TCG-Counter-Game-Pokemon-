#include <TFT_eSPI.h>
#include <SPI.h>
#include <Wire.h>
#include <math.h>

// ── Display ───────────────────────────────────
TFT_eSPI tft = TFT_eSPI();

// ── MPU6050 ───────────────────────────────────
#define MPU_ADDR 0x68    // I2C address (AD0=GND). Use 0x69 if AD0=3.3V

// ── Colors ────────────────────────────────────
#define YELLOW  0xFFE0
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define BROWN   0x8200   // digit / status letter color
#define GREEN   0x07E0
#define BLUE    0x001F

// ── Pixel art block sizes ─────────────────────
// Increase to scale up each element.
// Display is 240×240 px — values too large will overflow the circle.
//
//   P  — face elements (eyes, cheeks, nose, mouth)   recommended: 6–10
//   DP — timer digits (MM:SS)                         recommended: 5–8
//   SP — status letters (PAUSED, P1 TURN…)            recommended: 2–4
#define P  8
#define DP 6
#define SP 3

// ── Game timing ───────────────────────────────
// START_SECONDS    — starting time per player in seconds
//                    300=5 min | 600=10 min | 1500=25 min
const int START_SECONDS      = 1500;

// TURN_LIMIT_SECONDS — max seconds allowed per turn before "TURNOVER"
//                      Set very high (e.g. 99999) to disable turn limit.
const int TURN_LIMIT_SECONDS = 90;

// ── Game state ────────────────────────────────
int  playerSeconds[2]     = { START_SECONDS, START_SECONDS };
int  activePlayer         = 0;     // 0 = P1, 1 = P2
bool paused               = false;
bool finished             = false;
bool turnExpired          = false;
int  turnSecondsRemaining = TURN_LIMIT_SECONDS;
unsigned long lastTick    = 0;

// ── Timer cache (avoids redrawing unchanged digits) ───
int  prevMins         = -100;
int  prevSecs         = -100;
bool prevTimerExpired = false;

// ── Status cache ──────────────────────────────
int  prevShownPlayer = -1;
bool prevPaused      = false;
bool prevFinished    = false;
bool prevTurnExpired = false;

// ── IMU raw data ──────────────────────────────
float ax = 0, ay = 0, az = 0;   // accelerometer (g)
float gx = 0, gy = 0, gz = 0;   // gyroscope (°/s)

// ── IMU filtered data (low-pass EMA) ─────────
float filteredAx = 0, filteredAy = 0, filteredAz = 0;
float filteredGx = 0, filteredGy = 0, filteredGz = 0;

// ── IMU tuning ────────────────────────────────
// FLIP_THRESHOLD    — accel X value (g) required to register a side.
//                     Higher = needs more tilt to switch turns.     range: 0.5–0.85
const float FLIP_THRESHOLD    = 0.75;

// CENTER_DEADBAND   — dead zone around ax=0. While |ax| < this value
//                     the device is considered centered (no turn change).
const float CENTER_DEADBAND   = 0.25;

// SHAKE_GYRO_THRESHOLD  — gyro magnitude (°/s) that counts as a shake pulse.
//                         Lower = more sensitive.                   range: 150–350
const float SHAKE_GYRO_THRESHOLD  = 280;

// SHAKE_REQUIRED_PULSES — pulses needed inside SHAKE_WINDOW_MS to confirm a shake.
const int   SHAKE_REQUIRED_PULSES = 6;

// SHAKE_WINDOW_MS   — time window (ms) in which all pulses must occur.
const unsigned long SHAKE_WINDOW_MS    = 1000;

// SHAKE_PULSE_GAP_MS — minimum gap (ms) between consecutive pulses.
//                      Prevents one long shake from counting as multiple pulses.
const unsigned long SHAKE_PULSE_GAP_MS = 150;

// FLIP_DEBOUNCE_MS  — minimum ms between two consecutive turn changes.
//                     Prevents rapid toggling while passing through center.
const unsigned long FLIP_DEBOUNCE_MS     = 1000;

// SHAKE_TOGGLE_LOCK_MS — lock time (ms) after a shake to ignore the next one.
const unsigned long SHAKE_TOGGLE_LOCK_MS = 1800;

// INVERT_PLAYERS    — swap which physical side maps to each player.
//                     false: ax>0 = P1, ax<0 = P2  (default)
//                     true:  ax>0 = P2, ax<0 = P1  (flipped)
//
// ► INITIAL POSITION FIX:
//   If the device always boots with the wrong player → set INVERT_PLAYERS = true.
//   If the tilt axis is wrong (module rotated 90°) → change "filteredAx" to
//   filteredAy or filteredAz inside detectSideFromIMU().
const bool INVERT_PLAYERS = false;

// ALPHA_ACC / ALPHA_GYR — EMA smoothing factor (0 = max smooth, 1 = no filter).
const float ALPHA_ACC = 0.15;
const float ALPHA_GYR = 0.20;

// ── IMU state ─────────────────────────────────
int           stableSide        = 0;
unsigned long lastFlipMs        = 0;
unsigned long lastShakeToggleMs = 0;
unsigned long lastGoodReadMs    = 0;
int           imuFailCount      = 0;
// IMU_FAIL_LIMIT — consecutive failures before attempting I²C bus recovery.
const int IMU_FAIL_LIMIT = 5;

// ── Screen layout — vertical positions (Y) ───
// All values are pixels from the top of the 240×240 display.
//
//   TIMER_Y   — top edge of the MM:SS digit block.
//               Increase to shift the timer lower.                  recommended: 10–40
//   STATUS_Y  — top edge of the status text row.
//               Must be at least TIMER_Y + 7×DP + a few px of gap.  recommended: 55–85
//   STATUS_CX — horizontal center of the status text (120 = center of 240 px display).
#define TIMER_Y   20
#define STATUS_Y  66
#define STATUS_CX 120

// ── Face element positions ────────────────────
// Adjust to move the emoji face up or down.
// Leave room above for the timer and status rows.
//
//   FACE_EYE_Y   — top of the eyes                                  recommended: 80–100
//   FACE_NOSE_Y  — top of the nose dots                             recommended: 130–145
//   FACE_CHEEK_Y — top of the cheek circles                         recommended: 138–155
//   FACE_MOUTH_Y — top of the mouth top row                         recommended: 148–165
#define FACE_EYE_Y    90
#define FACE_NOSE_Y   138
#define FACE_CHEEK_Y  146
#define FACE_MOUTH_Y  154

// ── Pixel font 3×5 — letter indices ──────────
// Used by drawStatusPixel() to compose words from bitmaps.
#define L_P  0
#define L_A  1
#define L_U  2
#define L_S  3
#define L_E  4
#define L_D  5
#define L_T  6
#define L_R  7
#define L_N  8
#define L_1  9
#define L_2  10
#define L_O  11
#define L_V  12

// 3×5 bitmaps: 1 = BROWN (on), 0 = YELLOW (background)
uint8_t letterMap[13][5][3] = {
  { {1,1,0},{1,0,1},{1,1,0},{1,0,0},{1,0,0} }, // P
  { {0,1,0},{1,0,1},{1,1,1},{1,0,1},{1,0,1} }, // A
  { {1,0,1},{1,0,1},{1,0,1},{1,0,1},{0,1,0} }, // U
  { {1,1,1},{1,0,0},{0,1,0},{0,0,1},{1,1,1} }, // S
  { {1,1,1},{1,0,0},{1,1,0},{1,0,0},{1,1,1} }, // E
  { {1,1,0},{1,0,1},{1,0,1},{1,0,1},{1,1,0} }, // D
  { {1,1,1},{0,1,0},{0,1,0},{0,1,0},{0,1,0} }, // T
  { {1,1,0},{1,0,1},{1,1,0},{1,0,1},{1,0,1} }, // R
  { {1,0,1},{1,1,1},{1,1,1},{1,0,1},{1,0,1} }, // N
  { {0,1,0},{1,1,0},{0,1,0},{0,1,0},{1,1,1} }, // 1
  { {1,1,0},{0,0,1},{0,1,0},{1,0,0},{1,1,1} }, // 2
  { {0,1,0},{1,0,1},{1,0,1},{1,0,1},{0,1,0} }, // O
  { {1,0,1},{1,0,1},{1,0,1},{1,0,1},{0,1,0} }, // V
};

// ─────────────────────────────────────────────
// Eye — 6×5 grid, mirrored for the left eye
// (0=bg, 1=black outline, 2=white shine)
// ─────────────────────────────────────────────
void drawEye(int ox, int oy, bool mirror) {
  uint8_t grid[5][6] = {
    { 0, 1, 1, 1, 0, 0 },
    { 1, 2, 2, 1, 1, 0 },  // shine top-left
    { 1, 2, 2, 1, 1, 0 },
    { 1, 1, 1, 1, 1, 0 },
    { 0, 1, 1, 1, 0, 0 },
  };
  uint16_t palette[] = { YELLOW, BLACK, WHITE };

  for (int r = 0; r < 5; r++)
    for (int c = 0; c < 6; c++) {
      int col = mirror ? (5 - c) : c;
      tft.fillRect(ox + c * P, oy + r * P, P, P, palette[grid[r][col]]);
    }
}

// ─────────────────────────────────────────────
// Cheek — 5×5 rounded square (0=bg, 1=red)
// Called twice: right cheek and left cheek
// ─────────────────────────────────────────────
void drawCheek(int ox, int oy) {
  uint8_t grid[5][5] = {
    { 0, 1, 1, 1, 0 },
    { 1, 1, 1, 1, 1 },
    { 1, 1, 1, 1, 1 },
    { 1, 1, 1, 1, 1 },
    { 0, 1, 1, 1, 0 },
  };
  uint16_t palette[] = { YELLOW, RED };

  for (int r = 0; r < 5; r++)
    for (int c = 0; c < 5; c++)
      tft.fillRect(ox + c * P, oy + r * P, P, P, palette[grid[r][c]]);
}

// ─────────────────────────────────────────────
// Nose — 3 dots centered horizontally
// ─────────────────────────────────────────────
void drawNose(int y) {
  tft.fillRect(108, y, P, P, BLACK);  // left dot
  tft.fillRect(116, y, P, P, BLACK);  // center dot
  tft.fillRect(124, y, P, P, BLACK);  // right dot
}

// ─────────────────────────────────────────────
// Mouth — pixel art smile (2 rows)
//   X _ _ X X X _ _ X
//   _ X X _ _ _ X X _
// ─────────────────────────────────────────────
void drawMouth(int y) {
  // top row — corners + center curve
  tft.fillRect(84,  y,     P, P, BLACK);  // far left
  tft.fillRect(108, y,     P, P, BLACK);  // center-left
  tft.fillRect(116, y,     P, P, BLACK);  // center
  tft.fillRect(124, y,     P, P, BLACK);  // center-right
  tft.fillRect(148, y,     P, P, BLACK);  // far right
  // bottom row — smile curve
  tft.fillRect(92,  y + P, P, P, BLACK);  // left outer
  tft.fillRect(100, y + P, P, P, BLACK);  // left inner
  tft.fillRect(132, y + P, P, P, BLACK);  // right inner
  tft.fillRect(140, y + P, P, P, BLACK);  // right outer
}

// ─────────────────────────────────────────────
// Digit — 4×7 pixel art, 0–9
// onColor: BROWN in normal state, RED when time expires
// ─────────────────────────────────────────────
void drawDigitCustomColor(int ox, int oy, int digit, int size, uint16_t onColor) {
  uint8_t digits[10][7][4] = {
    { {0,1,1,0},{1,0,0,1},{1,0,0,1},{1,0,0,1},{1,0,0,1},{1,0,0,1},{0,1,1,0} }, // 0
    { {0,0,1,0},{0,1,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,1,1,1} }, // 1
    { {0,1,1,0},{1,0,0,1},{0,0,0,1},{0,0,1,0},{0,1,0,0},{1,0,0,0},{1,1,1,1} }, // 2
    { {1,1,1,0},{0,0,0,1},{0,0,0,1},{0,1,1,0},{0,0,0,1},{0,0,0,1},{1,1,1,0} }, // 3
    { {0,0,1,0},{0,1,1,0},{1,0,1,0},{1,0,1,0},{1,1,1,1},{0,0,1,0},{0,0,1,0} }, // 4
    { {1,1,1,1},{1,0,0,0},{1,0,0,0},{1,1,1,0},{0,0,0,1},{0,0,0,1},{1,1,1,0} }, // 5
    { {0,1,1,0},{1,0,0,0},{1,0,0,0},{1,1,1,0},{1,0,0,1},{1,0,0,1},{0,1,1,0} }, // 6
    { {1,1,1,1},{0,0,0,1},{0,0,1,0},{0,0,1,0},{0,1,0,0},{0,1,0,0},{0,1,0,0} }, // 7
    { {0,1,1,0},{1,0,0,1},{1,0,0,1},{0,1,1,0},{1,0,0,1},{1,0,0,1},{0,1,1,0} }, // 8
    { {0,1,1,0},{1,0,0,1},{1,0,0,1},{0,1,1,1},{0,0,0,1},{0,0,0,1},{0,1,1,0} }, // 9
  };

  for (int r = 0; r < 7; r++)
    for (int c = 0; c < 4; c++)
      tft.fillRect(ox + c * size, oy + r * size, size, size,
                   digits[digit][r][c] ? onColor : YELLOW);
}

// ─────────────────────────────────────────────
// Colon — 2 dots for MM:SS separator
// ─────────────────────────────────────────────
void drawColonCustom(int ox, int oy, int size, uint16_t color) {
  tft.fillRect(ox, oy + 2 * size, size, size, color);  // top dot
  tft.fillRect(ox, oy + 4 * size, size, size, color);  // bottom dot
}

// ─────────────────────────────────────────────
// Timer — only redraws digits that changed
// expired=true renders everything in RED
// ─────────────────────────────────────────────
void drawMainTimer(int seconds, bool expired) {
  int mins = seconds / 60;
  int secs = seconds % 60;
  uint16_t color = expired ? RED : BROWN;

  if (mins / 10 != prevMins / 10 || expired != prevTimerExpired)
    drawDigitCustomColor(68,  TIMER_Y, mins / 10, DP, color);  // tens of minutes

  if (mins % 10 != prevMins % 10 || expired != prevTimerExpired)
    drawDigitCustomColor(93,  TIMER_Y, mins % 10, DP, color);  // units of minutes

  if (secs / 10 != prevSecs / 10 || expired != prevTimerExpired)
    drawDigitCustomColor(126, TIMER_Y, secs / 10, DP, color);  // tens of seconds

  drawDigitCustomColor(151, TIMER_Y, secs % 10, DP, color);    // units of seconds — always redraws
  drawColonCustom(118, TIMER_Y, DP, color);                    // colon — always redraws

  prevMins         = mins;
  prevSecs         = secs;
  prevTimerExpired = expired;
}

// forceRedrawMainTimer — invalidates cache and redraws everything.
// Call after a player switch or any state change.
void forceRedrawMainTimer(int seconds, bool expired) {
  prevMins         = -100;
  prevSecs         = -100;
  prevTimerExpired = !expired;
  drawMainTimer(seconds, expired);
}

// ─────────────────────────────────────────────
// Letter — single 3×5 pixel font glyph
// idx: one of the L_* constants above
// ─────────────────────────────────────────────
void drawLetter(int ox, int oy, int idx, int size) {
  for (int r = 0; r < 5; r++)
    for (int c = 0; c < 3; c++)
      tft.fillRect(ox + c * size, oy + r * size, size, size,
                   letterMap[idx][r][c] ? BROWN : YELLOW);
}

// wordWidth — total pixel width of a word at block size sz
int wordWidth(int len, int sz) {
  return (4 * len - 1) * sz;  // 3 px per glyph + 1 px gap, minus last gap
}

// drawWord — renders an array of L_* indices as a word
void drawWord(const int* seq, int len, int ox, int oy, int sz) {
  for (int i = 0; i < len; i++)
    drawLetter(ox + i * 4 * sz, oy, seq[i], sz);
}

// ─────────────────────────────────────────────
// Status — centered text row at STATUS_Y
//   "P1 TURN" / "P2 TURN"  — active player's turn
//   "PAUSED"               — game is paused
//   "TURNOVER"             — turn time ran out
//   "P1" / "P2"            — game over (who lost)
// force=true redraws even if nothing changed
// ─────────────────────────────────────────────
void drawStatusPixel(bool force = false) {
  if (!force &&
      prevShownPlayer == activePlayer &&
      prevPaused      == paused       &&
      prevFinished    == finished     &&
      prevTurnExpired == turnExpired) return;

  tft.fillRect(STATUS_CX - 80, STATUS_Y, 160, 5 * SP, YELLOW);

  if (finished) {
    int who = (playerSeconds[0] == 0) ? L_1 : L_2;
    const int seq[] = { L_P, who };
    drawWord(seq, 2, STATUS_CX - wordWidth(2, SP) / 2, STATUS_Y, SP);

  } else if (paused) {
    const int seq[] = { L_P, L_A, L_U, L_S, L_E, L_D };
    drawWord(seq, 6, STATUS_CX - wordWidth(6, SP) / 2, STATUS_Y, SP);

  } else if (turnExpired) {
    const int seq[] = { L_T, L_U, L_R, L_N, L_O, L_V, L_E, L_R };
    drawWord(seq, 8, STATUS_CX - wordWidth(8, SP) / 2, STATUS_Y, SP);

  } else {
    int who = (activePlayer == 0) ? L_1 : L_2;
    const int w1seq[] = { L_P, who };
    const int w2seq[] = { L_T, L_U, L_R, L_N };
    int gap    = 2 * SP;
    int startX = STATUS_CX - (wordWidth(2, SP) + gap + wordWidth(4, SP)) / 2;
    drawWord(w1seq, 2, startX, STATUS_Y, SP);
    drawWord(w2seq, 4, startX + wordWidth(2, SP) + gap, STATUS_Y, SP);
  }

  prevShownPlayer = activePlayer;
  prevPaused      = paused;
  prevFinished    = finished;
  prevTurnExpired = turnExpired;
}

// ─────────────────────────────────────────────
// Static elements — full screen clear + face
// Only called once in setup()
// ─────────────────────────────────────────────
void drawFaceAndStaticElements() {
  tft.fillScreen(YELLOW);
  drawEye(150, FACE_EYE_Y, false);   // right eye
  drawEye(42,  FACE_EYE_Y, true);    // left eye (mirrored)
  drawNose(FACE_NOSE_Y);
  drawCheek(175, FACE_CHEEK_Y);      // right cheek
  drawCheek(25,  FACE_CHEEK_Y);      // left cheek
  drawMouth(FACE_MOUTH_Y);
}

// ─────────────────────────────────────────────
// MPU6050 — wake from sleep (PWR_MGMT_1 = 0)
// ─────────────────────────────────────────────
void wakeMPU() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);  // disable sleep, use internal oscillator
  Wire.endTransmission(true);
}

// ─────────────────────────────────────────────
// MPU6050 — set accelerometer and gyro full-scale ranges
//   ACCEL_CONFIG 0x1C = 0x00 → ±2 g    (16384 LSB/g)
//   GYRO_CONFIG  0x1B = 0x00 → ±250°/s  (131 LSB/°/s)
//
// ► To use ±4 g: write 0x08 and divide rawA by 8192.0 in readMPU()
// ─────────────────────────────────────────────
void configureMPU() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C); Wire.write(0x00);  // ±2 g
  Wire.endTransmission(true);

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1B); Wire.write(0x00);  // ±250 °/s
  Wire.endTransmission(true);
}

// ─────────────────────────────────────────────
// MPU6050 — read 14 bytes: accel, temp (discarded), gyro
// Returns false on bus error or insufficient bytes
// ─────────────────────────────────────────────
bool readMPU() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);  // ACCEL_XOUT_H — burst read start
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom(MPU_ADDR, 14, true);
  if (Wire.available() < 14) return false;

  int16_t rawAx = (Wire.read() << 8) | Wire.read();
  int16_t rawAy = (Wire.read() << 8) | Wire.read();
  int16_t rawAz = (Wire.read() << 8) | Wire.read();
  Wire.read(); Wire.read();  // temperature — discarded
  int16_t rawGx = (Wire.read() << 8) | Wire.read();
  int16_t rawGy = (Wire.read() << 8) | Wire.read();
  int16_t rawGz = (Wire.read() << 8) | Wire.read();

  ax = rawAx / 16384.0f;  ay = rawAy / 16384.0f;  az = rawAz / 16384.0f;
  gx = rawGx /   131.0f;  gy = rawGy /   131.0f;  gz = rawGz /   131.0f;
  return true;
}

// ─────────────────────────────────────────────
// MPU6050 — reinitialise I²C bus and sensor
// Called automatically after IMU_FAIL_LIMIT consecutive errors
// ─────────────────────────────────────────────
void recoverMPU() {
  Wire.begin();
  Wire.setClock(100000);  // 100 kHz. Raise to 400000 for short, clean cables.
  delay(5);
  wakeMPU(); configureMPU(); delay(10);
  for (int i = 0; i < 5; i++) {
    if (readMPU()) {
      filteredAx = ax; filteredAy = ay; filteredAz = az;
      filteredGx = gx; filteredGy = gy; filteredGz = gz;
      lastGoodReadMs = millis(); imuFailCount = 0; break;
    }
    delay(10);
  }
}

// ─────────────────────────────────────────────
// IMU — read + apply EMA low-pass filter
// Returns false on read failure (recovery triggered internally)
// ─────────────────────────────────────────────
bool updateIMU() {
  if (!readMPU()) {
    imuFailCount++;
    if (imuFailCount >= IMU_FAIL_LIMIT || millis() - lastGoodReadMs > 500)
      recoverMPU(), imuFailCount = 0;
    return false;
  }
  lastGoodReadMs = millis(); imuFailCount = 0;

  filteredAx = (1.0f - ALPHA_ACC) * filteredAx + ALPHA_ACC * ax;
  filteredAy = (1.0f - ALPHA_ACC) * filteredAy + ALPHA_ACC * ay;
  filteredAz = (1.0f - ALPHA_ACC) * filteredAz + ALPHA_ACC * az;
  filteredGx = (1.0f - ALPHA_GYR) * filteredGx + ALPHA_GYR * gx;
  filteredGy = (1.0f - ALPHA_GYR) * filteredGy + ALPHA_GYR * gy;
  filteredGz = (1.0f - ALPHA_GYR) * filteredGz + ALPHA_GYR * gz;
  return true;
}

// gyroMagnitude — magnitude of the filtered gyro vector (°/s)
float gyroMagnitude() {
  return sqrt(filteredGx*filteredGx + filteredGy*filteredGy + filteredGz*filteredGz);
}

// ─────────────────────────────────────────────
// detectSideFromIMU — which side is the device tilted to?
// Returns: +1 (positive X side), -1 (negative X side), 0 (centered)
//
// ► AXIS FIX: if the module is mounted rotated 90°, change
//   "float sideValue = filteredAx" to filteredAy or filteredAz.
// ─────────────────────────────────────────────
int detectSideFromIMU() {
  float sideValue = filteredAx;  // ← change axis here if needed

  if (sideValue >  FLIP_THRESHOLD)  return +1;
  if (sideValue < -FLIP_THRESHOLD)  return -1;
  if (sideValue > -CENTER_DEADBAND && sideValue < CENTER_DEADBAND) return 0;
  return stableSide;  // between deadband and threshold — hold last side
}

// ─────────────────────────────────────────────
// detectShakeConfirmed — counts high-gyro pulses within a time window
// Returns true once SHAKE_REQUIRED_PULSES are reached
// ─────────────────────────────────────────────
bool detectShakeConfirmed() {
  static int           shakeCount   = 0;
  static unsigned long firstShakeMs = 0;
  static unsigned long lastPulseMs  = 0;

  float gMag = gyroMagnitude();
  unsigned long now = millis();

  if (shakeCount > 0 && now - firstShakeMs > SHAKE_WINDOW_MS)
    shakeCount = 0, firstShakeMs = 0, lastPulseMs = 0;

  if (gMag >= SHAKE_GYRO_THRESHOLD && now - lastPulseMs > SHAKE_PULSE_GAP_MS) {
    lastPulseMs = now;
    if (shakeCount == 0) firstShakeMs = now;
    if (++shakeCount >= SHAKE_REQUIRED_PULSES) {
      shakeCount = 0; firstShakeMs = 0; lastPulseMs = 0;
      return true;
    }
  }
  return false;
}

// sideToPlayer — converts +1/-1 side to player index, respecting INVERT_PLAYERS
int sideToPlayer(int side) {
  return INVERT_PLAYERS ? (side == +1 ? 1 : 0)
                        : (side == +1 ? 0 : 1);
}

// ─────────────────────────────────────────────
// startNewTurnForActivePlayer — resets turn timer and redraws UI
// ─────────────────────────────────────────────
void startNewTurnForActivePlayer() {
  turnSecondsRemaining = TURN_LIMIT_SECONDS;
  turnExpired = false;
  lastTick    = millis();  // prevents phantom time accumulation
  forceRedrawMainTimer(playerSeconds[activePlayer], false);
  drawStatusPixel(true);
}

// ─────────────────────────────────────────────
// switchTurnFromSide — changes active player if side differs, with debounce
// ─────────────────────────────────────────────
void switchTurnFromSide(int newSide) {
  if (newSide == 0) return;
  if (millis() - lastFlipMs < FLIP_DEBOUNCE_MS) return;
  int newPlayer = sideToPlayer(newSide);
  if (newPlayer != activePlayer) {
    activePlayer = newPlayer;
    lastFlipMs   = millis();
    startNewTurnForActivePlayer();
  }
  stableSide = newSide;
}

// ─────────────────────────────────────────────
// togglePause — pause / resume. No-op if game is finished.
// ─────────────────────────────────────────────
void togglePause() {
  if (finished) return;
  paused   = !paused;
  lastTick = millis();  // prevents time jump on resume
  drawStatusPixel(true);
}

// ─────────────────────────────────────────────
// handleIMUControls — called every loop iteration
//   1. Check for shake → toggle pause
//   2. Check for tilt  → switch turn
// ─────────────────────────────────────────────
void handleIMUControls() {
  if (!updateIMU()) return;

  if (millis() - lastShakeToggleMs > SHAKE_TOGGLE_LOCK_MS) {
    if (detectShakeConfirmed()) {
      togglePause();
      lastShakeToggleMs = millis();
      return;  // skip tilt check this cycle
    }
  }

  if (paused || finished) return;

  int side = detectSideFromIMU();
  if      (side != 0 && side != stableSide) switchTurnFromSide(side);
  else if (side != 0)                       stableSide = side;
}

// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(2);  // 0–3, steps of 90°. Change if display appears rotated.

  Wire.begin();
  Wire.setClock(100000);
  wakeMPU();
  configureMPU();

  drawFaceAndStaticElements();

  // Warm-up reads to seed the EMA filter
  for (int i = 0; i < 10; i++) {
    if (readMPU()) {
      filteredAx = ax; filteredAy = ay; filteredAz = az;
      filteredGx = gx; filteredGy = gy; filteredGz = gz;
      break;
    }
    delay(20);
  }

  // Determine starting player from physical orientation at boot.
  // If the device is flat (side=0), P1 is assigned by default.
  // ► Always boots with wrong player? → flip INVERT_PLAYERS.
  int initialSide = detectSideFromIMU();
  if (initialSide == 0) { stableSide = +1; activePlayer = 0; }
  else                  { stableSide = initialSide; activePlayer = sideToPlayer(initialSide); }

  turnSecondsRemaining = TURN_LIMIT_SECONDS;
  turnExpired          = false;

  forceRedrawMainTimer(playerSeconds[activePlayer], false);
  drawStatusPixel(true);
  drawColonCustom(118, TIMER_Y, DP, BROWN);  // drawn once; also redrawn inside drawMainTimer

  lastGoodReadMs = millis();
  imuFailCount   = 0;
  lastTick       = millis();
}

// ─────────────────────────────────────────────
void loop() {
  handleIMUControls();

  // One-second tick — only while running, not paused, not expired
  if (!paused && !finished && !turnExpired && millis() - lastTick >= 1000) {
    lastTick += 1000;  // fixed increment prevents drift

    if (playerSeconds[activePlayer] > 0) {
      playerSeconds[activePlayer]--;
      drawMainTimer(playerSeconds[activePlayer], false);
    }

    if (turnSecondsRemaining > 0) turnSecondsRemaining--;

    // Game over — active player ran out of total time
    if (playerSeconds[activePlayer] <= 0) {
      playerSeconds[activePlayer] = 0;
      finished = true;
      forceRedrawMainTimer(0, true);  // red digits
      drawStatusPixel(true);
    }

    // Turn expired — freeze clock, show TURNOVER in red
    if (turnSecondsRemaining <= 0 && !finished) {
      turnSecondsRemaining = 0;
      turnExpired          = true;
      lastTick             = millis();  // freeze reference so time doesn't rush on next turn
      forceRedrawMainTimer(playerSeconds[activePlayer], true);  // red digits
      drawStatusPixel(true);
    }
  }

  delay(5);  // ~200 Hz loop — sufficient for IMU polling
}

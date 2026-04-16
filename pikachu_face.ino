#include <TFT_eSPI.h>
#include <SPI.h>

// ── Display ──────────────────────────────────
TFT_eSPI tft = TFT_eSPI();

// ── Colors ───────────────────────────────────
#define YELLOW  0xFFE0
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define BROWN   0x8200

// ── Pixel art block size (px) ─────────────────
#define P 8

// ─────────────────────────────────────────────
// Eye — 6x5 grid, mirrored for left eye
// (0=bg, 1=black, 2=white shine)
// ─────────────────────────────────────────────
void drawEye(int ox, int oy, bool mirror) {
  uint8_t grid[5][6] = {
    { 0, 1, 1, 1, 0, 0 },
    { 1, 2, 2, 1, 1, 0 },  // shine on top-left
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
// Cheek — 5x5 rounded square (0=bg, 1=red)
// Called twice: left and right cheek
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
// Mouth — pixel art smile
//   X _ _ X X X _ _ X
//   _ X X _ _ _ X X _
// ─────────────────────────────────────────────
void drawMouth(int y) {
  // top row — corners + center curve
  tft.fillRect(84,  y,     P, P, BLACK);  // far left
  tft.fillRect(108, y,     P, P, BLACK);  // center left
  tft.fillRect(116, y,     P, P, BLACK);  // center
  tft.fillRect(124, y,     P, P, BLACK);  // center right
  tft.fillRect(148, y,     P, P, BLACK);  // far right

  // bottom row — cheek curves
  tft.fillRect(92,  y + P, P, P, BLACK);  // left outer
  tft.fillRect(100, y + P, P, P, BLACK);  // left inner
  tft.fillRect(132, y + P, P, P, BLACK);  // right inner
  tft.fillRect(140, y + P, P, P, BLACK);  // right outer
}

// ─────────────────────────────────────────────
void setup() {
  tft.init();
  tft.setRotation(2);
  tft.fillScreen(YELLOW);

  drawEye(150, 90, false);  // right eye
  drawEye(42,  90, true);   // left eye (mirrored)
  drawNose(138);
  drawCheek(175, 146);      // right cheek
  drawCheek(25,  146);      // left cheek
  drawMouth(154);
}

void loop() {}
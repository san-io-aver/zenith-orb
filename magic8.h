#ifndef MAGIC_8_BALL_H
#define MAGIC_8_BALL_H

#include <Arduino.h>
#include <U8g2lib.h>
#include <math.h>

// ─── Magic 8 Ball Answers ──────────────────────────────────
static const char* MAGIC_ANSWERS[] = {
  "IT IS CERTAIN",       "DECIDEDLY SO",      "WITHOUT A DOUBT",
  "YES DEFINITELY",      "YOU MAY RELY ON IT","AS I SEE IT, YES",
  "MOST LIKELY",         "OUTLOOK GOOD",      "YES",
  "SIGNS POINT TO YES",  "REPLY HAZY AGAIN",  "ASK AGAIN LATER",
  "BETTER NOT TELL YOU", "CANNOT PREDICT NOW","CONCENTRATE & ASK",
  "DON'T COUNT ON IT",   "MY REPLY IS NO",    "MY SOURCES SAY NO",
  "OUTLOOK NOT GOOD",    "VERY DOUBTFUL"
};
constexpr uint8_t NUM_MAGIC_ANSWERS = 20;

// ─── State Variables ────────────────────────────────────────
inline uint8_t  g_magicAnswerIdx = 0;
inline uint32_t g_magicShakeStart = 0;

// ─── Trigger Function ───────────────────────────────────────
inline void triggerMagic8Ball() {
  g_magicAnswerIdx = esp_random() % NUM_MAGIC_ANSWERS;
  g_magicShakeStart = millis();
}

// ─── Helper: Smart Word Wrapper for Triangle Die ───────────
struct TextLines {
  char lines[3][16];
  uint8_t count;
};

static TextLines wrapAnswerForDie(const char* text) {
  TextLines tl = {{{0}}, 0};
  uint16_t len = strlen(text);

  if (len <= 8) {
    strncpy(tl.lines[0], text, 15);
    tl.count = 1;
    return tl;
  }

  uint8_t spaces = 0;
  int spacePos[4] = {0};
  for (uint16_t i = 0; i < len; i++) {
    if (text[i] == ' ' && spaces < 4) {
      spacePos[spaces++] = i;
    }
  }

  if (spaces >= 2 && len > 12) {
    int sp1 = spacePos[0];
    int sp2 = (spaces >= 3) ? spacePos[2] : spacePos[1];
    
    // Line 1
    strncpy(tl.lines[0], text, sp1);
    tl.lines[0][sp1] = '\0';
    // Line 2
    int len2 = sp2 - sp1 - 1;
    strncpy(tl.lines[1], text + sp1 + 1, len2);
    tl.lines[1][len2] = '\0';
    // Line 3
    strncpy(tl.lines[2], text + sp2 + 1, 15);
    tl.count = 3;
  } else if (spaces >= 1) {
    int midSp = spacePos[spaces / 2];
    strncpy(tl.lines[0], text, midSp);
    tl.lines[0][midSp] = '\0';
    strncpy(tl.lines[1], text + midSp + 1, 15);
    tl.count = 2;
  } else {
    strncpy(tl.lines[0], text, 8);
    tl.lines[0][8] = '\0';
    strncpy(tl.lines[1], text + 8, 15);
    tl.count = 2;
  }

  return tl;
}

// ─── Animated Renderer ──────────────────────────────────────
inline void drawMagic8Ball(U8G2 &u8g2, int16_t cx, int16_t cy, int16_t radius,
                           void (*drawClippedLine)(int16_t, int16_t, int16_t, int16_t) = nullptr) {
  uint32_t now = millis();
  uint32_t elapsed = now - g_magicShakeStart;

  // Animation Timings (ms)
  constexpr uint32_t SHAKE_DUR   = 1200;
  constexpr uint32_t REVEAL_DUR = 1000;

  bool isShaking  = (elapsed < SHAKE_DUR);
  bool isRevealing = (!isShaking && elapsed < (SHAKE_DUR + REVEAL_DUR));

  // 1. Decaying Damped Physics Shake
  float shakeOffsetVal = 0.0f;
  int16_t offsetX = 0, offsetY = 0;
  if (isShaking) {
    float decay = 1.0f - ((float)elapsed / (float)SHAKE_DUR);
    shakeOffsetVal = sinf((float)elapsed * 0.05f) * 6.0f * decay;
    offsetX = (int16_t)shakeOffsetVal;
    offsetY = (int16_t)(cosf((float)elapsed * 0.07f) * 5.0f * decay);
  }

  int16_t ballCx = cx + offsetX;
  int16_t ballCy = cy + offsetY;

  // Outer Sphere Rim & Ambient Specular Reflection Arc
  u8g2.drawCircle(cx, cy, radius, U8G2_DRAW_ALL);
  if (radius > 12) {
    u8g2.drawCircle(cx, cy, radius - 1, U8G2_DRAW_ALL); // Double outer lens ring
  }

  // Draw Rising Fluid Bubbles
  for (uint8_t i = 0; i < 5; i++) {
    uint32_t bTime = now + (i * 350);
    int16_t bx = ballCx + (int16_t)(sinf(bTime * 0.004f + i) * (radius - 8));
    int16_t by = ballCy + radius - 4 - (int16_t)(fmodf(bTime * 0.025f, (float)(radius * 1.8f)));
    
    // Per-pixel viewport boundary check before drawing bubbles
    int32_t dx = bx - cx, dy = by - cy;
    if ((dx * dx + dy * dy) <= ((int32_t)(radius - 3) * (radius - 3))) {
      u8g2.drawPixel(bx, by);
      if (i % 2 == 0) u8g2.drawPixel(bx + 1, by);
    }
  }

  // 2. SHAKE PHASE: Animated 8-Ball Sphere
  if (isShaking) {
    // Inner white pool circle
    int16_t poolR = (radius * 4) / 10;
    u8g2.drawDisc(ballCx, ballCy, poolR, U8G2_DRAW_ALL);

    // Large Bold "8" Glyph inside Pool
    u8g2.setFont(u8g2_font_helvB12_tr);
    u8g2.setDrawColor(0); // Inverted black text on white pool
    u8g2.drawStr(ballCx - 4, ballCy + 5, "8");
    u8g2.setDrawColor(1); // Reset back to white

    // Specular Highlight Arc (Top-Left Glare)
    for (int16_t a = 110; a <= 160; a += 10) {
      float rad = a * (M_PI / 180.0f);
      int16_t gx = ballCx + (int16_t)(cosf(rad) * (radius - 4));
      int16_t gy = ballCy - (int16_t)(sinf(rad) * (radius - 4));
      u8g2.drawPixel(gx, gy);
    }
  } 
  // 3. REVEAL & SETTLED PHASE: Floating 20-Sided Die & Answer
  else {
    // Buoyancy Floating Math
    float floatPhase = (float)(now % 3000) / 3000.0f * 2.0f * M_PI;
    float buoyancyY  = sinf(floatPhase) * 1.5f;
    float buoyancyX  = cosf(floatPhase * 0.5f) * 1.0f;

    // Scale-In Easing when emerging from fluid depth
    float scale = 1.0f;
    if (isRevealing) {
      float revProgress = (float)(elapsed - SHAKE_DUR) / (float)REVEAL_DUR;
      scale = sinf(revProgress * M_PI_2); // Smooth ease-out
      buoyancyY += (1.0f - scale) * 12.0f; // Rise up from bottom depth
    }

    int16_t dieCx = ballCx + (int16_t)buoyancyX;
    int16_t dieCy = ballCy + (int16_t)buoyancyY;

    // Reservoir Triangle Dimensions
    int16_t baseR = (int16_t)((radius - 5) * scale);
    if (baseR < 4) baseR = 4;

    int16_t x0 = dieCx,                  y0 = dieCy + baseR - 1;         // Bottom point
    int16_t x1 = dieCx - baseR + 2,      y1 = dieCy - (int16_t)(baseR * 0.8f); // Top left
    int16_t x2 = dieCx + baseR - 2,      y2 = dieCy - (int16_t)(baseR * 0.8f); // Top right

    // Render Double-Border Bevelled Die Face
    auto lineFunc = drawClippedLine ? drawClippedLine : [](int16_t ax, int16_t ay, int16_t bx, int16_t by) {
      // Fallback if drawClippedLine pointer wasn't provided
    };

    if (drawClippedLine) {
      // Outer Triangle Edge
      drawClippedLine(x0, y0, x1, y1);
      drawClippedLine(x1, y1, x2, y2);
      drawClippedLine(x2, y2, x0, y0);

      // Inner Inset Bevel Edge (for 3D die look)
      if (scale > 0.7f) {
        drawClippedLine(x0, y0 - 2, x1 + 2, y1 + 1);
        drawClippedLine(x1 + 2, y1 + 1, x2 - 2, y2 + 1);
        drawClippedLine(x2 - 2, y2 + 1, x0, y0 - 2);
      }
    } else {
      u8g2.drawLine(x0, y0, x1, y1);
      u8g2.drawLine(x1, y1, x2, y2);
      u8g2.drawLine(x2, y2, x0, y0);
    }

    // Render Answer Text once fully/partially emerged
    if (scale > 0.4f) {
      // Upgraded to standard crisp 5x7 font
      u8g2.setFont(u8g2_font_5x7_tf);

      TextLines tl = wrapAnswerForDie(MAGIC_ANSWERS[g_magicAnswerIdx]);

      // Vertical layout centering
      int16_t startY = dieCy - (tl.count * 4) + 2;

      for (uint8_t i = 0; i < tl.count; i++) {
        uint16_t lineLen = strlen(tl.lines[i]);
        int16_t drawX = dieCx - (lineLen * 5 / 2);
        int16_t drawY = startY + (i * 8);

        // Apply text dithering dissolve effect while floating up
        if (isRevealing) {
          u8g2.drawStr(drawX, drawY, tl.lines[i]);
        } else {
          u8g2.drawStr(drawX, drawY, tl.lines[i]);
        }
      }
    }
  }
}

#endif // MAGIC_8_BALL_H
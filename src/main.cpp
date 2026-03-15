#include <Arduino.h>
#include <M5Unified.h>
#include <FastLED.h>
#include <m5_unit_joystick2.hpp>

// --- ハードウェア設定 ---
#define LED_PIN     5
#define NUM_LEDS    16
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

#define JOY_I2C_ADDR 0x63
#define JOY_SDA      2
#define JOY_SCL      1

// --- 定数 ---
#define LOOP_INTERVAL_MS 20   // 50Hz
#define EMA_ALPHA        0.15f
#define BRIGHTNESS_MIN   5
#define BRIGHTNESS_MAX   255
#define ADC_MAX          65535.0f  // 16bit ADC
#define DEBUG_INTERVAL_MS 1000
#define DISPLAY_INTERVAL_MS 200
#define FADE_DURATION_MS 500  // フェード時間

// --- 色温度テーブル（位置, R, G, B） ---
struct ColorPoint {
    float pos;
    uint8_t r, g, b;
};

static const ColorPoint COLOR_TABLE[] = {
    {0.00f, 255, 147,  41},  // 1900K Candle
    {0.14f, 255, 197, 143},  // 2600K Tungsten40W
    {0.19f, 255, 214, 170},  // 2850K Tungsten100W
    {0.25f, 255, 241, 224},  // 3200K Halogen
    {0.65f, 255, 250, 244},  // 5200K CarbonArc
    {0.80f, 255, 255, 255},  // 6000K DirectSunlight
    {1.00f, 201, 226, 255},  // 7000K OvercastSky
};
static const int COLOR_TABLE_SIZE = sizeof(COLOR_TABLE) / sizeof(COLOR_TABLE[0]);

// --- グローバル変数 ---
CRGB leds[NUM_LEDS];
M5UnitJoystick2 joystick;
M5Canvas canvas(&M5.Display);

float filteredX = 32767.0f;
float filteredY = 32767.0f;
float currentColorT = 0.5f;

// モード管理
bool editMode = true;
bool lastAtomBtnState = false;

// LED ON/OFF + フェード
bool ledOn = true;
bool lastJoyBtnState = false;
float fadeLevel = 1.0f;
bool fading = false;
float fadeTarget = 1.0f;
float fadeStep = 0.0f;

// 現在のパラメータ
CRGB currentColor = CRGB::White;
uint8_t currentBrightness = 130;

unsigned long lastDebugTime = 0;
unsigned long lastDisplayTime = 0;

// --- メディアンフィルタ（3サンプル） ---
uint16_t median3(uint16_t a, uint16_t b, uint16_t c) {
    if (a > b) { uint16_t t = a; a = b; b = t; }
    if (b > c) { b = c; }
    if (a > b) { b = a; }
    return b;
}

// --- 色温度補間 ---
CRGB interpolateColor(float t) {
    t = constrain(t, 0.0f, 1.0f);
    for (int i = 0; i < COLOR_TABLE_SIZE - 1; i++) {
        if (t <= COLOR_TABLE[i + 1].pos) {
            float segLen = COLOR_TABLE[i + 1].pos - COLOR_TABLE[i].pos;
            float ratio = (segLen > 0.0f) ? (t - COLOR_TABLE[i].pos) / segLen : 0.0f;
            uint8_t r = COLOR_TABLE[i].r + (int)((COLOR_TABLE[i + 1].r - COLOR_TABLE[i].r) * ratio);
            uint8_t g = COLOR_TABLE[i].g + (int)((COLOR_TABLE[i + 1].g - COLOR_TABLE[i].g) * ratio);
            uint8_t b = COLOR_TABLE[i].b + (int)((COLOR_TABLE[i + 1].b - COLOR_TABLE[i].b) * ratio);
            return CRGB(r, g, b);
        }
    }
    const ColorPoint &last = COLOR_TABLE[COLOR_TABLE_SIZE - 1];
    return CRGB(last.r, last.g, last.b);
}

// --- ディスプレイ更新 ---
void updateDisplay() {
    int w = M5.Display.width();
    int h = M5.Display.height();
    canvas.createSprite(w, h);
    canvas.fillSprite(TFT_BLACK);

    // モード表示（上部バー）
    uint16_t barColor = editMode ? canvas.color565(0, 100, 200) : canvas.color565(0, 140, 60);
    canvas.fillRect(0, 0, w, 22, barColor);
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextDatum(MC_DATUM);
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.drawString(editMode ? "EDIT" : "LIGHT", w / 2, 11);

    // 色プレビュー（角丸四角）
    uint16_t previewColor = canvas.color565(currentColor.r, currentColor.g, currentColor.b);
    canvas.fillRoundRect(14, 30, w - 28, 36, 6, previewColor);
    canvas.drawRoundRect(14, 30, w - 28, 36, 6, TFT_DARKGREY);

    // 色温度（K）
    int kelvin = 1900 + (int)(currentColorT * 5100);
    canvas.setTextDatum(ML_DATUM);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(TFT_LIGHTGREY);
    canvas.drawString("Temp", 8, 80);
    canvas.setTextDatum(MR_DATUM);
    char buf[16];
    snprintf(buf, sizeof(buf), "%dK", kelvin);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString(buf, w - 8, 80);

    // 輝度（%）
    int brightPct = (int)(currentBrightness / 255.0f * 100);
    canvas.setTextDatum(ML_DATUM);
    canvas.setTextColor(TFT_LIGHTGREY);
    canvas.drawString("Bright", 8, 100);
    canvas.setTextDatum(MR_DATUM);
    snprintf(buf, sizeof(buf), "%d%%", brightPct);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString(buf, w - 8, 100);

    // ON/OFF状態
    canvas.setTextDatum(MC_DATUM);
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    if (ledOn) {
        canvas.fillRoundRect(24, 114, w - 48, 18, 4, canvas.color565(200, 160, 0));
        canvas.setTextColor(TFT_BLACK);
        canvas.drawString("ON", w / 2, 123);
    } else {
        canvas.fillRoundRect(24, 114, w - 48, 18, 4, canvas.color565(60, 60, 60));
        canvas.setTextColor(TFT_DARKGREY);
        canvas.drawString("OFF", w / 2, 123);
    }

    canvas.pushSprite(0, 0);
    canvas.deleteSprite();
}

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);

    Serial.println("=== M5AtomS3 LED Ring Controller ===");

    // ディスプレイ初期設定
    M5.Display.setRotation(2);
    M5.Display.setBrightness(80);

    // FastLED初期化
    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(BRIGHTNESS_MAX);
    fill_solid(leds, NUM_LEDS, CRGB::White);
    FastLED.show();

    // Joystick2初期化
    while (!joystick.begin(&Wire, JOY_I2C_ADDR, JOY_SDA, JOY_SCL)) {
        Serial.println("Joystick2 not found, retrying...");
        delay(500);
    }
    Serial.println("Joystick2 connected.");

    fadeStep = (float)LOOP_INTERVAL_MS / FADE_DURATION_MS;

    updateDisplay();
}

void loop() {
    static unsigned long lastLoop = 0;
    unsigned long now = millis();
    if (now - lastLoop < LOOP_INTERVAL_MS) return;
    lastLoop = now;

    M5.update();

    // --- ジョイスティック読み取り（3回メディアン） ---
    uint16_t sx[3], sy[3];
    for (int i = 0; i < 3; i++) {
        joystick.get_joy_adc_16bits_value_xy(&sx[i], &sy[i]);
    }
    uint16_t rawX = median3(sx[0], sx[1], sx[2]);
    uint16_t rawY = median3(sy[0], sy[1], sy[2]);
    uint8_t joyButton = joystick.get_button_value();

    // --- AtomS3本体ボタン: 編集/ライトモード切替 ---
    if (M5.BtnA.wasPressed()) {
        editMode = !editMode;
        Serial.printf("Mode: %s\n", editMode ? "EDIT" : "LIGHT");
    }

    // --- ジョイスティックボタン: ON/OFFトグル（フェード開始） ---
    bool joyBtnPressed = (joyButton == 0);
    if (joyBtnPressed && !lastJoyBtnState) {
        ledOn = !ledOn;
        fadeTarget = ledOn ? 1.0f : 0.0f;
        fading = true;
        Serial.printf("LED: %s (fading)\n", ledOn ? "ON" : "OFF");
    }
    lastJoyBtnState = joyBtnPressed;

    // --- フェード処理 ---
    if (fading) {
        if (fadeTarget > fadeLevel) {
            fadeLevel += fadeStep;
            if (fadeLevel >= fadeTarget) { fadeLevel = fadeTarget; fading = false; }
        } else {
            fadeLevel -= fadeStep;
            if (fadeLevel <= fadeTarget) { fadeLevel = fadeTarget; fading = false; }
        }
    }

    // --- 編集モード: ジョイスティックでパラメータ更新 ---
    if (editMode) {
        filteredX = filteredX + EMA_ALPHA * ((float)rawX - filteredX);
        filteredY = filteredY + EMA_ALPHA * ((float)rawY - filteredY);

        currentColorT = constrain(filteredX / ADC_MAX, 0.0f, 1.0f);
        currentColor = interpolateColor(currentColorT);

        float brightnessNorm = 1.0f - constrain(filteredY / ADC_MAX, 0.0f, 1.0f);
        currentBrightness = BRIGHTNESS_MIN + (uint8_t)(brightnessNorm * (BRIGHTNESS_MAX - BRIGHTNESS_MIN));
    }

    // --- LED更新（フェード適用） ---
    uint8_t finalBrightness = (uint8_t)(currentBrightness * fadeLevel);
    fill_solid(leds, NUM_LEDS, currentColor);
    FastLED.setBrightness(finalBrightness);
    FastLED.show();

    // --- ディスプレイ更新（200ms間隔） ---
    if (now - lastDisplayTime >= DISPLAY_INTERVAL_MS) {
        lastDisplayTime = now;
        updateDisplay();
    }

    // --- デバッグ出力（1秒間隔） ---
    if (now - lastDebugTime >= DEBUG_INTERVAL_MS) {
        lastDebugTime = now;
        int kelvin = 1900 + (int)(currentColorT * 5100);
        Serial.printf("Bright:%3d Fade:%.2f %dK | %s %s\n",
                      currentBrightness, fadeLevel, kelvin,
                      editMode ? "EDIT" : "LIGHT",
                      ledOn ? "ON" : "OFF");
    }
}

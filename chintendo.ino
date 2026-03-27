#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>
#include <math.h>

#if defined(ESP32) && __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

#ifndef LV_COLOR_16_SWAP
#define LV_COLOR_16_SWAP 1
#endif

#include <lvgl.h>

// -----------------------------------------------------------------------------
// Display + pinout
// -----------------------------------------------------------------------------
static constexpr uint8_t TFT_CS = 3;
static constexpr uint8_t TFT_DC = 4;
static constexpr uint8_t TFT_RST = 5;
static constexpr uint8_t TFT_SCL = 8;
static constexpr uint8_t TFT_SDA = 10;

static constexpr uint16_t SCREEN_W = 128;
static constexpr uint16_t SCREEN_H = 128;

Adafruit_SSD1351 display(SCREEN_W, SCREEN_H, &SPI, TFT_CS, TFT_DC, TFT_RST);

// -----------------------------------------------------------------------------
// SquareLine generated files
// -----------------------------------------------------------------------------
#include "src/ui.h"
#include "src/ui_helpers.h"
#include "src/ui_events.h"

// -----------------------------------------------------------------------------
// Physical buttons
// -----------------------------------------------------------------------------
static constexpr uint8_t LEFT_BTN_PIN = 2;
static constexpr uint8_t RIGHT_BTN_PIN = 20;

bool lastLeftBtnState = HIGH;
bool lastRightBtnState = HIGH;

unsigned long lastLeftBtnPressTime = 0;
unsigned long lastRightBtnPressTime = 0;

const unsigned long debounceMs = 120;

// -----------------------------------------------------------------------------
// Button sound effect (PWM on GPIO7)
// -----------------------------------------------------------------------------
static constexpr uint8_t SFX_PWM_PIN = 7;

// Cute Shepard-style rising illusion: use a bright major-pentatonic contour so
// each press feels sweeter while still giving an "always climbing" intensity.
static constexpr float SHEPARD_BASE_HZ = 261.63f; // C4
static constexpr uint8_t SHEPARD_NOTES_PER_OCTAVE = 12;
static constexpr uint8_t SHEPARD_OCTAVE_SPAN = 3;
static constexpr uint8_t buttonSfxStepCount = 4;
static constexpr uint8_t buttonSfxScaleStride = 1;
static constexpr uint8_t SHEPARD_SCALE_LENGTH = 5;
static constexpr uint8_t SHEPARD_CUTE_SCALE[SHEPARD_SCALE_LENGTH] = {0, 2, 4, 7, 9}; // major pentatonic
static constexpr uint16_t buttonSfxStepDurationMs = 22;
static constexpr uint16_t buttonSfxGapMs = 8;

bool buttonSfxActive = false;
uint8_t buttonSfxStep = 0;
unsigned long buttonSfxStepStartMs = 0;
bool buttonSfxInGap = false;
uint16_t shepardPhase = 0;

static const uint16_t winSfxFreqs[] = {1047, 1319, 1568, 2093}; // C6 E6 G6 C7
static const uint16_t winSfxDurationsMs[] = {80, 80, 100, 220};
static constexpr uint8_t winSfxStepCount = sizeof(winSfxFreqs) / sizeof(winSfxFreqs[0]);
static constexpr uint16_t winSfxGapMs = 14;

bool winSfxActive = false;
uint8_t winSfxStep = 0;
unsigned long winSfxStepStartMs = 0;
bool winSfxInGap = false;

// -----------------------------------------------------------------------------
// ikuMeter game state
// -----------------------------------------------------------------------------
int ikuValue = 0;
unsigned long lastDecayTime = 0;

const unsigned long baseDecayInterval = 90;
const unsigned long minDecayInterval = 20;
const int buttonGain = 8;
const int decayAmount = 6;

bool gameWon = false;

// -----------------------------------------------------------------------------
// Run stats
// -----------------------------------------------------------------------------
unsigned long screen2StartTime = 0;
unsigned long totalScreen2TimeMs = 0;
unsigned long screen3EnterTime = 0;
unsigned long screen3ReadyDelayMs = 2000;
unsigned long strokeCount = 0;

int closeTriggerScore = 0;
bool closeAnimationPlayed = false;
int ganTriggerScore = 0;
bool ganAnimationPlayed = false;
bool shinAnimationPlayed = false;
bool shin2AnimationPlayed = false;

bool screen2TimerRunning = false;
bool screen3TimerStarted = false;



// -----------------------------------------------------------------------------
// LVGL focus handling
// -----------------------------------------------------------------------------

static lv_obj_t* trackedScreen = nullptr;
static int32_t preferredFocusIndex = -1;

// -----------------------------------------------------------------------------
// LVGL display buffer
// -----------------------------------------------------------------------------
static lv_color_t lvgl_buf[SCREEN_W * 20];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;

static uint32_t last_tick_ms = 0;

// -----------------------------------------------------------------------------
// Fireworks made from LVGL objects
// -----------------------------------------------------------------------------
static lv_timer_t* fireworksTimer = nullptr;

struct Particle {
    lv_obj_t* obj;
    float x;
    float y;
    float vx;
    float vy;
    uint16_t life;
    bool active;
};

static constexpr uint8_t PARTICLE_COUNT = 24;
static Particle particles[PARTICLE_COUNT];

// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------
void triggerWinState();
void startFireworks();
void stopFireworks();
void resetGameData();
void updateScreenTimers();
void handleScreen3Restart();
void finalizeScreen2Time();
void maybePlayGanAnimation(int previousValue);
void maybePlayCloseAnimation(int previousValue);
uint16_t getShepardFrequency(uint8_t stepInPress);
void startButtonSfx();
void stopButtonSfx();
void updateButtonSfx();
void startWinSfx();
void stopWinSfx();
void updateWinSfx();

void resetShin(){
    
    lv_anim_del(ui_Image6, nullptr);
    lv_obj_set_style_opa(ui_Image6, 255, LV_PART_MAIN);
    lv_img_set_zoom(ui_Image6, 0);
    lv_img_set_angle(ui_Image6, 0);
    shinAnimationPlayed = false;

    lv_anim_del(ui_Image7, nullptr);
    lv_obj_set_style_opa(ui_Image7, 255, LV_PART_MAIN);
    lv_img_set_zoom(ui_Image7, 0);
    lv_img_set_angle(ui_Image7, 0);
    shin2AnimationPlayed = false;
}

// -----------------------------------------------------------------------------
// Helper functions
// -----------------------------------------------------------------------------
void writeSfxTone(uint32_t frequency) {
#if defined(ESP32)
    ledcWriteTone(SFX_PWM_PIN, frequency);
#else
    if (frequency == 0) {
        noTone(SFX_PWM_PIN);
    } else {
        tone(SFX_PWM_PIN, frequency);
    }
#endif
}

void stopButtonSfx() {
    writeSfxTone(0);

    buttonSfxActive = false;
    buttonSfxInGap = false;
    buttonSfxStep = 0;
}

void stopWinSfx() {
    if (winSfxActive) {
        writeSfxTone(0);
    }

    winSfxActive = false;
    winSfxInGap = false;
    winSfxStep = 0;
}

void startWinSfx() {
    stopWinSfx();
    stopButtonSfx();

    winSfxActive = true;
    winSfxStep = 0;
    winSfxInGap = false;
    winSfxStepStartMs = millis();

    writeSfxTone(winSfxFreqs[0]);
}

uint16_t getShepardFrequency(uint8_t stepInPress) {
    const uint16_t noteIndex = shepardPhase + (stepInPress * buttonSfxScaleStride);
    const uint8_t octave = (noteIndex / SHEPARD_SCALE_LENGTH) % SHEPARD_OCTAVE_SPAN;
    const uint8_t scaleIndex = noteIndex % SHEPARD_SCALE_LENGTH;
    const float semitones = static_cast<float>(SHEPARD_CUTE_SCALE[scaleIndex] + (octave * SHEPARD_NOTES_PER_OCTAVE));
    const float frequency = SHEPARD_BASE_HZ * powf(2.0f, semitones / SHEPARD_NOTES_PER_OCTAVE);

    return static_cast<uint16_t>(roundf(frequency));
}

void startButtonSfx() {
    // Don't restart while a tone is already running; let the current SFX
    // finish so rapid button mashing doesn't chop/cut off the sound.
    if (buttonSfxActive) {
        return;
    }

    buttonSfxActive = true;
    buttonSfxStep = 0;
    buttonSfxInGap = false;
    buttonSfxStepStartMs = millis();

    shepardPhase = (shepardPhase + 1) % (SHEPARD_SCALE_LENGTH * SHEPARD_OCTAVE_SPAN);
    writeSfxTone(getShepardFrequency(0));
}

void updateButtonSfx() {
    if (!buttonSfxActive) {
        return;
    }

    unsigned long now = millis();

    if (!buttonSfxInGap) {
        if (now - buttonSfxStepStartMs < buttonSfxStepDurationMs) {
            return;
        }

        writeSfxTone(0);

        buttonSfxInGap = true;
        buttonSfxStepStartMs = now;
        return;
    }

    if (now - buttonSfxStepStartMs < buttonSfxGapMs) {
        return;
    }

    buttonSfxStep++;

    if (buttonSfxStep >= buttonSfxStepCount) {
        stopButtonSfx();
        return;
    }

    buttonSfxInGap = false;
    buttonSfxStepStartMs = now;

    writeSfxTone(getShepardFrequency(buttonSfxStep));
}

void updateWinSfx() {
    if (!winSfxActive) {
        return;
    }

    unsigned long now = millis();

    if (!winSfxInGap) {
        if (now - winSfxStepStartMs < winSfxDurationsMs[winSfxStep]) {
            return;
        }

        writeSfxTone(0);

        winSfxInGap = true;
        winSfxStepStartMs = now;
        return;
    }

    if (now - winSfxStepStartMs < winSfxGapMs) {
        return;
    }

    winSfxStep++;

    if (winSfxStep >= winSfxStepCount) {
        stopWinSfx();
        return;
    }

    winSfxInGap = false;
    winSfxStepStartMs = now;

    writeSfxTone(winSfxFreqs[winSfxStep]);
}

void finalizeScreen2Time() {
    if (screen2TimerRunning) {
        totalScreen2TimeMs += millis() - screen2StartTime;
        screen2TimerRunning = false;
    }
}

void resetGameData() {
    stopFireworks();
    stopWinSfx();

    ikuValue = 0;
    lastDecayTime = millis();
    gameWon = false;

    resetShin();

    totalScreen2TimeMs = 0;
    screen2StartTime = millis();
    screen2TimerRunning = false;

    screen3EnterTime = 0;
    screen3TimerStarted = false;

    strokeCount = 0;

    closeTriggerScore = random(30, 51);
    closeAnimationPlayed = false;

    ganTriggerScore = random(60, 81);

    lv_obj_set_x(ui_Image4, 95);
    lv_obj_set_y(ui_Image4, 51);

    lv_obj_set_x(ui_Image5, 112);
    lv_obj_set_y(ui_Image5, 51);

    lv_obj_set_y(ui_Image2, -80);

    lv_anim_del(ui_Image4, nullptr);
    lv_anim_del(ui_Image5, nullptr);
    ganAnimationPlayed = false;
    closeAnimationPlayed = false;

    lv_bar_set_range(ui_ikuMeter, 0, 100);
    lv_bar_set_value(ui_ikuMeter, 0, LV_ANIM_OFF);

    lv_label_set_text(ui_Label1, "");
    lv_label_set_text(ui_Label2, "");
}

void updateIkuMeter(int delta) {
    if (gameWon) {
        return;
    }

    int previousIkuValue = ikuValue;

    ikuValue += delta;

    if (ikuValue > 100) {
        ikuValue = 100;
    }

    if (ikuValue <= 0) {
        ikuValue = 0;
    }

    lv_bar_set_value(ui_ikuMeter, ikuValue, LV_ANIM_ON);

    maybePlayCloseAnimation(previousIkuValue);
    maybePlayGanAnimation(previousIkuValue);

    if (ikuValue >= 100) {
        triggerWinState();
    }
}



void maybePlayCloseAnimation(int previousValue) {
    if (gameWon) {
        return;
    }

    if (closeAnimationPlayed) {
        return;
    }

    if (lv_scr_act() != ui_Screen2) {
        return;
    }

    if (previousValue < closeTriggerScore && ikuValue >= closeTriggerScore) {
        closeAnimationPlayed = true;
        gan_Animation(ui_Image4, 0);
    }
}

void maybePlayGanAnimation(int previousValue) {
    if (gameWon) {
        return;
    }

    if (ganAnimationPlayed) {
        return;
    }

    if (lv_scr_act() != ui_Screen2) {
        return;
    }

    if (previousValue < ganTriggerScore && ikuValue >= ganTriggerScore) {
        ganAnimationPlayed = true;
        close_Animation(ui_Image5, 0);
    }
}

unsigned long getDecayInterval() {
    if (ikuValue >= 90) {
        return 18;
    }

    if (ikuValue >= 80) {
        return 28;
    }

    if (ikuValue >= 70) {
        return 40;
    }

    float t = ikuValue / 100.0f;
    unsigned long interval =
        baseDecayInterval - static_cast<unsigned long>(t * (baseDecayInterval - minDecayInterval));

    if (interval < minDecayInterval) {
        interval = minDecayInterval;
    }

    return interval;
}

void updateScreenTimers() {
    lv_obj_t* activeScreen = lv_scr_act();

    if (activeScreen == ui_Screen2) {
        if (!screen2TimerRunning) {
            screen2StartTime = millis();
            screen2TimerRunning = true;
        }
    } else {
        finalizeScreen2Time();
    }

    if (activeScreen == ui_Screen3) {
        if (!screen3TimerStarted) {
            screen3EnterTime = millis();
            screen3TimerStarted = true;
        }
    } else {
        screen3TimerStarted = false;
    }
}

void handleScreen3Restart() {
    if (lv_scr_act() != ui_Screen3) {
        return;
    }

    if (!screen3TimerStarted) {
        return;
    }

    if (millis() - screen3EnterTime < screen3ReadyDelayMs) {
        return;
    }

    bool leftPressed = (lastLeftBtnState == HIGH && digitalRead(LEFT_BTN_PIN) == LOW);
    bool rightPressed = (lastRightBtnState == HIGH && digitalRead(RIGHT_BTN_PIN) == LOW);

    if (leftPressed || rightPressed) {
        lv_scr_load(ui_Screen1);
        screen2StartTime = millis();
        screen2TimerRunning = true;
        resetGameData();
    }
}

// -----------------------------------------------------------------------------
// Fireworks
// -----------------------------------------------------------------------------
lv_color_t getParticleColor(uint8_t index) {
    LV_UNUSED(index);
    return lv_color_make(173, 187, 165);
}

void createParticleObjects() {
    for (uint8_t i = 0; i < PARTICLE_COUNT; i++) {
        particles[i].obj = lv_obj_create(ui_Screen3);
        lv_obj_remove_style_all(particles[i].obj);
        lv_obj_set_size(particles[i].obj, 3, 3);
        lv_obj_clear_flag(particles[i].obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(particles[i].obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_style_radius(particles[i].obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(particles[i].obj, getParticleColor(i), 0);
        lv_obj_set_style_bg_opa(particles[i].obj, LV_OPA_COVER, 0);
        lv_obj_add_flag(particles[i].obj, LV_OBJ_FLAG_HIDDEN);

        particles[i].x = 0.0f;
        particles[i].y = 0.0f;
        particles[i].vx = 0.0f;
        particles[i].vy = 0.0f;
        particles[i].life = 0;
        particles[i].active = false;
    }
}

void spawnFireworkBurst(int16_t centerX, int16_t centerY) {
    for (uint8_t i = 0; i < PARTICLE_COUNT; i++) {
        float angle = (2.0f * PI * i) / PARTICLE_COUNT;
        float speed = 1.0f + static_cast<float>(random(8, 18)) / 10.0f;

        particles[i].x = static_cast<float>(centerX);
        particles[i].y = static_cast<float>(centerY);
        particles[i].vx = cosf(angle) * speed;
        particles[i].vy = sinf(angle) * speed;
        particles[i].life = random(18, 32);
        particles[i].active = true;

        lv_obj_set_pos(particles[i].obj, centerX, centerY);
        lv_obj_clear_flag(particles[i].obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(particles[i].obj, LV_OPA_COVER, 0);
    }
}

void updateFireworks(lv_timer_t* timer) {
    LV_UNUSED(timer);

    bool anyActive = false;

    for (uint8_t i = 0; i < PARTICLE_COUNT; i++) {
        if (!particles[i].active) {
            continue;
        }

        anyActive = true;

        particles[i].x += particles[i].vx;
        particles[i].y += particles[i].vy;
        particles[i].vx *= 0.99f;
        particles[i].vy *= 0.99f;
        particles[i].vy += 0.04f;

        if (particles[i].life > 0) {
            particles[i].life--;
        }

        if (particles[i].life == 0) {
            particles[i].active = false;
            lv_obj_add_flag(particles[i].obj, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_set_pos(
            particles[i].obj,
            static_cast<int16_t>(particles[i].x),
            static_cast<int16_t>(particles[i].y)
        );

        uint8_t opa = static_cast<uint8_t>((255UL * particles[i].life) / 32UL);
        lv_obj_set_style_bg_opa(particles[i].obj, opa, 0);
    }

    if (!anyActive) {
        int16_t burstX = random(20, SCREEN_W - 20);
        int16_t burstY = random(18, SCREEN_H - 35);
        spawnFireworkBurst(burstX, burstY);
    }
}

void stopFireworks() {
    if (fireworksTimer != nullptr) {
        lv_timer_del(fireworksTimer);
        fireworksTimer = nullptr;
    }

    for (uint8_t i = 0; i < PARTICLE_COUNT; i++) {
        if (particles[i].obj != nullptr && lv_obj_is_valid(particles[i].obj)) {
            lv_obj_del(particles[i].obj);
        }
        particles[i].obj = nullptr;
        particles[i].active = false;
    }
}

void startFireworks() {
    stopFireworks();

    createParticleObjects();
    spawnFireworkBurst(SCREEN_W / 2, SCREEN_H / 2);

    fireworksTimer = lv_timer_create(updateFireworks, 40, nullptr);
}

// -----------------------------------------------------------------------------
// Game state
// -----------------------------------------------------------------------------
void triggerWinState() {
    if (gameWon) {
        return;
    }

    gameWon = true;
    finalizeScreen2Time();

    unsigned long totalSeconds = totalScreen2TimeMs / 1000;

    lv_label_set_text_fmt(ui_Label1, "Time: %lu s", totalSeconds);
    lv_label_set_text_fmt(ui_Label2, "Strokes: %lu", strokeCount);

    Serial.println("WIN");

    lv_scr_load(ui_Screen3);
    startWinSfx();
    startFireworks();
}

// -----------------------------------------------------------------------------
// Button actions
// -----------------------------------------------------------------------------
void handleLeftButton() {
    if (gameWon) {
        return;
    }

    if (lv_scr_act() == ui_Screen1) {
        return;
    }

    if(shinAnimationPlayed == false){
        shin1_Animation(ui_Image6, 0);
        shinAnimationPlayed = true;
    }

    Serial.println("left_btn pressed");

    strokeCount++;

    lv_anim_del(ui_Image3, nullptr);
    chin_Animation(ui_Image3, 0);

    updateIkuMeter(buttonGain);
}

void handleRightButton() {
    if (gameWon) {
        return;
    }

    if (lv_scr_act() == ui_Screen1) {
        return;
    }

    if(shin2AnimationPlayed == false){
        shin1_Animation(ui_Image7, 0);
        shin2AnimationPlayed = true;
    }

    Serial.println("right_btn pressed");

    strokeCount++;

    lv_anim_del(ui_Image3, nullptr);
    chinback_Animation(ui_Image3, 0);

    updateIkuMeter(buttonGain);
}

void pollPhysicalButtons() {
    handleScreen3Restart();

    bool currentLeftBtnState = digitalRead(LEFT_BTN_PIN);
    bool currentRightBtnState = digitalRead(RIGHT_BTN_PIN);

    unsigned long now = millis();

    if (lastLeftBtnState == HIGH && currentLeftBtnState == LOW) {
        if (now - lastLeftBtnPressTime > debounceMs) {
            startButtonSfx();
            handleLeftButton();
            lastLeftBtnPressTime = now;
        }
    }

    if (lastRightBtnState == HIGH && currentRightBtnState == LOW) {
        if (now - lastRightBtnPressTime > debounceMs) {
            startButtonSfx();
            handleRightButton();
            lastRightBtnPressTime = now;
        }
    }

    lastLeftBtnState = currentLeftBtnState;
    lastRightBtnState = currentRightBtnState;
}

// -----------------------------------------------------------------------------
// LVGL display flush
// -----------------------------------------------------------------------------
static void my_disp_flush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
    const int16_t x1 = static_cast<int16_t>(area->x1);
    const int16_t y1 = static_cast<int16_t>(area->y1);
    const int16_t w = static_cast<int16_t>(area->x2 - area->x1 + 1);
    const int16_t h = static_cast<int16_t>(area->y2 - area->y1 + 1);

    display.startWrite();
    display.setAddrWindow(x1, y1, w, h);

    const uint32_t pixelCount = static_cast<uint32_t>(w) * static_cast<uint32_t>(h);

    for (uint32_t i = 0; i < pixelCount; ++i) {
        uint16_t color = color_p[i].full;
#if LV_COLOR_16_SWAP
        color = static_cast<uint16_t>((color >> 8) | (color << 8));
#endif
        display.writeColor(color, 1);
    }

    display.endWrite();
    lv_disp_flush_ready(disp);
}

// -----------------------------------------------------------------------------
// Setup / loop
// -----------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    randomSeed(micros());

    SPI.begin(TFT_SCL, -1, TFT_SDA, TFT_CS);
    display.begin();
    display.setRotation(3);

    display.fillScreen(0xFFFF);
    delay(120);
    display.fillScreen(0x0000);

    lv_init();

    lv_disp_draw_buf_init(&draw_buf, lvgl_buf, nullptr, SCREEN_W * 20);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_W;
    disp_drv.ver_res = SCREEN_H;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    ui_init();

    lv_bar_set_range(ui_ikuMeter, 0, 100);
    lv_bar_set_value(ui_ikuMeter, 0, LV_ANIM_OFF);

    resetGameData();

    pinMode(LEFT_BTN_PIN, INPUT_PULLUP);
    pinMode(RIGHT_BTN_PIN, INPUT_PULLUP);

#if defined(ESP32)
    ledcAttach(SFX_PWM_PIN, 2000, 8);
#else
    pinMode(SFX_PWM_PIN, OUTPUT);
#endif
    stopButtonSfx();

    updateScreenTimers();

    last_tick_ms = millis();
}

void loop() {
    uint32_t now = millis();

    lv_tick_inc(now - last_tick_ms);
    last_tick_ms = now;

    lv_timer_handler();
    updateScreenTimers();
    pollPhysicalButtons();
    updateButtonSfx();
    updateWinSfx();

    if (lv_scr_act() == ui_Screen2 && !gameWon) {
        unsigned long currentDecayInterval = getDecayInterval();

        if (now - lastDecayTime >= currentDecayInterval) {
            updateIkuMeter(-decayAmount);
            lastDecayTime = now;
        }
    }

    delay(5);
}

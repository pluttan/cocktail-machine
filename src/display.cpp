#include "display.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_spiffs.h"
#include "strings.h"

static const char* TAG = "DISPLAY";

LGFX tft;
static bool fsOk = false;

// ── Portrait 320x480 ──
static constexpr int W = 320;
static constexpr int H = 480;

// VFS mount point
#define FS_BASE "/spiffs"

// ── Font definitions ──
struct FontDef {
    const char* vlw;
    const lgfx::IFont* fallback;
    uint8_t* buf;
};
static FontDef FONT_H = {FS_BASE "/mont_b28.vlw", &fonts::FreeSansBold18pt7b, nullptr};
static FontDef FONT_M = {FS_BASE "/mont_b20.vlw", &fonts::FreeSansBold12pt7b, nullptr};
static FontDef FONT_S = {FS_BASE "/mont_14.vlw",  &fonts::FreeSans9pt7b,      nullptr};

static FontDef* loadedFont = nullptr;

// ── VFS file helpers ──
static bool fs_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static uint8_t* fs_read_alloc(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = (uint8_t*)malloc(len);
    if (!buf) { fclose(f); return nullptr; }
    fread(buf, 1, len, f);
    fclose(f);
    if (out_len) *out_len = len;
    return buf;
}

static bool loadVlwBuf(FontDef& f) {
    if (f.buf) return true;
    if (!fsOk || !fs_exists(f.vlw)) return false;
    size_t len;
    f.buf = fs_read_alloc(f.vlw, &len);
    return f.buf != nullptr;
}

static void useFont(FontDef& f) {
    if (&f == loadedFont) return;
    if (loadVlwBuf(f)) {
        tft.unloadFont();
        tft.loadFont(f.buf);
        loadedFont = &f;
        return;
    }
    tft.setFont(f.fallback);
    loadedFont = nullptr;
}

// ── Helpers ──

static bool drawPng(const char* path, int x, int y) {
    if (!fsOk || !fs_exists(path)) return false;
    size_t len;
    uint8_t* buf = fs_read_alloc(path, &len);
    if (!buf) return false;
    tft.drawPng(buf, len, x, y);
    free(buf);
    return true;
}

static void clearScreen() {
    if (!drawPng(FS_BASE "/bg.png", 0, 0))
        tft.fillScreen(ui::BG);
}

static void drawCenteredText(const char* text, int y, FontDef& font, uint32_t color, int32_t bg = -1) {
    useFont(font);
    if (bg >= 0) tft.setTextColor(color, (uint32_t)bg);
    else         tft.setTextColor(color);
    int tw = tft.textWidth(text);
    tft.setCursor((W - tw) / 2, y);
    tft.print(text);
}

static bool touchInRect(int tx, int ty, int rx, int ry, int rw, int rh) {
    return tx >= rx && tx <= rx + rw && ty >= ry && ty <= ry + rh;
}

static bool getTouchPoint(int& x, int& y) {
    lgfx::touch_point_t tp;
    if (tft.getTouch(&tp)) {
        x = tp.x;
        y = H - 1 - tp.y;
        return true;
    }
    return false;
}

// ── Decorative elements ──

static void drawAccentLine(int y, int lineW, uint32_t color) {
    int x0 = (W - lineW) / 2;
    int cx = W / 2;
    tft.drawFastHLine(x0, y, lineW / 2 - 10, color);
    tft.drawFastHLine(cx + 10, y, lineW / 2 - 10, color);
    tft.fillTriangle(cx - 5, y, cx, y - 3, cx + 5, y, color);
    tft.fillTriangle(cx - 5, y, cx, y + 3, cx + 5, y, color);
}

static void drawGlass(int cx, int cy, uint32_t color, float scale = 1.0f) {
    int tw = (int)(42 * scale);
    int bh = (int)(34 * scale);
    int sh = (int)(26 * scale);
    int bw = (int)(20 * scale);
    tft.fillTriangle(cx - tw, cy, cx + tw, cy, cx, cy + bh, color);
    for (int i = -1; i <= 1; i++)
        tft.drawFastHLine(cx - tw - 3, cy + i, tw * 2 + 6, color);
    int lOff = (int)(8 * scale);
    int lw = tw - lOff;
    tft.fillTriangle(cx - lw, cy + lOff, cx + lw, cy + lOff, cx, cy + bh - 2, ui::YELLOW);
    tft.fillRect(cx - 1, cy + bh, 3, sh, color);
    tft.fillRoundRect(cx - bw, cy + bh + sh, bw * 2, (int)(4 * scale), 2, color);
}

static void drawBubbles(int cx, int cy, uint32_t color) {
    tft.drawCircle(cx - 8, cy - 5, 3, color);
    tft.drawCircle(cx + 12, cy - 12, 2, color);
    tft.drawCircle(cx + 3, cy - 18, 4, color);
    tft.drawCircle(cx - 14, cy - 16, 2, color);
    tft.drawCircle(cx + 18, cy - 6, 3, color);
}

static void drawQrBrackets(int cx, int cy, int size, uint32_t color) {
    int x = cx - size / 2, y = cy - size / 2;
    int cn = size / 4;
    tft.fillRect(x, y, cn, 3, color);
    tft.fillRect(x, y, 3, cn, color);
    tft.fillRect(x + size - cn, y, cn, 3, color);
    tft.fillRect(x + size - 3, y, 3, cn, color);
    tft.fillRect(x, y + size - 3, cn, 3, color);
    tft.fillRect(x, y + size - cn, 3, cn, color);
    tft.fillRect(x + size - cn, y + size - 3, cn, 3, color);
    tft.fillRect(x + size - 3, y + size - cn, 3, cn, color);
}

static void drawBtn(int x, int y, int w, int h,
                    const char* label, uint32_t bg, uint32_t fg) {
    tft.fillRoundRect(x, y, w, h, 10, bg);
    tft.drawRoundRect(x, y, w, h, 10, ui::SURFACE1);
    useFont(FONT_M);
    tft.setTextColor(fg, bg);
    int tw = tft.textWidth(label);
    int th = tft.fontHeight();
    tft.setCursor(x + (w - tw) / 2, y + (h - th) / 2);
    tft.print(label);
}

// ── Init ──

static void fs_init() {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = FS_BASE,
        .partition_label = "spiffs",
        .max_files = 8,
        .format_if_mount_failed = false,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret == ESP_OK) {
        fsOk = true;
        size_t total = 0, used = 0;
        esp_spiffs_info("spiffs", &total, &used);
        ESP_LOGI(TAG, "SPIFFS mounted at %s (%d/%d bytes)", FS_BASE, (int)used, (int)total);

        // Check key files
        const char* files[] = {"/bg.png", "/glass.png", "/mont_b28.vlw", "/recipes.json", "/wifi.txt"};
        for (int i = 0; i < 5; i++) {
            char path[64];
            snprintf(path, sizeof(path), "%s%s", FS_BASE, files[i]);
            struct stat st;
            if (stat(path, &st) == 0)
                ESP_LOGI(TAG, "  %s: %ld bytes", files[i], st.st_size);
            else
                ESP_LOGW(TAG, "  %s: MISSING", files[i]);
        }
    } else {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
    }
}

void display_init() {
    fs_init();
    tft.init();
    tft.setRotation(2);
    tft.setBrightness(255);
    tft.setColorDepth(18);
    tft.setBaseColor(ui::BG);
    clearScreen();
}

// ══════════════════════════════════════
// ██  IDLE
// ══════════════════════════════════════

void display_drawIdle(int cocktailsToday) {
    clearScreen();
    drawAccentLine(30, 200, ui::ACCENT);

    if (!drawPng(FS_BASE "/glass.png", (W - 100) / 2, 45)) {
        drawGlass(W / 2, 60, ui::ACCENT, 1.4f);
        drawBubbles(W / 2, 60, ui::TEXT_DIM);
    }

    drawCenteredText(S_COCKTAIL, 195, FONT_H, ui::TEXT);
    drawCenteredText(S_MACHINE, 240, FONT_H, ui::ACCENT);
    drawAccentLine(280, 220, ui::BORDER);
    drawCenteredText(S_SHOW_QR, 310, FONT_M, ui::TEXT_SEC);

    if (!drawPng(FS_BASE "/qr.png", (W - 80) / 2, 340))
        drawQrBrackets(W / 2, 375, 70, ui::ACCENT);

    if (cocktailsToday > 0) {
        char buf[24];
        snprintf(buf, sizeof(buf), S_N_TODAY_FMT, cocktailsToday);
        drawCenteredText(buf, H - 20, FONT_S, ui::TEXT_DIM);
    }
}

// ══════════════════════════════════════
// ██  SELECT_STRENGTH
// ══════════════════════════════════════

static constexpr int SS_MARGIN  = 16;
static constexpr int SS_Y0      = 60;
static constexpr int SS_ROW_H   = 52;
static constexpr int SS_TOGGLE_H = 36;
static constexpr int SS_GAP     = 6;
static constexpr int SS_DOT_R   = 7;
static constexpr int SS_DOT_R_SEL = 9;
static constexpr int SS_BTN_W   = 130;
static constexpr int SS_BTN_H   = 44;

static int ssRowY[MAX_INGR];
static int ssRowH[MAX_INGR];
static int ssDotX0[MAX_INGR];
static int ssDotStep[MAX_INGR];
static int ssGoY, ssCancelY;

void display_drawSelectStrength(const Recipe& recipe, const float* mults) {
    clearScreen();
    drawCenteredText(recipe.name, 10, FONT_H, ui::TEXT);
    drawAccentLine(48, 200, ui::ACCENT);

    int y = SS_Y0;
    for (int i = 0; i < recipe.numIngredients; i++) {
        const Ingredient& ing = recipe.ingredients[i];
        const StrengthOpt* opt = findStrengthOpt(recipe, i);
        float mult = mults[i];
        int ml = (int)(ing.baseMl * mult);
        ssRowY[i] = y;

        if (opt && opt->numPoints > 1) {
            ssRowH[i] = SS_ROW_H;
            tft.fillRoundRect(SS_MARGIN, y, W - SS_MARGIN * 2, SS_ROW_H, 8, ui::BG_CARD);
            tft.drawRoundRect(SS_MARGIN, y, W - SS_MARGIN * 2, SS_ROW_H, 8, ui::BORDER);

            useFont(FONT_S);
            tft.setTextColor(ui::TEXT, ui::BG_CARD);
            tft.setCursor(SS_MARGIN + 10, y + 4);
            tft.print(ing.name);

            char mlStr[16];
            snprintf(mlStr, sizeof(mlStr), S_ML_FMT, ml);
            tft.setTextColor(ui::ACCENT, ui::BG_CARD);
            int mw = tft.textWidth(mlStr);
            tft.setCursor(W - SS_MARGIN - 10 - mw, y + 4);
            tft.print(mlStr);

            int dotsW = W - SS_MARGIN * 2 - 30;
            int step = dotsW / (opt->numPoints - 1);
            int x0 = SS_MARGIN + 15;
            int dotY = y + SS_ROW_H - 16;
            ssDotX0[i] = x0;
            ssDotStep[i] = step;

            tft.drawFastHLine(x0, dotY, step * (opt->numPoints - 1), ui::BORDER);

            int selIdx = opt->defaultIdx;
            for (int j = 0; j < opt->numPoints; j++) {
                if (fabsf(mult - opt->points[j]) < 0.01f) { selIdx = j; break; }
            }
            for (int j = 0; j < opt->numPoints; j++) {
                int dx = x0 + j * step;
                if (j == selIdx) {
                    tft.fillCircle(dx, dotY, SS_DOT_R_SEL, ui::ACCENT);
                } else {
                    tft.drawCircle(dx, dotY, SS_DOT_R, ui::BORDER);
                    tft.fillCircle(dx, dotY, SS_DOT_R - 2, ui::BG_CARD);
                }
            }
            y += SS_ROW_H + SS_GAP;
        } else {
            ssRowH[i] = SS_TOGGLE_H;
            ssDotX0[i] = -1;
            tft.fillRoundRect(SS_MARGIN, y, W - SS_MARGIN * 2, SS_TOGGLE_H, 8, ui::BG_CARD);
            tft.drawRoundRect(SS_MARGIN, y, W - SS_MARGIN * 2, SS_TOGGLE_H, 8, ui::BORDER);

            bool on = mult > 0.01f;
            int circX = SS_MARGIN + 22;
            int circY = y + SS_TOGGLE_H / 2;
            if (on) {
                tft.fillCircle(circX, circY, 10, ui::GREEN);
            } else {
                tft.drawCircle(circX, circY, 10, ui::RED);
                tft.drawCircle(circX, circY, 9, ui::RED);
            }

            useFont(FONT_S);
            tft.setTextColor(on ? ui::TEXT : ui::TEXT_DIM, ui::BG_CARD);
            tft.setCursor(SS_MARGIN + 40, y + (SS_TOGGLE_H - 14) / 2);
            tft.print(ing.name);

            if (on) {
                char mlStr[16];
                snprintf(mlStr, sizeof(mlStr), S_ML_FMT, ml);
                tft.setTextColor(ui::ACCENT, ui::BG_CARD);
                int mw = tft.textWidth(mlStr);
                tft.setCursor(W - SS_MARGIN - 10 - mw, y + (SS_TOGGLE_H - 14) / 2);
                tft.print(mlStr);
            }
            y += SS_TOGGLE_H + SS_GAP;
        }
    }

    int btnArea = H - 20;
    ssCancelY = btnArea - SS_BTN_H;
    ssGoY = ssCancelY - SS_BTN_H - 10;
    drawBtn((W - SS_BTN_W * 2 - 10) / 2, ssGoY, SS_BTN_W * 2 + 10, SS_BTN_H,
            S_READY, ui::ACCENT, ui::BG);
    drawBtn((W - SS_BTN_W) / 2, ssCancelY, SS_BTN_W, SS_BTN_H,
            S_CANCEL, ui::GRAY_BTN, ui::TEXT_SEC);
}

int display_checkStrengthTap(const Recipe& recipe) {
    int tx, ty;
    if (!getTouchPoint(tx, ty)) return -1;

    int goX = (W - SS_BTN_W * 2 - 10) / 2;
    if (touchInRect(tx, ty, goX, ssGoY, SS_BTN_W * 2 + 10, SS_BTN_H)) return -3;
    int cancelX = (W - SS_BTN_W) / 2;
    if (touchInRect(tx, ty, cancelX, ssCancelY, SS_BTN_W, SS_BTN_H)) return -2;

    for (int i = 0; i < recipe.numIngredients; i++) {
        if (!touchInRect(tx, ty, SS_MARGIN, ssRowY[i], W - SS_MARGIN * 2, ssRowH[i])) continue;
        const StrengthOpt* opt = findStrengthOpt(recipe, i);
        if (opt && opt->numPoints > 1 && ssDotX0[i] >= 0) {
            int bestJ = -1, bestDist = 9999;
            for (int j = 0; j < opt->numPoints; j++) {
                int dx = ssDotX0[i] + j * ssDotStep[i];
                int dist = abs(tx - dx);
                if (dist < bestDist) { bestDist = dist; bestJ = j; }
            }
            if (bestJ >= 0 && bestDist < ssDotStep[i] / 2 + 10) return (i << 8) | bestJ;
        } else {
            return (i << 8) | 0xFF;
        }
    }
    return -1;
}

// ══════════════════════════════════════
// ██  DISPENSING
// ══════════════════════════════════════

static constexpr int STOP_X = (W - 160) / 2;
static constexpr int STOP_Y = H - 60;
static constexpr int STOP_W = 160;
static constexpr int STOP_H = 48;
static constexpr int PB_X = 24;
static constexpr int PB_Y = 65;
static constexpr int PB_W = W - 48;
static constexpr int PB_H = 44;

static float dsp_lastPct = -1;
static int   dsp_lastActive = -99;

void display_drawDispensingStatic(const char* cocktailName) {
    clearScreen();
    drawCenteredText(cocktailName, 15, FONT_M, ui::TEXT);
    tft.fillRoundRect(PB_X, PB_Y, PB_W, PB_H, 10, ui::BG_CARD);
    tft.drawRoundRect(PB_X, PB_Y, PB_W, PB_H, 10, ui::BORDER);
    drawAccentLine(140, 180, ui::BORDER);
    drawBtn(STOP_X, STOP_Y, STOP_W, STOP_H, S_STOP, ui::RED, ui::TEXT);
    dsp_lastPct = -1;
    dsp_lastActive = -99;
}

void display_updateDispensing(float progressPct, int activePump, const char* ingredientText) {
    if (fabsf(progressPct - dsp_lastPct) > 0.5f || dsp_lastPct < 0) {
        tft.fillRect(PB_X + 3, PB_Y + 3, PB_W - 6, PB_H - 6, ui::BG_CARD);
        int fw = (int)((PB_W - 6) * progressPct / 100.0f);
        if (fw > 0) {
            float t = progressPct / 100.0f;
            uint8_t r = (uint8_t)(0xF3 + (0xA6 - 0xF3) * t);
            uint8_t g = (uint8_t)(0x8B + (0xE3 - 0x8B) * t);
            uint8_t b = (uint8_t)(0xA8 + (0xA1 - 0xA8) * t);
            uint32_t fillColor = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            tft.fillRoundRect(PB_X + 3, PB_Y + 3, fw, PB_H - 6, 8, fillColor);
        }
        char pctStr[8];
        snprintf(pctStr, sizeof(pctStr), S_PCT_FMT, (int)progressPct);
        float t2 = progressPct / 100.0f;
        uint8_t pr = (uint8_t)(0xF3 + (0xA6 - 0xF3) * t2);
        uint8_t pg = (uint8_t)(0x8B + (0xE3 - 0x8B) * t2);
        uint8_t pb = (uint8_t)(0xA8 + (0xA1 - 0xA8) * t2);
        uint32_t pctBg = (fw > PB_W / 2) ? (((uint32_t)pr << 16) | ((uint32_t)pg << 8) | pb) : ui::BG_CARD;
        drawCenteredText(pctStr, PB_Y + 10, FONT_M, ui::TEXT, (int32_t)pctBg);
        dsp_lastPct = progressPct;
    }
    if (activePump != dsp_lastActive) {
        tft.fillRect(0, 130, W, 30, ui::BG);
        if (ingredientText && ingredientText[0])
            drawCenteredText(ingredientText, 132, FONT_M, ui::SKY);
        dsp_lastActive = activePump;
    }
}

bool display_checkStopButton() {
    int tx, ty;
    if (!getTouchPoint(tx, ty)) return false;
    return touchInRect(tx, ty, STOP_X, STOP_Y, STOP_W, STOP_H);
}

// ══════════════════════════════════════
// ██  BOTTLE_EMPTY
// ══════════════════════════════════════

static constexpr int BE_BTN_W = W - 40;
static constexpr int BE_BTN_H = 54;
static constexpr int BE_CONT_X = 20;
static constexpr int BE_CONT_Y = H - 130;
static constexpr int BE_CANC_X = 20;
static constexpr int BE_CANC_Y = H - 68;

void display_drawBottleEmpty(const char* ingredientName) {
    clearScreen();
    tft.drawRoundRect(8, 8, W - 16, H - 16, 12, ui::PEACH);
    tft.drawRoundRect(10, 10, W - 20, H - 20, 11, ui::PEACH);

    int cx = W / 2;
    if (!drawPng(FS_BASE "/warn.png", (W - 80) / 2, 40)) {
        tft.fillTriangle(cx, 50, cx - 35, 120, cx + 35, 120, ui::PEACH);
        tft.fillRect(cx - 3, 68, 6, 28, ui::BG);
        tft.fillCircle(cx, 105, 5, ui::BG);
    }

    drawCenteredText(S_BOTTLE_EMPTY, 150, FONT_H, ui::PEACH);
    char msg[80];
    snprintf(msg, sizeof(msg), S_REPLACE_COLON, ingredientName);
    drawCenteredText(msg, 210, FONT_M, ui::TEXT);
    drawCenteredText(S_THEN_CONTINUE, 255, FONT_S, ui::TEXT_SEC);
    drawBtn(BE_CONT_X, BE_CONT_Y, BE_BTN_W, BE_BTN_H, S_CONTINUE, ui::GREEN, ui::TEXT);
    drawBtn(BE_CANC_X, BE_CANC_Y, BE_BTN_W, BE_BTN_H, S_CANCEL, ui::RED, ui::TEXT);
}

int display_checkBottleEmptyButton() {
    int tx, ty;
    if (!getTouchPoint(tx, ty)) return -1;
    if (touchInRect(tx, ty, BE_CONT_X, BE_CONT_Y, BE_BTN_W, BE_BTN_H)) return 0;
    if (touchInRect(tx, ty, BE_CANC_X, BE_CANC_Y, BE_BTN_W, BE_BTN_H)) return 1;
    return -1;
}

// ══════════════════════════════════════
// ██  DONE
// ══════════════════════════════════════

void display_drawDone() {
    clearScreen();
    int cx = W / 2, cy = 140;
    if (!drawPng(FS_BASE "/check.png", (W - 100) / 2, 90)) {
        for (int r = 60; r >= 52; r--) {
            uint8_t a = (60 - r) * 10;
            tft.drawCircle(cx, cy, r, ((uint32_t)(a + 40) << 8));
        }
        tft.fillCircle(cx, cy, 50, ui::GREEN);
        for (int d = -3; d <= 3; d++) {
            tft.drawLine(cx - 22, cy + d, cx - 7, cy + 18 + d, ui::TEXT);
            tft.drawLine(cx - 7, cy + 18 + d, cx + 24, cy - 14 + d, ui::TEXT);
        }
    }
    drawCenteredText(S_READY_BANG, 230, FONT_H, ui::GREEN);
    drawCenteredText(S_ENJOY, 280, FONT_M, ui::TEXT);
    drawAccentLine(325, 200, ui::ACCENT);
    if (!drawPng(FS_BASE "/glass.png", (W - 100) / 2, 340)) {
        drawGlass(W / 2, 350, ui::ACCENT, 0.7f);
        drawBubbles(W / 2, 350, ui::TEXT_DIM);
    }
}

bool display_isTouched() {
    int tx, ty;
    return getTouchPoint(tx, ty);
}

bool display_checkTopTap() {
    int tx, ty;
    if (!getTouchPoint(tx, ty)) return false;
    return ty < 60;
}

#pragma once
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_timer.h"
#include "recipes.h"
#include "strings.h"

// ── State exposed from app_main ──
extern const char* bot_state_name();
extern int         bot_cocktails_today();

// ── Statistics ──
static uint32_t stats_total = 0;
static uint32_t stats_recipe[MAX_RECIPES] = {};

void stats_increment(int idx) {
    stats_total++;
    if (idx >= 0 && idx < MAX_RECIPES) stats_recipe[idx]++;
}

static void stats_save() {
    FILE* f = fopen("/spiffs/stats.txt", "w");
    if (!f) return;
    fprintf(f, "%u\n", (unsigned)stats_total);
    for (int i = 0; i < MAX_RECIPES; i++) fprintf(f, "%u\n", (unsigned)stats_recipe[i]);
    fclose(f);
}

static void stats_load() {
    FILE* f = fopen("/spiffs/stats.txt", "r");
    if (!f) return;
    fscanf(f, "%u", &stats_total);
    for (int i = 0; i < MAX_RECIPES; i++) fscanf(f, "%u", &stats_recipe[i]);
    fclose(f);
}

// ── Pump persistence ──
static void pumps_cfg_save() {
    FILE* f = fopen("/spiffs/pumps.txt", "w");
    if (!f) return;
    for (int i = 0; i < MAX_PUMPS; i++) fprintf(f, "%s\n", PUMP_CONTENTS[i]);
    fclose(f);
}

static void pumps_cfg_load() {
    FILE* f = fopen("/spiffs/pumps.txt", "r");
    if (!f) return;
    for (int i = 0; i < MAX_PUMPS; i++) {
        char line[64];
        if (!fgets(line, sizeof(line), f)) break;
        // trim newline
        char* nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (strlen(line) > 0 && strlen(line) < 31)
            strncpy(PUMP_CONTENTS[i], line, 31);
    }
    fclose(f);
}

// ── Bot state ──
static const char* TAG_BOT = "TG_BOT";
static char tg_token[64] = {};
static char tg_wifi_ssid[64] = {};
static char tg_wifi_pass[64] = {};
static int tg_last_update_id = 0;
static bool tg_ready = false;
static EventGroupHandle_t tg_wifi_events = NULL;
#define WIFI_CONNECTED_BIT BIT0

// Replace flow
static char tg_replace_chat[32] = {};
static int tg_replace_pump = -1;

// Calibration flow
static char tg_calib_chat[32] = {};
static int tg_calib_pump = -1;          // which pump is being calibrated
static bool tg_calib_waiting_ml = false; // waiting for user to enter actual ml
#define CALIB_TEST_ML 50                 // how much the pump "thinks" it pours

// HTTP response buffer
#define TG_RESP_BUF_SZ  4096
static char* tg_resp_buf = NULL;
static int tg_resp_len = 0;

static esp_err_t tg_http_event(esp_http_client_event_t* evt) {
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (tg_resp_len + evt->data_len < TG_RESP_BUF_SZ - 1) {
            memcpy(tg_resp_buf + tg_resp_len, evt->data, evt->data_len);
            tg_resp_len += evt->data_len;
        }
        break;
    default: break;
    }
    return ESP_OK;
}

// ── Telegram API calls ──

static cJSON* tg_api_call(const char* method, const char* body) {
    char url[128];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/%s", tg_token, method);

    tg_resp_len = 0;
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.event_handler = tg_http_event;
    cfg.timeout_ms = 10000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (body) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));
    }

    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) return NULL;

    tg_resp_buf[tg_resp_len] = 0;
    return cJSON_Parse(tg_resp_buf);
}

static void tg_send_message(const char* chat_id, const char* text, const char* reply_markup) {
    cJSON* body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "chat_id", chat_id);
    cJSON_AddStringToObject(body, "text", text);
    cJSON_AddStringToObject(body, "parse_mode", "Markdown");
    if (reply_markup && reply_markup[0]) {
        cJSON* rm = cJSON_Parse(reply_markup);
        if (rm) {
            cJSON* wrapper = cJSON_CreateObject();
            cJSON_AddItemToObject(wrapper, "inline_keyboard", rm);
            cJSON_AddItemToObject(body, "reply_markup", wrapper);
        }
    }
    char* json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (json) {
        cJSON* resp = tg_api_call("sendMessage", json);
        if (resp) cJSON_Delete(resp);
        free(json);
    }
}

static void tg_send_photo(const char* chat_id, const char* photo_url, const char* caption) {
    cJSON* body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "chat_id", chat_id);
    cJSON_AddStringToObject(body, "photo", photo_url);
    cJSON_AddStringToObject(body, "caption", caption);
    char* json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (json) {
        cJSON* resp = tg_api_call("sendPhoto", json);
        if (resp) cJSON_Delete(resp);
        free(json);
    }
}

// ── Keyboard builders ──

static const char* KB_MAIN =
    "[[{\"text\":\"" E_COCKTAIL " " S_BOT_MENU "\",\"callback_data\":\"menu\"},"
    "{\"text\":\"" E_WRENCH " " S_BOT_PUMPS "\",\"callback_data\":\"pumps\"}],"
    "[{\"text\":\"" E_CHART " " S_BOT_STATISTICS "\",\"callback_data\":\"stats\"},"
    "{\"text\":\"" E_PLUG " " S_BOT_STATUS "\",\"callback_data\":\"status\"}],"
    "[{\"text\":\"" E_BEAKER " " S_BOT_CALIBRATION "\",\"callback_data\":\"calib\"}],"
    "[{\"text\":\"" E_PENCIL " " S_BOT_EDIT "\",\"callback_data\":\"edit\"}]]";

static const char* KB_BACK =
    "[[{\"text\":\"" E_LAQUO " " S_BOT_BACK "\",\"callback_data\":\"home\"}]]";

// ── Command handlers ──

static void cmd_home(const char* cid) {
    tg_send_message(cid, E_COCKTAIL " *" S_BOT_TITLE "*\n\n" S_BOT_CHOOSE, KB_MAIN);
}

static void cmd_menu(const char* cid) {
    // Build recipe buttons 2 per row
    char kb[2048];
    int pos = 0;
    pos += snprintf(kb + pos, sizeof(kb) - pos, "[");
    for (int i = 0; i < NUM_RECIPES; i++) {
        if (i % 2 == 0) { if (i > 0) pos += snprintf(kb + pos, sizeof(kb) - pos, ","); pos += snprintf(kb + pos, sizeof(kb) - pos, "["); }
        else pos += snprintf(kb + pos, sizeof(kb) - pos, ",");
        pos += snprintf(kb + pos, sizeof(kb) - pos, "{\"text\":\"%d. %s\",\"callback_data\":\"r:%d\"}", i + 1, RECIPES[i].name, i);
        if (i % 2 == 1 || i == NUM_RECIPES - 1) pos += snprintf(kb + pos, sizeof(kb) - pos, "]");
    }
    pos += snprintf(kb + pos, sizeof(kb) - pos, ",[{\"text\":\"" E_LAQUO " " S_BOT_BACK "\",\"callback_data\":\"home\"}]]");
    tg_send_message(cid, E_MEMO " *" S_BOT_MENU_HEADER "*", kb);
}

static void cmd_recipe(const char* cid, int idx) {
    if (idx < 0 || idx >= NUM_RECIPES) return;
    const Recipe& r = RECIPES[idx];
    char msg[512];
    int p = snprintf(msg, sizeof(msg), E_GLASS " *%s*\n\n", r.name);
    for (int i = 0; i < r.numIngredients; i++)
        p += snprintf(msg + p, sizeof(msg) - p, E_BULLET " %s " E_DASH " %d " S_ML "\n", r.ingredients[i].name, r.ingredients[i].baseMl);

    char kb[256];
    snprintf(kb, sizeof(kb),
        "[[{\"text\":\"" E_PHONE " " S_BOT_QR "\",\"callback_data\":\"qr:%d\"}],"
        "[{\"text\":\"" E_LAQUO " " S_BOT_MENU "\",\"callback_data\":\"menu\"},"
        "{\"text\":\"" E_HOUSE " " S_BOT_HOME "\",\"callback_data\":\"home\"}]]", idx);
    tg_send_message(cid, msg, kb);
}

static void cmd_qr(const char* cid, int idx) {
    if (idx < 0 || idx >= NUM_RECIPES) return;
    char url[128], cap[64];
    snprintf(url, sizeof(url), "https://api.qrserver.com/v1/create-qr-code/?size=300x300&data=QR:%d", idx);
    snprintf(cap, sizeof(cap), E_PHONE " %s", RECIPES[idx].name);
    tg_send_photo(cid, url, cap);
    tg_send_message(cid, S_BOT_SHOW_QR_CAM, KB_BACK);
}

static void cmd_pumps(const char* cid) {
    char msg[256];
    int p = snprintf(msg, sizeof(msg), E_WRENCH " *" S_BOT_PUMPS_HEADER "*\n\n");
    for (int i = 0; i < MAX_PUMPS; i++)
        p += snprintf(msg + p, sizeof(msg) - p, "%d. %s\n", i + 1, PUMP_CONTENTS[i]);

    char kb[512];
    int kp = snprintf(kb, sizeof(kb), "[");
    for (int i = 0; i < MAX_PUMPS; i++) {
        if (i % 3 == 0) { if (i > 0) kp += snprintf(kb + kp, sizeof(kb) - kp, ","); kp += snprintf(kb + kp, sizeof(kb) - kp, "["); }
        else kp += snprintf(kb + kp, sizeof(kb) - kp, ",");
        kp += snprintf(kb + kp, sizeof(kb) - kp, "{\"text\":\"" E_PENCIL " %d\",\"callback_data\":\"rp:%d\"}", i + 1, i);
        if (i % 3 == 2 || i == MAX_PUMPS - 1) kp += snprintf(kb + kp, sizeof(kb) - kp, "]");
    }
    kp += snprintf(kb + kp, sizeof(kb) - kp, ",[{\"text\":\"" E_LAQUO " " S_BOT_BACK "\",\"callback_data\":\"home\"}]]");
    tg_send_message(cid, msg, kb);
}

static void cmd_replace_start(const char* cid, int pump) {
    if (pump < 0 || pump >= MAX_PUMPS) return;
    strncpy(tg_replace_chat, cid, 31);
    tg_replace_pump = pump;
    char msg[128];
    snprintf(msg, sizeof(msg), E_PENCIL " " S_BOT_PUMP_N " %d (%s)\n\n" S_BOT_ENTER_NAME, pump + 1, PUMP_CONTENTS[pump]);
    tg_send_message(cid, msg, "[[{\"text\":\"" E_CROSS " " S_BOT_CANCEL "\",\"callback_data\":\"pumps\"}]]");
}

static void cmd_replace_finish(const char* cid, const char* name) {
    if (strlen(name) == 0 || strlen(name) > 30) {
        tg_send_message(cid, E_CROSS " " S_BOT_BAD_NAME, "");
        tg_replace_pump = -1;
        return;
    }
    strncpy(PUMP_CONTENTS[tg_replace_pump], name, 31);
    PUMP_CONTENTS[tg_replace_pump][31] = 0;
    pumps_cfg_save();
    char msg[128];
    snprintf(msg, sizeof(msg), E_CHECK " " S_BOT_PUMP_N " %d " E_ARROW " %s", tg_replace_pump + 1, name);
    tg_replace_pump = -1;
    tg_send_message(cid, msg, KB_BACK);
}

static void cmd_stats(const char* cid) {
    char msg[512];
    int p = snprintf(msg, sizeof(msg), E_CHART " *" S_BOT_STATS_HEADER "*\n\n" S_BOT_TOTAL " %u\n" S_BOT_TODAY " %d\n\n",
        (unsigned)stats_total, bot_cocktails_today());
    bool any = false;
    for (int i = 0; i < NUM_RECIPES; i++) {
        if (stats_recipe[i] > 0) {
            p += snprintf(msg + p, sizeof(msg) - p, "%s: %u\n", RECIPES[i].name, (unsigned)stats_recipe[i]);
            any = true;
        }
    }
    if (!any) p += snprintf(msg + p, sizeof(msg) - p, S_BOT_EMPTY_STATS);
    tg_send_message(cid, msg, KB_BACK);
}

// ── Calibration ──

static void cmd_calib_menu(const char* cid) {
    char msg[512];
    int p = snprintf(msg, sizeof(msg),
        E_BEAKER " *" S_BOT_CALIB_HEADER "*\n\n"
        S_BOT_PUMP_DESC "\n\n", CALIB_TEST_ML);
    for (int i = 0; i < MAX_PUMPS; i++) {
        p += snprintf(msg + p, sizeof(msg) - p, "%d. %s (%.2f " S_ML_SEC ")\n",
            i + 1, PUMP_CONTENTS[i], pumps_getRate(i));
    }

    char kb[512];
    int kp = snprintf(kb, sizeof(kb), "[");
    for (int i = 0; i < MAX_PUMPS; i++) {
        if (i % 3 == 0) { if (i > 0) kp += snprintf(kb + kp, sizeof(kb) - kp, ","); kp += snprintf(kb + kp, sizeof(kb) - kp, "["); }
        else kp += snprintf(kb + kp, sizeof(kb) - kp, ",");
        kp += snprintf(kb + kp, sizeof(kb) - kp,
            "{\"text\":\"" E_BEAKER " %d\",\"callback_data\":\"cal:%d\"}", i + 1, i);
        if (i % 3 == 2 || i == MAX_PUMPS - 1) kp += snprintf(kb + kp, sizeof(kb) - kp, "]");
    }
    kp += snprintf(kb + kp, sizeof(kb) - kp,
        ",[{\"text\":\"" E_LAQUO " " S_BOT_BACK "\",\"callback_data\":\"home\"}]]");
    tg_send_message(cid, msg, kb);
}

static void cmd_calib_start(const char* cid, int pump) {
    if (pump < 0 || pump >= MAX_PUMPS) return;
    tg_calib_pump = pump;
    tg_calib_waiting_ml = false;
    strncpy(tg_calib_chat, cid, 31);

    // Start pump for CALIB_TEST_ML
    pumps_startTask(pump, CALIB_TEST_ML);

    char msg[256];
    snprintf(msg, sizeof(msg),
        E_FAUCET " " S_BOT_PUMP_N " %d " S_BOT_POURING " %d" S_ML "...\n\n"
        S_BOT_MEASURE,
        pump + 1, CALIB_TEST_ML);
    tg_calib_waiting_ml = true;
    tg_send_message(cid, msg,
        "[[{\"text\":\"" E_CROSS " " S_BOT_CANCEL "\",\"callback_data\":\"calib\"}]]");
}

static void cmd_calib_finish(const char* cid, const char* input) {
    float actual_ml = atof(input);
    if (actual_ml <= 0 || actual_ml > 500) {
        tg_send_message(cid, E_CROSS " " S_BOT_BAD_ML, "");
        return;
    }

    int pump = tg_calib_pump;
    float old_rate = pumps_getRate(pump);
    // new_rate = old_rate * (target / actual)
    float new_rate = old_rate * ((float)CALIB_TEST_ML / actual_ml);
    pumps_setRate(pump, new_rate);
    pumps_saveCalibration();

    char msg[256];
    snprintf(msg, sizeof(msg),
        E_CHECK " " S_BOT_PUMP_N " %d " S_BOT_CALIBRATED "\n\n"
        S_BOT_TARGET " %d" S_ML ", " S_BOT_ACTUAL " %.0f" S_ML "\n"
        "%.2f " E_ARROW " %.2f " S_ML_SEC,
        pump + 1, CALIB_TEST_ML, actual_ml, old_rate, new_rate);

    tg_calib_pump = -1;
    tg_calib_waiting_ml = false;
    tg_send_message(cid, msg,
        "[[{\"text\":\"" E_BEAKER " " S_BOT_MORE "\",\"callback_data\":\"calib\"},"
        "{\"text\":\"" E_LAQUO " " S_BOT_HOME "\",\"callback_data\":\"home\"}]]");
}

// ── Recipe editing ──

// Edit state machine
enum EditState : uint8_t {
    EDIT_NONE,
    EDIT_WAIT_RECIPE_NAME,   // new recipe: waiting for name
    EDIT_WAIT_INGR,          // new recipe: waiting for "Name Ml"
    EDIT_WAIT_RENAME,        // rename recipe: waiting for new name
    EDIT_WAIT_ADD_INGR,      // add ingredient to existing recipe
    EDIT_WAIT_EDIT_ML,       // edit ml: waiting for new ml value
};

static EditState tg_edit_state = EDIT_NONE;
static char tg_edit_chat[32] = {};
static int tg_edit_recipe = -1;
static int tg_edit_ingr = -1;

static void cmd_edit_menu(const char* cid) {
    tg_edit_state = EDIT_NONE;
    tg_send_message(cid, E_PENCIL " *" S_BOT_EDIT "*",
        "[[{\"text\":\"" E_MEMO " " S_BOT_RECIPES "\",\"callback_data\":\"elist\"}],"
        "[{\"text\":\"+ " S_BOT_ADD_RECIPE "\",\"callback_data\":\"newrec\"}],"
        "[{\"text\":\"" E_LAQUO " " S_BOT_BACK "\",\"callback_data\":\"home\"}]]");
}

static void cmd_edit_recipe_list(const char* cid) {
    char kb[2048];
    int pos = snprintf(kb, sizeof(kb), "[");
    for (int i = 0; i < NUM_RECIPES; i++) {
        if (i % 2 == 0) { if (i > 0) pos += snprintf(kb + pos, sizeof(kb) - pos, ","); pos += snprintf(kb + pos, sizeof(kb) - pos, "["); }
        else pos += snprintf(kb + pos, sizeof(kb) - pos, ",");
        pos += snprintf(kb + pos, sizeof(kb) - pos, "{\"text\":\"%d. %s\",\"callback_data\":\"er:%d\"}", i + 1, RECIPES[i].name, i);
        if (i % 2 == 1 || i == NUM_RECIPES - 1) pos += snprintf(kb + pos, sizeof(kb) - pos, "]");
    }
    pos += snprintf(kb + pos, sizeof(kb) - pos, ",[{\"text\":\"" E_LAQUO " " S_BOT_BACK "\",\"callback_data\":\"edit\"}]]");
    tg_send_message(cid, E_PENCIL " " S_BOT_RECIPES ":", kb);
}

static void cmd_edit_recipe(const char* cid, int idx) {
    if (idx < 0 || idx >= NUM_RECIPES) return;
    tg_edit_recipe = idx;
    const Recipe& r = RECIPES[idx];

    char msg[512];
    int p = snprintf(msg, sizeof(msg), E_PENCIL " *%s*\n\n", r.name);
    for (int i = 0; i < r.numIngredients; i++)
        p += snprintf(msg + p, sizeof(msg) - p, "%d. %s " E_DASH " %d" S_ML "\n", i + 1, r.ingredients[i].name, r.ingredients[i].baseMl);

    // Buttons for each ingredient + actions
    char kb[1024];
    int kp = snprintf(kb, sizeof(kb), "[");
    for (int i = 0; i < r.numIngredients; i++) {
        if (i > 0) kp += snprintf(kb + kp, sizeof(kb) - kp, ",");
        kp += snprintf(kb + kp, sizeof(kb) - kp,
            "[{\"text\":\"%s " E_PENCIL "\",\"callback_data\":\"eim:%d:%d\"},"
            "{\"text\":\"" E_CROSS "\",\"callback_data\":\"edi:%d:%d\"}]",
            r.ingredients[i].name, idx, i, idx, i);
    }
    kp += snprintf(kb + kp, sizeof(kb) - kp,
        ",[{\"text\":\"+ " S_BOT_ADD_INGR "\",\"callback_data\":\"eai:%d\"}]"
        ",[{\"text\":\"" E_PENCIL " " S_BOT_RENAME "\",\"callback_data\":\"ern:%d\"},"
        "{\"text\":\"" E_CROSS " " S_BOT_DEL_RECIPE "\",\"callback_data\":\"erd:%d\"}]"
        ",[{\"text\":\"" E_LAQUO " " S_BOT_BACK "\",\"callback_data\":\"elist\"}]]",
        idx, idx, idx);
    tg_send_message(cid, msg, kb);
}

static void cmd_new_recipe_start(const char* cid) {
    tg_edit_state = EDIT_WAIT_RECIPE_NAME;
    strncpy(tg_edit_chat, cid, 31);
    tg_send_message(cid, S_BOT_ENTER_RECIPE,
        "[[{\"text\":\"" E_CROSS " " S_CANCEL "\",\"callback_data\":\"edit\"}]]");
}

static void cmd_new_recipe_name(const char* cid, const char* name) {
    if (NUM_RECIPES >= MAX_RECIPES) {
        tg_send_message(cid, E_CROSS " " S_BOT_MAX_REACHED, "");
        tg_edit_state = EDIT_NONE;
        return;
    }
    tg_edit_recipe = NUM_RECIPES;
    Recipe& r = RECIPES[NUM_RECIPES];
    memset(&r, 0, sizeof(Recipe));
    strncpy(r.name, name, NAME_LEN - 1);
    NUM_RECIPES++;

    tg_edit_state = EDIT_WAIT_INGR;
    char msg[128];
    snprintf(msg, sizeof(msg), E_CHECK " *%s*\n\n" S_BOT_ENTER_INGR, name);
    tg_send_message(cid, msg,
        "[[{\"text\":\"" E_CHECK " " S_BOT_DONE "\",\"callback_data\":\"edone\"}]]");
}

static void cmd_add_ingredient(const char* cid, const char* input) {
    if (tg_edit_recipe < 0 || tg_edit_recipe >= NUM_RECIPES) return;
    Recipe& r = RECIPES[tg_edit_recipe];
    if (r.numIngredients >= MAX_INGR) {
        tg_send_message(cid, E_CROSS " " S_BOT_MAX_REACHED, "");
        return;
    }

    // Parse "Name Ml" — find last space before number
    char nameBuf[NAME_LEN] = {};
    int ml = 0;
    const char* lastSpace = strrchr(input, ' ');
    if (!lastSpace || lastSpace == input) {
        tg_send_message(cid, E_CROSS " " S_BOT_BAD_FORMAT "\n" S_BOT_ENTER_INGR, "");
        return;
    }
    ml = atoi(lastSpace + 1);
    if (ml <= 0 || ml > 999) {
        tg_send_message(cid, E_CROSS " " S_BOT_BAD_FORMAT "\n" S_BOT_ENTER_INGR, "");
        return;
    }
    int nameLen = lastSpace - input;
    if (nameLen >= NAME_LEN) nameLen = NAME_LEN - 1;
    memcpy(nameBuf, input, nameLen);
    nameBuf[nameLen] = 0;

    Ingredient& ing = r.ingredients[r.numIngredients++];
    strncpy(ing.name, nameBuf, NAME_LEN - 1);
    ing.baseMl = ml;

    char msg[128];
    snprintf(msg, sizeof(msg), E_CHECK " %s %d" S_ML "\n" S_BOT_ENTER_INGR, nameBuf, ml);
    tg_send_message(cid, msg,
        "[[{\"text\":\"" E_CHECK " " S_BOT_DONE "\",\"callback_data\":\"edone\"}]]");
}

static void cmd_edit_done(const char* cid) {
    tg_edit_state = EDIT_NONE;
    recipes_save();
    tg_send_message(cid, E_CHECK " " S_BOT_SAVED, KB_BACK);
}

static void cmd_delete_recipe(const char* cid, int idx) {
    if (idx < 0 || idx >= NUM_RECIPES) return;
    // Shift recipes down
    for (int i = idx; i < NUM_RECIPES - 1; i++)
        RECIPES[i] = RECIPES[i + 1];
    NUM_RECIPES--;
    recipes_save();
    tg_send_message(cid, E_CHECK " " S_BOT_RECIPE_DEL,
        "[[{\"text\":\"" E_LAQUO " " S_BOT_BACK "\",\"callback_data\":\"elist\"}]]");
}

static void cmd_rename_start(const char* cid, int idx) {
    if (idx < 0 || idx >= NUM_RECIPES) return;
    tg_edit_state = EDIT_WAIT_RENAME;
    tg_edit_recipe = idx;
    strncpy(tg_edit_chat, cid, 31);
    char msg[128];
    snprintf(msg, sizeof(msg), S_BOT_RENAME " *%s*\n" S_BOT_ENTER_RECIPE, RECIPES[idx].name);
    char kb[128];
    snprintf(kb, sizeof(kb), "[[{\"text\":\"" E_CROSS " " S_CANCEL "\",\"callback_data\":\"er:%d\"}]]", idx);
    tg_send_message(cid, msg, kb);
}

static void cmd_rename_finish(const char* cid, const char* name) {
    if (tg_edit_recipe < 0 || tg_edit_recipe >= NUM_RECIPES) return;
    strncpy(RECIPES[tg_edit_recipe].name, name, NAME_LEN - 1);
    recipes_save();
    tg_edit_state = EDIT_NONE;
    char msg[128];
    snprintf(msg, sizeof(msg), E_CHECK " " S_BOT_RENAMED ": %s", name);
    tg_send_message(cid, msg,
        "[[{\"text\":\"" E_LAQUO " " S_BOT_BACK "\",\"callback_data\":\"elist\"}]]");
}

static void cmd_delete_ingr(const char* cid, int recIdx, int ingrIdx) {
    if (recIdx < 0 || recIdx >= NUM_RECIPES) return;
    Recipe& r = RECIPES[recIdx];
    if (ingrIdx < 0 || ingrIdx >= r.numIngredients || r.numIngredients <= 1) return;
    for (int i = ingrIdx; i < r.numIngredients - 1; i++)
        r.ingredients[i] = r.ingredients[i + 1];
    r.numIngredients--;
    recipes_save();
    cmd_edit_recipe(cid, recIdx);
}

static void cmd_add_ingr_start(const char* cid, int recIdx) {
    if (recIdx < 0 || recIdx >= NUM_RECIPES) return;
    tg_edit_state = EDIT_WAIT_ADD_INGR;
    tg_edit_recipe = recIdx;
    strncpy(tg_edit_chat, cid, 31);
    char kb[128];
    snprintf(kb, sizeof(kb), "[[{\"text\":\"" E_CROSS " " S_CANCEL "\",\"callback_data\":\"er:%d\"}]]", recIdx);
    tg_send_message(cid, S_BOT_ENTER_INGR, kb);
}

static void cmd_edit_ml_start(const char* cid, int recIdx, int ingrIdx) {
    if (recIdx < 0 || recIdx >= NUM_RECIPES) return;
    Recipe& r = RECIPES[recIdx];
    if (ingrIdx < 0 || ingrIdx >= r.numIngredients) return;
    tg_edit_state = EDIT_WAIT_EDIT_ML;
    tg_edit_recipe = recIdx;
    tg_edit_ingr = ingrIdx;
    strncpy(tg_edit_chat, cid, 31);
    char msg[128];
    snprintf(msg, sizeof(msg), "%s: %d" S_ML "\n" S_BOT_ENTER_ML,
        r.ingredients[ingrIdx].name, r.ingredients[ingrIdx].baseMl);
    char kb[128];
    snprintf(kb, sizeof(kb), "[[{\"text\":\"" E_CROSS " " S_CANCEL "\",\"callback_data\":\"er:%d\"}]]", recIdx);
    tg_send_message(cid, msg, kb);
}

static void cmd_edit_ml_finish(const char* cid, const char* input) {
    int ml = atoi(input);
    if (ml <= 0 || ml > 999) {
        tg_send_message(cid, E_CROSS " " S_BOT_BAD_FORMAT, "");
        return;
    }
    if (tg_edit_recipe >= 0 && tg_edit_recipe < NUM_RECIPES &&
        tg_edit_ingr >= 0 && tg_edit_ingr < RECIPES[tg_edit_recipe].numIngredients) {
        RECIPES[tg_edit_recipe].ingredients[tg_edit_ingr].baseMl = ml;
        recipes_save();
    }
    tg_edit_state = EDIT_NONE;
    char msg[64];
    snprintf(msg, sizeof(msg), E_CHECK " " S_BOT_ML_UPDATED ": %d" S_ML, ml);
    tg_send_message(cid, msg,
        "[[{\"text\":\"" E_LAQUO " " S_BOT_BACK "\",\"callback_data\":\"elist\"}]]");
}

static void cmd_status(const char* cid) {
    esp_netif_ip_info_t ip_info;
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_get_ip_info(netif, &ip_info);

    wifi_ap_record_t ap;
    esp_wifi_sta_get_ap_info(&ap);

    char msg[256];
    snprintf(msg, sizeof(msg),
        E_PLUG " *" S_BOT_STATUS_HEADER "* %s\n"
        E_SIGNAL " " S_BOT_IP " " IPSTR "\n"
        E_FREE " " S_BOT_RSSI " %d " S_BOT_DBM "\n"
        E_COCKTAIL " " S_BOT_TODAY " %d",
        bot_state_name(), IP2STR(&ip_info.ip), ap.rssi, bot_cocktails_today());
    tg_send_message(cid, msg, KB_BACK);
}

// ── Message dispatcher ──

static void tg_handle_update(cJSON* update) {
    cJSON* msg_obj = cJSON_GetObjectItem(update, "message");
    cJSON* cb_obj = cJSON_GetObjectItem(update, "callback_query");

    if (cb_obj) {
        cJSON* data = cJSON_GetObjectItem(cb_obj, "data");
        cJSON* from_msg = cJSON_GetObjectItem(cb_obj, "message");
        cJSON* chat = from_msg ? cJSON_GetObjectItem(from_msg, "chat") : NULL;
        cJSON* chat_id = chat ? cJSON_GetObjectItem(chat, "id") : NULL;
        if (!data || !chat_id) return;

        char cid[32];
        snprintf(cid, sizeof(cid), "%lld", (long long)chat_id->valuedouble);
        const char* d = data->valuestring;

        if (strcmp(d, "home") == 0) cmd_home(cid);
        else if (strcmp(d, "menu") == 0) cmd_menu(cid);
        else if (strcmp(d, "pumps") == 0) { tg_replace_pump = -1; cmd_pumps(cid); }
        else if (strcmp(d, "stats") == 0) cmd_stats(cid);
        else if (strcmp(d, "status") == 0) cmd_status(cid);
        else if (strncmp(d, "r:", 2) == 0) cmd_recipe(cid, atoi(d + 2));
        else if (strncmp(d, "qr:", 3) == 0) cmd_qr(cid, atoi(d + 3));
        else if (strncmp(d, "rp:", 3) == 0) cmd_replace_start(cid, atoi(d + 3));
        else if (strcmp(d, "calib") == 0) { tg_calib_pump = -1; tg_calib_waiting_ml = false; cmd_calib_menu(cid); }
        else if (strncmp(d, "cal:", 4) == 0) cmd_calib_start(cid, atoi(d + 4));
        // Recipe editing
        else if (strcmp(d, "edit") == 0) cmd_edit_menu(cid);
        else if (strcmp(d, "elist") == 0) cmd_edit_recipe_list(cid);
        else if (strncmp(d, "er:", 3) == 0) cmd_edit_recipe(cid, atoi(d + 3));
        else if (strcmp(d, "newrec") == 0) cmd_new_recipe_start(cid);
        else if (strcmp(d, "edone") == 0) cmd_edit_done(cid);
        else if (strncmp(d, "erd:", 4) == 0) cmd_delete_recipe(cid, atoi(d + 4));
        else if (strncmp(d, "ern:", 4) == 0) cmd_rename_start(cid, atoi(d + 4));
        else if (strncmp(d, "eai:", 4) == 0) cmd_add_ingr_start(cid, atoi(d + 4));
        else if (strncmp(d, "edi:", 4) == 0) {
            // edi:recIdx:ingrIdx
            int ri = atoi(d + 4);
            const char* colon = strchr(d + 4, ':');
            int ii = colon ? atoi(colon + 1) : -1;
            cmd_delete_ingr(cid, ri, ii);
        }
        else if (strncmp(d, "eim:", 4) == 0) {
            // eim:recIdx:ingrIdx
            int ri = atoi(d + 4);
            const char* colon = strchr(d + 4, ':');
            int ii = colon ? atoi(colon + 1) : -1;
            cmd_edit_ml_start(cid, ri, ii);
        }
    }
    else if (msg_obj) {
        cJSON* chat = cJSON_GetObjectItem(msg_obj, "chat");
        cJSON* chat_id = chat ? cJSON_GetObjectItem(chat, "id") : NULL;
        cJSON* text = cJSON_GetObjectItem(msg_obj, "text");
        if (!chat_id || !text) return;

        char cid[32];
        snprintf(cid, sizeof(cid), "%lld", (long long)chat_id->valuedouble);
        const char* t = text->valuestring;

        if (strcmp(t, "/start") == 0 || strcmp(t, "/help") == 0) {
            cmd_home(cid);
        } else if (tg_edit_state != EDIT_NONE && strcmp(cid, tg_edit_chat) == 0) {
            switch (tg_edit_state) {
            case EDIT_WAIT_RECIPE_NAME: cmd_new_recipe_name(cid, t); break;
            case EDIT_WAIT_INGR:
            case EDIT_WAIT_ADD_INGR:    cmd_add_ingredient(cid, t); break;
            case EDIT_WAIT_RENAME:      cmd_rename_finish(cid, t); break;
            case EDIT_WAIT_EDIT_ML:     cmd_edit_ml_finish(cid, t); break;
            default: break;
            }
        } else if (tg_calib_waiting_ml && tg_calib_pump >= 0 && strcmp(cid, tg_calib_chat) == 0) {
            cmd_calib_finish(cid, t);
        } else if (tg_replace_pump >= 0 && strcmp(cid, tg_replace_chat) == 0) {
            cmd_replace_finish(cid, t);
        }
    }
}

// ── WiFi ──

static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(tg_wifi_events, WIFI_CONNECTED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(tg_wifi_events, WIFI_CONNECTED_BIT);
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)data;
        ESP_LOGI(TAG_BOT, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static bool tg_read_config() {
    FILE* f = fopen("/spiffs/wifi.txt", "r");
    if (!f) { ESP_LOGE(TAG_BOT, "wifi.txt not found"); return false; }
    char line[64];
    if (fgets(line, sizeof(line), f)) { char* nl = strchr(line, '\n'); if (nl) *nl = 0; strncpy(tg_wifi_ssid, line, 63); }
    if (fgets(line, sizeof(line), f)) { char* nl = strchr(line, '\n'); if (nl) *nl = 0; strncpy(tg_wifi_pass, line, 63); }
    fclose(f);

    f = fopen("/spiffs/bot.txt", "r");
    if (!f) { ESP_LOGE(TAG_BOT, "bot.txt not found"); return false; }
    if (fgets(line, sizeof(line), f)) { char* nl = strchr(line, '\n'); if (nl) *nl = 0; strncpy(tg_token, line, 63); }
    fclose(f);

    return strlen(tg_wifi_ssid) > 0 && strlen(tg_token) > 0;
}

static void tg_wifi_init() {
    tg_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

    wifi_config_t wcfg = {};
    strncpy((char*)wcfg.sta.ssid, tg_wifi_ssid, 31);
    strncpy((char*)wcfg.sta.password, tg_wifi_pass, 63);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_BOT, "Connecting to %s...", tg_wifi_ssid);
    xEventGroupWaitBits(tg_wifi_events, WIFI_CONNECTED_BIT, false, true, pdMS_TO_TICKS(10000));
}

// ── Public API ──

void bot_init() {
    if (!tg_read_config()) {
        ESP_LOGW(TAG_BOT, "Config missing, bot disabled");
        return;
    }

    tg_resp_buf = (char*)malloc(TG_RESP_BUF_SZ);
    if (!tg_resp_buf) return;

    tg_wifi_init();

    stats_load();
    pumps_cfg_load();

    tg_ready = true;
    ESP_LOGI(TAG_BOT, "Bot ready");
}

void bot_update() {
    if (!tg_ready) return;
    if (!(xEventGroupGetBits(tg_wifi_events) & WIFI_CONNECTED_BIT)) return;

    char url_params[64];
    snprintf(url_params, sizeof(url_params), "?offset=%d&timeout=0", tg_last_update_id + 1);

    char method[80];
    snprintf(method, sizeof(method), "getUpdates%s", url_params);

    cJSON* resp = tg_api_call(method, NULL);
    if (!resp) return;

    cJSON* ok = cJSON_GetObjectItem(resp, "ok");
    cJSON* result = cJSON_GetObjectItem(resp, "result");
    if (ok && cJSON_IsTrue(ok) && result && cJSON_IsArray(result)) {
        int n = cJSON_GetArraySize(result);
        for (int i = 0; i < n; i++) {
            cJSON* update = cJSON_GetArrayItem(result, i);
            cJSON* uid = cJSON_GetObjectItem(update, "update_id");
            if (uid) tg_last_update_id = uid->valueint;
            tg_handle_update(update);
        }
    }
    cJSON_Delete(resp);
}

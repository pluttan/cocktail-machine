#include "recipes.h"
#include <cstdio>
#include "cJSON.h"
#include "esp_log.h"

static const char* TAG = "RECIPES";

#define RECIPES_PATH "/spiffs/recipes.json"

// Runtime storage
char PUMP_CONTENTS[MAX_PUMPS][NAME_LEN] = {};
Recipe RECIPES[MAX_RECIPES] = {};
int NUM_RECIPES = 0;

// ── Load from JSON ──

bool recipes_load() {
    FILE* f = fopen(RECIPES_PATH, "r");
    if (!f) {
        ESP_LOGW(TAG, "recipes.json not found, using defaults");
        return false;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buf = (char*)malloc(len + 1);
    if (!buf) { fclose(f); return false; }
    fread(buf, 1, len, f);
    buf[len] = 0;
    fclose(f);

    cJSON* root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse error");
        return false;
    }

    // Pumps
    cJSON* pumps = cJSON_GetObjectItem(root, "pumps");
    if (pumps && cJSON_IsArray(pumps)) {
        int n = cJSON_GetArraySize(pumps);
        if (n > MAX_PUMPS) n = MAX_PUMPS;
        for (int i = 0; i < n; i++) {
            cJSON* p = cJSON_GetArrayItem(pumps, i);
            if (p && p->valuestring)
                strncpy(PUMP_CONTENTS[i], p->valuestring, NAME_LEN - 1);
        }
    }

    // Recipes
    cJSON* recipes = cJSON_GetObjectItem(root, "recipes");
    if (recipes && cJSON_IsArray(recipes)) {
        NUM_RECIPES = cJSON_GetArraySize(recipes);
        if (NUM_RECIPES > MAX_RECIPES) NUM_RECIPES = MAX_RECIPES;

        for (int i = 0; i < NUM_RECIPES; i++) {
            cJSON* r = cJSON_GetArrayItem(recipes, i);
            Recipe& rec = RECIPES[i];
            memset(&rec, 0, sizeof(Recipe));

            // Name
            cJSON* name = cJSON_GetObjectItem(r, "name");
            if (name && name->valuestring)
                strncpy(rec.name, name->valuestring, NAME_LEN - 1);

            // Ingredients
            cJSON* ingr = cJSON_GetObjectItem(r, "ingredients");
            if (ingr && cJSON_IsArray(ingr)) {
                rec.numIngredients = cJSON_GetArraySize(ingr);
                if (rec.numIngredients > MAX_INGR) rec.numIngredients = MAX_INGR;
                for (int j = 0; j < rec.numIngredients; j++) {
                    cJSON* item = cJSON_GetArrayItem(ingr, j);
                    cJSON* n = cJSON_GetObjectItem(item, "name");
                    cJSON* ml = cJSON_GetObjectItem(item, "ml");
                    if (n && n->valuestring)
                        strncpy(rec.ingredients[j].name, n->valuestring, NAME_LEN - 1);
                    if (ml)
                        rec.ingredients[j].baseMl = (uint16_t)ml->valuedouble;
                }
            }

            // Strength options
            cJSON* str = cJSON_GetObjectItem(r, "strength");
            if (str && cJSON_IsArray(str)) {
                rec.numStrengths = cJSON_GetArraySize(str);
                if (rec.numStrengths > MAX_INGR) rec.numStrengths = MAX_INGR;
                for (int j = 0; j < rec.numStrengths; j++) {
                    cJSON* s = cJSON_GetArrayItem(str, j);
                    StrengthOpt& opt = rec.strengths[j];

                    cJSON* idx = cJSON_GetObjectItem(s, "idx");
                    if (idx) opt.ingredIdx = (uint8_t)idx->valuedouble;

                    cJSON* def = cJSON_GetObjectItem(s, "def");
                    if (def) opt.defaultIdx = (uint8_t)def->valuedouble;

                    cJSON* pts = cJSON_GetObjectItem(s, "points");
                    if (pts && cJSON_IsArray(pts)) {
                        opt.numPoints = cJSON_GetArraySize(pts);
                        if (opt.numPoints > MAX_POINTS) opt.numPoints = MAX_POINTS;
                        for (int k = 0; k < opt.numPoints; k++) {
                            cJSON* v = cJSON_GetArrayItem(pts, k);
                            if (v) opt.points[k] = (float)v->valuedouble;
                        }
                    }
                }
            }
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded %d recipes, %d pumps", NUM_RECIPES, MAX_PUMPS);
    return true;
}

// ── Save to JSON ──

bool recipes_save() {
    cJSON* root = cJSON_CreateObject();

    // Pumps
    cJSON* pumps = cJSON_AddArrayToObject(root, "pumps");
    for (int i = 0; i < MAX_PUMPS; i++)
        cJSON_AddItemToArray(pumps, cJSON_CreateString(PUMP_CONTENTS[i]));

    // Recipes
    cJSON* recipes = cJSON_AddArrayToObject(root, "recipes");
    for (int i = 0; i < NUM_RECIPES; i++) {
        const Recipe& rec = RECIPES[i];
        cJSON* r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "name", rec.name);

        cJSON* ingr = cJSON_AddArrayToObject(r, "ingredients");
        for (int j = 0; j < rec.numIngredients; j++) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "name", rec.ingredients[j].name);
            cJSON_AddNumberToObject(item, "ml", rec.ingredients[j].baseMl);
            cJSON_AddItemToArray(ingr, item);
        }

        cJSON* str = cJSON_AddArrayToObject(r, "strength");
        for (int j = 0; j < rec.numStrengths; j++) {
            const StrengthOpt& opt = rec.strengths[j];
            cJSON* s = cJSON_CreateObject();
            cJSON_AddNumberToObject(s, "idx", opt.ingredIdx);
            cJSON_AddNumberToObject(s, "def", opt.defaultIdx);
            cJSON* pts = cJSON_AddArrayToObject(s, "points");
            for (int k = 0; k < opt.numPoints; k++)
                cJSON_AddItemToArray(pts, cJSON_CreateNumber(opt.points[k]));
            cJSON_AddItemToArray(str, s);
        }

        cJSON_AddItemToArray(recipes, r);
    }

    char* json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json) return false;

    FILE* f = fopen(RECIPES_PATH, "w");
    if (!f) { free(json); return false; }
    fputs(json, f);
    fclose(f);

    ESP_LOGI(TAG, "Saved %d recipes (%d bytes)", NUM_RECIPES, (int)strlen(json));
    free(json);
    return true;
}

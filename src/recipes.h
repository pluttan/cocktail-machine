#pragma once
#include <cstdint>
#include <cstring>

#define MAX_PUMPS   6
#define MAX_RECIPES 30
#define MAX_INGR    8
#define MAX_POINTS  9
#define NAME_LEN    32

struct Ingredient {
    char name[NAME_LEN];
    uint16_t baseMl;
};

struct StrengthOpt {
    uint8_t ingredIdx;
    float points[MAX_POINTS];
    uint8_t numPoints;
    uint8_t defaultIdx;
};

struct Recipe {
    char name[NAME_LEN];
    Ingredient ingredients[MAX_INGR];
    uint8_t numIngredients;
    StrengthOpt strengths[MAX_INGR];
    uint8_t numStrengths;
};

// Runtime data
extern char PUMP_CONTENTS[MAX_PUMPS][NAME_LEN];
extern Recipe RECIPES[MAX_RECIPES];
extern int NUM_RECIPES;

// API
bool recipes_load();
bool recipes_save();

inline int findPump(const char* name) {
    for (int i = 0; i < MAX_PUMPS; i++)
        if (strcmp(PUMP_CONTENTS[i], name) == 0) return i;
    return -1;
}

inline const StrengthOpt* findStrengthOpt(const Recipe& r, uint8_t ingredIdx) {
    for (int i = 0; i < r.numStrengths; i++)
        if (r.strengths[i].ingredIdx == ingredIdx) return &r.strengths[i];
    return nullptr;
}

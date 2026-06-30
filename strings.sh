#!/bin/bash
# ══════════════════════════════════════════════════════
# String localization — change texts here, then run:
#   ./strings.sh
# Generates src/strings.h with UTF-8 escaped C defines
# ══════════════════════════════════════════════════════

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

python3 - "$SCRIPT_DIR/src/strings.h" << 'PYEOF'
import sys

output_path = sys.argv[1]

# ── All UI strings: KEY = "readable text" ──
# Groups: display, bot, recipes, ingredients, emojis

strings = {
    # ── Display UI ──
    "S_COCKTAIL":         "COCKTAIL",
    "S_MACHINE":          "MACHINE",
    "S_SHOW_QR":          "Покажите QR-код",
    "S_TODAY":            "за сегодня",
    "S_ML":               "мл",
    "S_ML_SEC":           "мл/с",
    "S_READY":            "ГОТОВО",
    "S_READY_BANG":       "ГОТОВО!",
    "S_CANCEL":           "Отмена",
    "S_STOP":             "СТОП",
    "S_BOTTLE_EMPTY":     "БУТЫЛКА ПУСТА",
    "S_REPLACE_COLON":    "Замените: %s",
    "S_THEN_CONTINUE":    "Затем нажмите Продолжить",
    "S_CONTINUE":         "Продолжить",
    "S_ENJOY":            "Наслаждайтесь!",
    "S_PCT_FMT":          "%d%%",
    "S_N_TODAY_FMT":      "%d за сегодня",
    "S_ML_FMT":           "%dмл",
    "S_INGR_ML_FMT":      "%s  %dml",

    # ── Bot UI ──
    "S_BOT_TITLE":        "Cocktail Machine",
    "S_BOT_CHOOSE":       "Выберите действие:",
    "S_BOT_MENU":         "Меню",
    "S_BOT_PUMPS":        "Насосы",
    "S_BOT_STATISTICS":   "Статистика",
    "S_BOT_STATUS":       "Статус",
    "S_BOT_CALIBRATION":  "Калибровка",
    "S_BOT_BACK":         "Назад",
    "S_BOT_HOME":         "Главная",
    "S_BOT_MORE":         "Ещё",
    "S_BOT_CANCEL":       "Отмена",
    "S_BOT_SHOW_QR_CAM":  "Покажите QR камере",
    "S_BOT_MENU_HEADER":  "Меню:",
    "S_BOT_PUMPS_HEADER": "Насосы:",
    "S_BOT_STATS_HEADER": "Статистика:",
    "S_BOT_STATUS_HEADER":"Статус:",
    "S_BOT_CALIB_HEADER": "Калибровка:",
    "S_BOT_TODAY":        "Сегодня:",
    "S_BOT_TOTAL":        "Всего:",
    "S_BOT_EMPTY_STATS":  "Пока пусто",
    "S_BOT_BAD_NAME":     "Некорректное имя",
    "S_BOT_BAD_ML":       "Введите число мл (1-500)",
    "S_BOT_PUMP_N":       "Насос",
    "S_BOT_CALIBRATED":   "откалиброван",
    "S_BOT_TARGET":       "Цель:",
    "S_BOT_ACTUAL":       "реально:",
    "S_BOT_ENTER_NAME":   "Введите новое имя:",
    "S_BOT_POURING":      "наливает",
    "S_BOT_MEASURE":      "Когда закончит — измерьте и введите сколько мл реально налилось:",
    "S_BOT_PUMP_DESC":    "Помпа нальёт %dмл, вы измерите сколько реально.",
    "S_BOT_IP":           "IP:",
    "S_BOT_RSSI":         "RSSI:",
    "S_BOT_DBM":          "dBm",
    "S_BOT_QR":           "QR",

    # ── Recipe editing ──
    "S_BOT_EDIT":         "Редактирование",
    "S_BOT_RECIPES":      "Рецепты",
    "S_BOT_INGREDIENTS":  "Ингредиенты",
    "S_BOT_ADD_RECIPE":   "Новый рецепт",
    "S_BOT_DEL_RECIPE":   "Удалить",
    "S_BOT_RENAME":       "Переименовать",
    "S_BOT_ADD_INGR":     "Добавить ингредиент",
    "S_BOT_DEL_INGR":     "Убрать",
    "S_BOT_EDIT_ML":      "Изменить мл",
    "S_BOT_ENTER_RECIPE": "Введите название рецепта:",
    "S_BOT_ENTER_INGR":   "Введите: Название Мл (например: Водка 50)",
    "S_BOT_ENTER_ML":     "Введите новый объём в мл:",
    "S_BOT_RECIPE_ADDED": "Рецепт добавлен",
    "S_BOT_RECIPE_DEL":   "Рецепт удалён",
    "S_BOT_INGR_ADDED":   "Ингредиент добавлен",
    "S_BOT_INGR_DEL":     "Ингредиент удалён",
    "S_BOT_RENAMED":      "Переименовано",
    "S_BOT_ML_UPDATED":   "Объём обновлён",
    "S_BOT_DONE":         "Готово",
    "S_BOT_SAVED":        "Сохранено",
    "S_BOT_MAX_REACHED":  "Достигнут максимум",
    "S_BOT_BAD_FORMAT":   "Неверный формат",

    # ── Emojis ──
    "E_COCKTAIL":    "🍹",
    "E_GLASS":       "🍸",
    "E_WRENCH":      "🔧",
    "E_CHART":       "📊",
    "E_PLUG":        "🔌",
    "E_BEAKER":      "🧪",
    "E_PHONE":       "📱",
    "E_HOUSE":       "🏠",
    "E_MEMO":        "📋",
    "E_SIGNAL":      "📶",
    "E_FREE":        "🆓",
    "E_FAUCET":      "🚰",
    "E_PENCIL":      "✏",
    "E_CROSS":       "❌",
    "E_CHECK":       "✅",
    "E_BULLET":      "•",
    "E_DASH":        "—",
    "E_ARROW":       "→",
    "E_LAQUO":       "«",

    # ── Ingredient names ──
    "N_VODKA":       "Водка",
    "N_RUM":         "Ром",
    "N_GIN":         "Джин",
    "N_OJ":          "Апельсин. сок",
    "N_COLA":        "Кола",
    "N_TONIC":       "Тоник",

    # ── Recipe names ──
    "R_SCREWDRIVER":     "Отвёртка",
    "R_CUBA_LIBRE":      "Куба Либре",
    "R_GIN_TONIC":       "Джин-тоник",
    "R_RUM_COLA":        "Ром-кола",
    "R_VODKA_TONIC":     "Водка-тоник",
    "R_SUNSET":          "Закат",
    "R_LONG_ISLAND":     "Лонг Айленд",
    "R_TEQUILA_SUNRISE": "Текила Санрайз",
    "R_MOJITO":          "Мохито",
    "R_BLUE_LAGOON":     "Голубая Лагуна",
    "R_PINA_COLADA":     "Пина Колада",
    "R_ORANGE_PARADISE": "Апельс. Рай",
    "R_TROPICAL_BREEZE": "Тропич. Бриз",
    "R_COLA_MIX":        "Кола Микс",
    "R_TONIC_DELUXE":    "Тоник Делюкс",
    "R_FRUIT_PUNCH":     "Фрукт. Пунш",
    "R_EVENING_BREEZE":  "Вечерний Бриз",
    "R_COCONUT_DAWN":    "Кокос. Рассвет",
    "R_CITRUS_STORM":    "Цитрус. Шторм",
    "R_CLASSIC_MIX":     "Классич. Микс",
}


def to_c_escape(s):
    """Convert UTF-8 string to C hex escape sequence."""
    result = []
    for byte in s.encode("utf-8"):
        if 0x20 <= byte < 0x7F and byte != ord('"') and byte != ord('\\'):
            result.append(chr(byte))
        else:
            result.append(f"\\x{byte:02x}")
    return "".join(result)


def to_json_escape(s):
    """Convert string to JSON unicode escapes (for Telegram inline keyboards)."""
    result = []
    for ch in s:
        cp = ord(ch)
        if 0x20 <= cp < 0x7F and ch not in ('"', '\\'):
            result.append(ch)
        elif cp <= 0xFFFF:
            result.append(f"\\u{cp:04x}")
        else:
            # UTF-16 surrogate pair
            cp -= 0x10000
            hi = 0xD800 + (cp >> 10)
            lo = 0xDC00 + (cp & 0x3FF)
            result.append(f"\\u{hi:04x}\\u{lo:04x}")
    return "".join(result)


lines = [
    "#pragma once",
    "// Auto-generated by strings.sh — DO NOT EDIT MANUALLY",
    "",
    "// ── UTF-8 C strings (for display, logs, HTTP bodies) ──",
    "",
]

for key, val in strings.items():
    esc = to_c_escape(val)
    lines.append(f'#define {key:<24} "{esc}"')

lines.append("")
lines.append("// ── JSON unicode escapes (for Telegram inline keyboard JSON) ──")
lines.append("")

for key, val in strings.items():
    jesc = to_json_escape(val)
    lines.append(f'#define J_{key:<22} "{jesc}"')

lines.append("")

with open(output_path, "w") as f:
    f.write("\n".join(lines) + "\n")

print(f"Generated {output_path} ({len(strings)} strings)")
PYEOF

echo "Done!"

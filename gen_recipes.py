#!/usr/bin/env python3
"""Generate default recipes.json for LittleFS."""
import json, os

recipes = [
    {"name": "Отвёртка",         "ingredients": [{"name": "Водка", "ml": 50}, {"name": "Апельсин. сок", "ml": 150}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Куба Либре",       "ingredients": [{"name": "Ром", "ml": 50}, {"name": "Кола", "ml": 150}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Джин-тоник",       "ingredients": [{"name": "Джин", "ml": 50}, {"name": "Тоник", "ml": 150}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Ром-кола",         "ingredients": [{"name": "Ром", "ml": 60}, {"name": "Кола", "ml": 140}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Водка-тоник",      "ingredients": [{"name": "Водка", "ml": 50}, {"name": "Тоник", "ml": 150}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Закат",            "ingredients": [{"name": "Водка", "ml": 30}, {"name": "Ром", "ml": 20}, {"name": "Апельсин. сок", "ml": 100}, {"name": "Тоник", "ml": 50}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}, {"idx": 1, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Лонг Айленд",      "ingredients": [{"name": "Водка", "ml": 20}, {"name": "Ром", "ml": 20}, {"name": "Джин", "ml": 20}, {"name": "Апельсин. сок", "ml": 60}, {"name": "Кола", "ml": 80}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}, {"idx": 1, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}, {"idx": 2, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Текила Санрайз",   "ingredients": [{"name": "Водка", "ml": 50}, {"name": "Апельсин. сок", "ml": 150}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Мохито",           "ingredients": [{"name": "Ром", "ml": 50}, {"name": "Апельсин. сок", "ml": 50}, {"name": "Тоник", "ml": 100}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Голубая Лагуна",   "ingredients": [{"name": "Водка", "ml": 40}, {"name": "Апельсин. сок", "ml": 100}, {"name": "Тоник", "ml": 60}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Пина Колада",      "ingredients": [{"name": "Ром", "ml": 50}, {"name": "Апельсин. сок", "ml": 100}, {"name": "Тоник", "ml": 50}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Апельс. Рай",      "ingredients": [{"name": "Водка", "ml": 30}, {"name": "Джин", "ml": 20}, {"name": "Апельсин. сок", "ml": 150}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}, {"idx": 1, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Тропич. Бриз",     "ingredients": [{"name": "Водка", "ml": 20}, {"name": "Ром", "ml": 30}, {"name": "Апельсин. сок", "ml": 100}, {"name": "Тоник", "ml": 50}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}, {"idx": 1, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Кола Микс",        "ingredients": [{"name": "Водка", "ml": 30}, {"name": "Ром", "ml": 20}, {"name": "Кола", "ml": 150}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}, {"idx": 1, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Тоник Делюкс",     "ingredients": [{"name": "Водка", "ml": 20}, {"name": "Джин", "ml": 30}, {"name": "Тоник", "ml": 150}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}, {"idx": 1, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Фрукт. Пунш",     "ingredients": [{"name": "Водка", "ml": 20}, {"name": "Ром", "ml": 20}, {"name": "Апельсин. сок", "ml": 120}, {"name": "Кола", "ml": 40}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}, {"idx": 1, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Вечерний Бриз",    "ingredients": [{"name": "Джин", "ml": 40}, {"name": "Апельсин. сок", "ml": 80}, {"name": "Тоник", "ml": 80}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Кокос. Рассвет",   "ingredients": [{"name": "Ром", "ml": 40}, {"name": "Апельсин. сок", "ml": 100}, {"name": "Кола", "ml": 60}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Цитрус. Шторм",    "ingredients": [{"name": "Водка", "ml": 40}, {"name": "Джин", "ml": 10}, {"name": "Апельсин. сок", "ml": 100}, {"name": "Тоник", "ml": 50}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}, {"idx": 1, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
    {"name": "Классич. Микс",    "ingredients": [{"name": "Водка", "ml": 25}, {"name": "Ром", "ml": 25}, {"name": "Апельсин. сок", "ml": 50}, {"name": "Кола", "ml": 50}, {"name": "Тоник", "ml": 50}],
     "strength": [{"idx": 0, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}, {"idx": 1, "points": [0, 0.5, 0.75, 1.0, 1.25, 1.5], "def": 3}]},
]

data = {
    "pumps": ["Водка", "Ром", "Джин", "Апельсин. сок", "Кола", "Тоник"],
    "recipes": recipes,
}

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "recipes.json")
with open(out, "w", encoding="utf-8") as f:
    json.dump(data, f, ensure_ascii=False, indent=2)
print(f"Generated {out} ({len(recipes)} recipes, {os.path.getsize(out)} bytes)")

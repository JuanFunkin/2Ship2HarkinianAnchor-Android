#include "Anchor.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <vector>

extern "C" {
#include "z64actor.h"
#include "functions.h"
#include "variables.h"
#include "macros.h"
}

// ---------------------------------------------------------------------------------------------
// Recoloreo por jugador.
//
// Los colores de túnica / pelo / botas de Link están escritos DENTRO de las display lists del modelo
// (gDPSetPrimColor / gDPSetEnvColor). Cambiar esas instrucciones en su sitio afectaría a TODOS los
// Links a la vez (el renderizador las lee al final del frame), que es justo lo que hacía el Anchor
// viejo: todos se veían con tus colores. Por eso aquí se hace una COPIA de la display list por cada
// combinación de colores y el dibujo del jugador remoto usa su copia.
//
// Clasificación por tono (heurística, ajustable): verde = túnica, amarillo claro = pelo, marrón = botas.
// ---------------------------------------------------------------------------------------------

namespace {

enum class ColorClass { None, Tunic, Hair, Boots };

void RgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
    float mx = std::max(r, std::max(g, b));
    float mn = std::min(r, std::min(g, b));
    float d = mx - mn;
    v = mx;
    s = (mx <= 0.0f) ? 0.0f : d / mx;
    if (d <= 0.0f) {
        h = 0.0f;
    } else if (mx == r) {
        h = 60.0f * std::fmod((g - b) / d, 6.0f);
    } else if (mx == g) {
        h = 60.0f * (((b - r) / d) + 2.0f);
    } else {
        h = 60.0f * (((r - g) / d) + 4.0f);
    }
    if (h < 0.0f) {
        h += 360.0f;
    }
}

ColorClass Classify(uint8_t r, uint8_t g, uint8_t b) {
    float h, s, v;
    RgbToHsv(r / 255.0f, g / 255.0f, b / 255.0f, h, s, v);
    if (s < 0.25f || v < 0.12f) {
        return ColorClass::None;
    }
    if (h >= 70.0f && h <= 170.0f) {
        return ColorClass::Tunic;
    }
    if (h >= 38.0f && h < 70.0f && v > 0.75f) {
        return ColorClass::Hair;
    }
    if (h >= 10.0f && h < 38.0f && v < 0.65f && s > 0.40f) {
        return ColorClass::Boots;
    }
    return ColorClass::None;
}

float BaseValue(ColorClass c) {
    switch (c) {
        case ColorClass::Tunic:
            return 0.45f;
        case ColorClass::Hair:
            return 0.95f;
        default:
            return 0.45f;
    }
}

void Recolor(uint8_t& r, uint8_t& g, uint8_t& b, ColorClass cls, uint32_t target) {
    float origV = std::max(r, std::max(g, b)) / 255.0f;
    float k = std::min(1.5f, origV / BaseValue(cls));
    float tr = ((target >> 16) & 0xFF) * k;
    float tg = ((target >> 8) & 0xFF) * k;
    float tb = (target & 0xFF) * k;
    r = (uint8_t)std::min(255.0f, tr);
    g = (uint8_t)std::min(255.0f, tg);
    b = (uint8_t)std::min(255.0f, tb);
}

struct CacheEntry {
    std::vector<Gfx> clone;
    bool changed = false;
};

using CacheKey = std::tuple<uintptr_t, uint32_t, uint32_t, uint32_t>;
std::map<CacheKey, CacheEntry> sCache;

constexpr size_t kMaxInstructions = 4096;

} // namespace

void* Anchor_GetRecoloredDL(void* srcGfx, const AnchorLook& look) {
    Gfx* src = (Gfx*)srcGfx;
    if (src == nullptr) {
        return srcGfx;
    }

    CacheKey key{ (uintptr_t)src, look.tunic, look.hair, look.boots };
    auto it = sCache.find(key);
    if (it != sCache.end()) {
        return it->second.changed ? (void*)it->second.clone.data() : srcGfx;
    }

    CacheEntry entry;
    bool foundEnd = false;
    for (size_t i = 0; i < kMaxInstructions; i++) {
        Gfx g = src[i];
        uint8_t op = (uint8_t)((uint64_t)g.words.w0 >> 24);

        if (op == (uint8_t)G_SETPRIMCOLOR || op == (uint8_t)G_SETENVCOLOR) {
            uint32_t w1 = (uint32_t)g.words.w1;
            uint8_t r = (w1 >> 24) & 0xFF;
            uint8_t gr = (w1 >> 16) & 0xFF;
            uint8_t b = (w1 >> 8) & 0xFF;
            uint8_t a = w1 & 0xFF;
            ColorClass cls = Classify(r, gr, b);
            if (cls != ColorClass::None) {
                uint32_t target = (cls == ColorClass::Tunic) ? look.tunic
                                  : (cls == ColorClass::Hair) ? look.hair
                                                              : look.boots;
                Recolor(r, gr, b, cls, target);
                g.words.w1 = ((uint32_t)r << 24) | ((uint32_t)gr << 16) | ((uint32_t)b << 8) | a;
                entry.changed = true;
            }
        }

        entry.clone.push_back(g);
        if (op == (uint8_t)G_ENDDL) {
            foundEnd = true;
            break;
        }
    }

    if (!foundEnd) {
        entry.changed = false; // no se pudo copiar con seguridad: usar el original
    }

    auto& stored = sCache.emplace(key, std::move(entry)).first->second;
    return stored.changed ? (void*)stored.clone.data() : srcGfx;
}

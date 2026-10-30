package com.dishii.mm;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

/**
 * Modelo de datos de los controles táctiles: posición, tamaño, opacidad y colores
 * de cada botón, más los ajustes globales. Se guarda como JSON en SharedPreferences.
 */
public final class ControlsConfig {

    // Forma del elemento
    public static final int KIND_BUTTON = 0; // círculo
    public static final int KIND_PILL = 1;   // rectángulo redondeado
    public static final int KIND_STICK = 2;  // joystick

    // Qué hace el elemento
    public static final int ACT_BUTTON = 0;   // setButton(code)
    public static final int ACT_AXIS = 1;     // setAxis(code, axisValue)  (botones C)
    public static final int ACT_STICK = 2;    // joystick izquierdo
    public static final int ACT_SETTINGS = 3; // abre el menú de controles
    public static final int ACT_TOGGLE = 4;   // oculta / muestra los controles

    private static final String PREFS = "com.dishii.mm.touchcontrols";
    private static final String KEY = "layout_v1";

    public static final class Element {
        public final String id;
        public final String name;   // nombre mostrado en el menú
        public final String label;  // texto dibujado sobre el botón
        public final int kind;
        public final int action;
        public final int code;      // código de botón o de eje
        public final int axisValue; // valor del eje para ACT_AXIS
        public final float aspect;  // ancho / alto

        public float x, y;          // centro, como fracción (0..1) de la pantalla
        public float sizeDp;        // alto (o diámetro) en dp
        public boolean visible = true;
        public int opacity = 100;   // 10..100
        public int fill, pressed, stroke, text;

        Element(String id, String name, String label, int kind, int action, int code, int axisValue,
                float aspect) {
            this.id = id;
            this.name = name;
            this.label = label;
            this.kind = kind;
            this.action = action;
            this.code = code;
            this.axisValue = axisValue;
            this.aspect = aspect;
        }
    }

    // ---- Ajustes globales ----
    public int scalePct = 100;      // 50..200
    public int opacityPct = 100;    // 10..100
    public int strokeDp = 3;        // 1..8
    public boolean lookEnabled = true;
    public int lookSens = 15;       // 1..40
    public int lookMinPct = 35;     // la cámara solo responde a la derecha de este % de pantalla
    public boolean haptics = true;
    public boolean hidden = false;

    public List<Element> elements = defaults();

    // ---- Valores por defecto (opacos y con borde blanco para que se vean siempre) ----
    private static Element el(String id, String name, String label, int kind, int action, int code,
                              int axisValue, float x, float y, float sizeDp, float aspect,
                              int fill, int pressed, int text) {
        Element e = new Element(id, name, label, kind, action, code, axisValue, aspect);
        e.x = x;
        e.y = y;
        e.sizeDp = sizeDp;
        e.fill = fill;
        e.pressed = pressed;
        e.stroke = 0xFFFFFFFF;
        e.text = text;
        return e;
    }

    public static List<Element> defaults() {
        final int GRAY = 0xFF4A4F5C;
        final int AMBER = 0xFFFFB300;
        final int WHITE = 0xFFFFFFFF;
        final int MAX = Short.MAX_VALUE;
        final int MIN = Short.MIN_VALUE;

        List<Element> l = new ArrayList<>();
        l.add(el("stick", "Joystick", "", KIND_STICK, ACT_STICK, 0, 0,
                0.14f, 0.70f, 150, 1f, 0xCC1B1F2A, AMBER, 0xFFE0E0E0));

        l.add(el("a", "Botón A", "A", KIND_BUTTON, ACT_BUTTON, ControllerButtons.BUTTON_A, 0,
                0.92f, 0.70f, 64, 1f, 0xFF1E5BC6, AMBER, WHITE));
        l.add(el("b", "Botón B", "B", KIND_BUTTON, ACT_BUTTON, ControllerButtons.BUTTON_B, 0,
                0.84f, 0.85f, 56, 1f, 0xFF2E9E4F, AMBER, WHITE));
        l.add(el("x", "Botón X", "X", KIND_BUTTON, ACT_BUTTON, ControllerButtons.BUTTON_X, 0,
                0.84f, 0.55f, 50, 1f, GRAY, AMBER, WHITE));
        l.add(el("y", "Botón Y", "Y", KIND_BUTTON, ACT_BUTTON, ControllerButtons.BUTTON_Y, 0,
                0.76f, 0.70f, 50, 1f, GRAY, AMBER, WHITE));

        final int YEL = 0xFFE6B800;
        final int DARK = 0xFF1B1B1B;
        l.add(el("cup", "C arriba", "\u25B2", KIND_BUTTON, ACT_AXIS, ControllerButtons.AXIS_RY, MIN,
                0.60f, 0.52f, 44, 1f, YEL, AMBER, DARK));
        l.add(el("cdown", "C abajo", "\u25BC", KIND_BUTTON, ACT_AXIS, ControllerButtons.AXIS_RY, MAX,
                0.60f, 0.82f, 44, 1f, YEL, AMBER, DARK));
        l.add(el("cleft", "C izquierda", "\u25C0", KIND_BUTTON, ACT_AXIS, ControllerButtons.AXIS_RX, MIN,
                0.53f, 0.67f, 44, 1f, YEL, AMBER, DARK));
        l.add(el("cright", "C derecha", "\u25B6", KIND_BUTTON, ACT_AXIS, ControllerButtons.AXIS_RX, MAX,
                0.67f, 0.67f, 44, 1f, YEL, AMBER, DARK));

        l.add(el("l", "Gatillo L", "L", KIND_PILL, ACT_BUTTON, ControllerButtons.BUTTON_LB, 0,
                0.07f, 0.10f, 46, 2.2f, GRAY, AMBER, WHITE));
        l.add(el("z", "Gatillo Z", "Z", KIND_PILL, ACT_BUTTON, ControllerButtons.AXIS_RT, 0,
                0.19f, 0.10f, 46, 2.2f, GRAY, AMBER, WHITE));
        l.add(el("r", "Gatillo R", "R", KIND_PILL, ACT_BUTTON, ControllerButtons.BUTTON_RB, 0,
                0.93f, 0.10f, 46, 2.2f, GRAY, AMBER, WHITE));

        l.add(el("back", "Back / Select", "BACK", KIND_PILL, ACT_BUTTON, ControllerButtons.BUTTON_BACK, 0,
                0.41f, 0.10f, 36, 2.4f, GRAY, AMBER, WHITE));
        l.add(el("start", "Start", "START", KIND_PILL, ACT_BUTTON, ControllerButtons.BUTTON_START, 0,
                0.59f, 0.10f, 36, 2.4f, 0xFFD32F2F, AMBER, WHITE));

        l.add(el("menu", "Menú de controles", "\u2630", KIND_PILL, ACT_SETTINGS, 0, 0,
                0.50f, 0.10f, 36, 1.5f, 0xFF37474F, AMBER, WHITE));
        l.add(el("toggle", "Ocultar / mostrar", "\u25C9", KIND_BUTTON, ACT_TOGGLE, 0, 0,
                0.50f, 0.22f, 30, 1f, 0xFF37474F, AMBER, WHITE));
        return l;
    }

    public Element find(String id) {
        for (Element e : elements) {
            if (e.id.equals(id)) return e;
        }
        return null;
    }

    public void resetAll() {
        scalePct = 100;
        opacityPct = 100;
        strokeDp = 3;
        lookEnabled = true;
        lookSens = 15;
        lookMinPct = 35;
        haptics = true;
        hidden = false;
        elements = defaults();
    }

    public void resetElement(String id) {
        Element cur = find(id);
        if (cur == null) return;
        for (Element d : defaults()) {
            if (d.id.equals(id)) {
                cur.x = d.x;
                cur.y = d.y;
                cur.sizeDp = d.sizeDp;
                cur.visible = true;
                cur.opacity = 100;
                cur.fill = d.fill;
                cur.pressed = d.pressed;
                cur.stroke = d.stroke;
                cur.text = d.text;
            }
        }
    }

    // ---- Persistencia ----
    public static ControlsConfig load(Context ctx) {
        ControlsConfig c = new ControlsConfig();
        try {
            SharedPreferences sp = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
            String json = sp.getString(KEY, null);
            if (json == null) return c;
            JSONObject o = new JSONObject(json);
            c.scalePct = clamp(o.optInt("scale", c.scalePct), 50, 200);
            c.opacityPct = clamp(o.optInt("opacity", c.opacityPct), 10, 100);
            c.strokeDp = clamp(o.optInt("stroke", c.strokeDp), 1, 8);
            c.lookEnabled = o.optBoolean("look", c.lookEnabled);
            c.lookSens = clamp(o.optInt("lookSens", c.lookSens), 1, 40);
            c.lookMinPct = clamp(o.optInt("lookMin", c.lookMinPct), 0, 80);
            c.haptics = o.optBoolean("haptics", c.haptics);
            c.hidden = o.optBoolean("hidden", c.hidden);

            JSONObject els = o.optJSONObject("el");
            if (els != null) {
                for (Element e : c.elements) {
                    JSONObject j = els.optJSONObject(e.id);
                    if (j == null) continue;
                    e.x = (float) j.optDouble("x", e.x);
                    e.y = (float) j.optDouble("y", e.y);
                    e.sizeDp = (float) j.optDouble("s", e.sizeDp);
                    e.visible = j.optBoolean("vis", e.visible);
                    e.opacity = clamp(j.optInt("op", e.opacity), 10, 100);
                    e.fill = j.optInt("fill", e.fill);
                    e.pressed = j.optInt("prs", e.pressed);
                    e.stroke = j.optInt("str", e.stroke);
                    e.text = j.optInt("txt", e.text);
                }
            }
        } catch (Exception ex) {
            // Si el JSON está corrupto se usan los valores por defecto.
            return new ControlsConfig();
        }
        return c;
    }

    public void save(Context ctx) {
        try {
            JSONObject o = new JSONObject();
            o.put("scale", scalePct);
            o.put("opacity", opacityPct);
            o.put("stroke", strokeDp);
            o.put("look", lookEnabled);
            o.put("lookSens", lookSens);
            o.put("lookMin", lookMinPct);
            o.put("haptics", haptics);
            o.put("hidden", hidden);
            JSONObject els = new JSONObject();
            for (Element e : elements) {
                JSONObject j = new JSONObject();
                j.put("x", e.x);
                j.put("y", e.y);
                j.put("s", e.sizeDp);
                j.put("vis", e.visible);
                j.put("op", e.opacity);
                j.put("fill", e.fill);
                j.put("prs", e.pressed);
                j.put("str", e.stroke);
                j.put("txt", e.text);
                els.put(e.id, j);
            }
            o.put("el", els);
            ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                    .edit().putString(KEY, o.toString()).apply();
        } catch (Exception ignored) {
        }
    }

    private static int clamp(int v, int lo, int hi) {
        return Math.max(lo, Math.min(hi, v));
    }
}

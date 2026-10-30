package com.dishii.mm;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

import java.util.List;

/**
 * Menú de configuración de los controles táctiles. Todo se construye por código
 * (sin XML) y los cambios se ven en vivo detrás del diálogo.
 */
public final class ControlsSettings {

    interface IntCb {
        void on(int value);
    }

    interface BoolCb {
        void on(boolean value);
    }

    private static final int[] PRESETS = {
            0xFFFFFFFF, 0xFF000000, 0xFFE53935, 0xFFFB8C00, 0xFFFDD835, 0xFF43A047,
            0xFF00ACC1, 0xFF1E88E5, 0xFF5E35B1, 0xFFD81B60, 0xFF607D8B, 0xFF1B1F2A
    };

    private final Activity activity;
    private final ControlsConfig cfg;
    private final TouchControlsView view;
    private final Runnable onEditRequested;
    private final float density;

    public ControlsSettings(Activity activity, ControlsConfig cfg, TouchControlsView view,
                            Runnable onEditRequested) {
        this.activity = activity;
        this.cfg = cfg;
        this.view = view;
        this.onEditRequested = onEditRequested;
        this.density = activity.getResources().getDisplayMetrics().density;
    }

    // ------------------------------------------------------------------ menú principal

    public void showMain() {
        LinearLayout root = column();

        heading(root, "Controles t\u00E1ctiles");
        addSeek(root, "Tama\u00F1o global (%)", 50, 200, cfg.scalePct, new IntCb() {
            public void on(int v) {
                cfg.scalePct = v;
                view.invalidate();
            }
        });
        addSeek(root, "Opacidad global (%)", 10, 100, cfg.opacityPct, new IntCb() {
            public void on(int v) {
                cfg.opacityPct = v;
                view.invalidate();
            }
        });
        addSeek(root, "Grosor del borde (dp)", 1, 8, cfg.strokeDp, new IntCb() {
            public void on(int v) {
                cfg.strokeDp = v;
                view.invalidate();
            }
        });

        heading(root, "C\u00E1mara (arrastrar en pantalla)");
        addCheck(root, "Activar c\u00E1mara t\u00E1ctil", cfg.lookEnabled, new BoolCb() {
            public void on(boolean v) {
                cfg.lookEnabled = v;
            }
        });
        addSeek(root, "Sensibilidad", 1, 40, cfg.lookSens, new IntCb() {
            public void on(int v) {
                cfg.lookSens = v;
            }
        });
        addSeek(root, "Zona: empieza en el % de pantalla", 0, 80, cfg.lookMinPct, new IntCb() {
            public void on(int v) {
                cfg.lookMinPct = v;
            }
        });

        heading(root, "General");
        addCheck(root, "Vibraci\u00F3n al tocar", cfg.haptics, new BoolCb() {
            public void on(boolean v) {
                cfg.haptics = v;
            }
        });
        addCheck(root, "Ocultar controles", cfg.hidden, new BoolCb() {
            public void on(boolean v) {
                cfg.hidden = v;
                view.invalidate();
            }
        });

        final AlertDialog[] holder = new AlertDialog[1];

        Button edit = button("Editar posiciones (arrastrar)");
        edit.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                holder[0].dismiss();
                onEditRequested.run();
            }
        });
        root.addView(edit);

        Button pick = button("Personalizar un bot\u00F3n\u2026");
        pick.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                holder[0].dismiss();
                showElementList();
            }
        });
        root.addView(pick);

        Button reset = button("Restaurar todo a los valores por defecto");
        reset.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                cfg.resetAll();
                cfg.save(activity);
                view.invalidate();
                holder[0].dismiss();
                showMain();
            }
        });
        root.addView(reset);

        holder[0] = buildDialog(root, "Cerrar");
    }

    // ------------------------------------------------------------------ lista de botones

    private void showElementList() {
        List<ControlsConfig.Element> list = cfg.elements;
        String[] names = new String[list.size()];
        for (int i = 0; i < names.length; i++) {
            names[i] = list.get(i).name + (list.get(i).visible ? "" : "  (oculto)");
        }
        final List<ControlsConfig.Element> fl = list;
        AlertDialog d = new AlertDialog.Builder(activity,
                android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Elige un bot\u00F3n")
                .setItems(names, new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface dialog, int which) {
                        showElement(fl.get(which));
                    }
                })
                .setNegativeButton("Atr\u00E1s", new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface dialog, int which) {
                        showMain();
                    }
                })
                .create();
        d.show();
    }

    // ------------------------------------------------------------------ un bot\u00F3n

    public void showElement(final ControlsConfig.Element el) {
        LinearLayout root = column();
        heading(root, el.name);

        addCheck(root, "Visible", el.visible, new BoolCb() {
            public void on(boolean v) {
                el.visible = v;
                view.invalidate();
            }
        });
        addSeek(root, "Tama\u00F1o (dp)", 20, 260, Math.round(el.sizeDp), new IntCb() {
            public void on(int v) {
                el.sizeDp = v;
                view.invalidate();
            }
        });
        addSeek(root, "Opacidad (%)", 10, 100, el.opacity, new IntCb() {
            public void on(int v) {
                el.opacity = v;
                view.invalidate();
            }
        });
        addSeek(root, "Posici\u00F3n X (%)", 1, 99, Math.round(el.x * 100), new IntCb() {
            public void on(int v) {
                el.x = v / 100f;
                view.invalidate();
            }
        });
        addSeek(root, "Posici\u00F3n Y (%)", 1, 99, Math.round(el.y * 100), new IntCb() {
            public void on(int v) {
                el.y = v / 100f;
                view.invalidate();
            }
        });

        heading(root, "Colores (toca el recuadro)");
        boolean stick = el.kind == ControlsConfig.KIND_STICK;
        addColor(root, stick ? "Base" : "Fondo", el.fill, new IntCb() {
            public void on(int v) {
                el.fill = v;
                view.invalidate();
            }
        });
        addColor(root, stick ? "Perilla activa" : "Al presionar", el.pressed, new IntCb() {
            public void on(int v) {
                el.pressed = v;
                view.invalidate();
            }
        });
        addColor(root, "Borde", el.stroke, new IntCb() {
            public void on(int v) {
                el.stroke = v;
                view.invalidate();
            }
        });
        addColor(root, stick ? "Perilla en reposo" : "Texto", el.text, new IntCb() {
            public void on(int v) {
                el.text = v;
                view.invalidate();
            }
        });

        final AlertDialog[] holder = new AlertDialog[1];
        Button reset = button("Restaurar este bot\u00F3n");
        reset.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                cfg.resetElement(el.id);
                cfg.save(activity);
                view.invalidate();
                holder[0].dismiss();
                showElement(el);
            }
        });
        root.addView(reset);

        holder[0] = buildDialog(root, "Listo");
    }

    // ------------------------------------------------------------------ selector de color

    private void pickColor(int initial, final IntCb cb) {
        final int[] argb = {initial};
        final SeekBar[] bars = new SeekBar[4];
        final TextView[] labels = new TextView[4];
        final String[] names = {"Alfa", "Rojo", "Verde", "Azul"};

        LinearLayout root = column();

        final View preview = new View(activity);
        preview.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(48)));
        preview.setBackgroundColor(initial);
        root.addView(preview);

        HorizontalScrollView hs = new HorizontalScrollView(activity);
        LinearLayout row = new LinearLayout(activity);
        row.setOrientation(LinearLayout.HORIZONTAL);
        hs.addView(row);
        root.addView(hs);

        for (int i = 0; i < 4; i++) {
            final int channel = i;
            labels[i] = text(names[i] + ": " + channelOf(initial, i));
            root.addView(labels[i]);
            bars[i] = new SeekBar(activity);
            bars[i].setMax(255);
            bars[i].setProgress(channelOf(initial, i));
            bars[i].setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                public void onProgressChanged(SeekBar s, int p, boolean fromUser) {
                    labels[channel].setText(names[channel] + ": " + p);
                    if (!fromUser) return;
                    argb[0] = Color.argb(bars[0].getProgress(), bars[1].getProgress(),
                            bars[2].getProgress(), bars[3].getProgress());
                    preview.setBackgroundColor(argb[0]);
                }

                public void onStartTrackingTouch(SeekBar s) {
                }

                public void onStopTrackingTouch(SeekBar s) {
                }
            });
            root.addView(bars[i]);
        }

        for (int i = 0; i < PRESETS.length; i++) {
            final int c = PRESETS[i];
            View sw = new View(activity);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(40), dp(40));
            lp.setMargins(dp(3), dp(6), dp(3), dp(6));
            sw.setLayoutParams(lp);
            sw.setBackground(swatchDrawable(c));
            sw.setOnClickListener(new View.OnClickListener() {
                public void onClick(View v) {
                    argb[0] = c;
                    preview.setBackgroundColor(c);
                    for (int k = 0; k < 4; k++) {
                        bars[k].setProgress(channelOf(c, k));
                        labels[k].setText(names[k] + ": " + channelOf(c, k));
                    }
                }
            });
            row.addView(sw);
        }

        ScrollView sv = new ScrollView(activity);
        sv.addView(root);
        new AlertDialog.Builder(activity, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Elegir color")
                .setView(sv)
                .setPositiveButton("Aplicar", new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface d, int w) {
                        cb.on(argb[0]);
                    }
                })
                .setNegativeButton("Cancelar", null)
                .show();
    }

    private static int channelOf(int argb, int channel) {
        switch (channel) {
            case 0:
                return Color.alpha(argb);
            case 1:
                return Color.red(argb);
            case 2:
                return Color.green(argb);
            default:
                return Color.blue(argb);
        }
    }

    // ------------------------------------------------------------------ helpers de UI

    private AlertDialog buildDialog(LinearLayout content, String closeText) {
        ScrollView sv = new ScrollView(activity);
        sv.addView(content);
        AlertDialog d = new AlertDialog.Builder(activity,
                android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setView(sv)
                .setPositiveButton(closeText, null)
                .create();
        d.setOnDismissListener(new DialogInterface.OnDismissListener() {
            public void onDismiss(DialogInterface dialog) {
                cfg.save(activity);
                view.invalidate();
            }
        });
        d.show();

        // El di\u00E1logo ocupa la mitad derecha y no oscurece el fondo: los cambios se ven en vivo.
        Window w = d.getWindow();
        if (w != null) {
            WindowManager.LayoutParams lp = w.getAttributes();
            lp.gravity = Gravity.END | Gravity.CENTER_VERTICAL;
            lp.width = (int) (activity.getResources().getDisplayMetrics().widthPixels * 0.5f);
            lp.dimAmount = 0f;
            w.setAttributes(lp);
            w.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
        }
        return d;
    }

    private LinearLayout column() {
        LinearLayout l = new LinearLayout(activity);
        l.setOrientation(LinearLayout.VERTICAL);
        l.setPadding(dp(16), dp(8), dp(16), dp(8));
        return l;
    }

    private int dp(int v) {
        return Math.round(v * density);
    }

    private TextView text(String s) {
        TextView t = new TextView(activity);
        t.setText(s);
        t.setTextColor(Color.WHITE);
        t.setTextSize(14);
        t.setPadding(0, dp(6), 0, 0);
        return t;
    }

    private void heading(LinearLayout parent, String s) {
        TextView t = text(s);
        t.setTextSize(17);
        t.setTextColor(0xFFFFB300);
        t.setPadding(0, dp(14), 0, dp(2));
        parent.addView(t);
    }

    private Button button(String s) {
        Button b = new Button(activity);
        b.setText(s);
        b.setAllCaps(false);
        return b;
    }

    private void addSeek(LinearLayout parent, final String label, final int min, int max, int value,
                         final IntCb cb) {
        final TextView tv = text(label + ": " + value);
        parent.addView(tv);
        SeekBar sb = new SeekBar(activity);
        sb.setMax(max - min);
        sb.setProgress(Math.max(0, Math.min(max - min, value - min)));
        sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar s, int p, boolean fromUser) {
                int v = p + min;
                tv.setText(label + ": " + v);
                if (fromUser) cb.on(v);
            }

            public void onStartTrackingTouch(SeekBar s) {
            }

            public void onStopTrackingTouch(SeekBar s) {
            }
        });
        parent.addView(sb);
    }

    private void addCheck(LinearLayout parent, String label, boolean value, final BoolCb cb) {
        CheckBox c = new CheckBox(activity);
        c.setText(label);
        c.setTextColor(Color.WHITE);
        c.setChecked(value);
        c.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            public void onCheckedChanged(CompoundButton b, boolean checked) {
                cb.on(checked);
            }
        });
        parent.addView(c);
    }

    private GradientDrawable swatchDrawable(int color) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(color);
        g.setStroke(dp(2), 0xFFFFFFFF);
        g.setCornerRadius(dp(6));
        return g;
    }

    private void addColor(LinearLayout parent, String label, final int color, final IntCb cb) {
        LinearLayout row = new LinearLayout(activity);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(4), 0, dp(4));

        TextView tv = text(label);
        LinearLayout.LayoutParams tlp = new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        tv.setLayoutParams(tlp);
        row.addView(tv);

        final View sw = new View(activity);
        sw.setLayoutParams(new LinearLayout.LayoutParams(dp(64), dp(34)));
        sw.setBackground(swatchDrawable(color));
        final int[] current = {color};
        sw.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                pickColor(current[0], new IntCb() {
                    public void on(int picked) {
                        current[0] = picked;
                        sw.setBackground(swatchDrawable(picked));
                        cb.on(picked);
                    }
                });
            }
        });
        row.addView(sw);
        parent.addView(row);
    }
}

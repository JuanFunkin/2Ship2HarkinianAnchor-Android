package com.dishii.mm;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.util.SparseIntArray;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

import java.util.List;

/**
 * Dibuja los controles táctiles y envía su estado al juego mediante {@link Listener}.
 * - Los toques que no caen en ningún botón se devuelven al sistema (así el menú ImGui sigue funcionando).
 * - En modo edición los botones se arrastran y, al tocarlos, se abre su configuración.
 */
public class TouchControlsView extends View {

    public interface Listener {
        void onButton(int code, boolean down);

        void onAxis(int axis, short value);

        void onCamera(int axis, float value);

        void onOpenSettings();

        void onElementTapped(ControlsConfig.Element element); // solo en modo edición

        void onEditDone();
    }

    private final ControlsConfig cfg;
    private final Listener listener;
    private final float density;

    private final Paint fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint strokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint dashPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();
    private final RectF doneRect = new RectF();

    private boolean editMode = false;
    private volatile boolean lookAllowed = true;

    // Estado de juego
    private final SparseIntArray pointerElement = new SparseIntArray(); // pointerId -> índice de elemento
    private final boolean[] pressedState;
    private final float[] stickDx;
    private final float[] stickDy;
    private int lookPointer = -1;
    private float lookLastX, lookLastY;

    // Estado de edición
    private int editPointer = -1;
    private int editIndex = -1;
    private int selectedIndex = -1;
    private boolean editDonePressed = false;
    private boolean editMoved = false;
    private float editDownX, editDownY, editStartX, editStartY;

    public TouchControlsView(Context context, ControlsConfig config, Listener listener) {
        super(context);
        this.cfg = config;
        this.listener = listener;
        this.density = context.getResources().getDisplayMetrics().density;
        int n = config.elements.size();
        pressedState = new boolean[n];
        stickDx = new float[n];
        stickDy = new float[n];

        fillPaint.setStyle(Paint.Style.FILL);
        strokePaint.setStyle(Paint.Style.STROKE);
        textPaint.setTextAlign(Paint.Align.CENTER);
        textPaint.setTypeface(Typeface.DEFAULT_BOLD);
        dashPaint.setStyle(Paint.Style.STROKE);
        dashPaint.setStrokeWidth(2 * density);
        dashPaint.setPathEffect(new DashPathEffect(new float[]{10 * density, 6 * density}, 0));
        setClickable(false);
        setHapticFeedbackEnabled(true);
    }

    // ------------------------------------------------------------------ API pública

    public void setEditMode(boolean on) {
        releaseAll();
        editMode = on;
        editPointer = -1;
        editIndex = -1;
        selectedIndex = -1;
        postInvalidate();
    }

    public boolean isEditMode() {
        return editMode;
    }

    /** Lo usan Enable/DisableTouchArea de MainActivity (pueden llamarse desde otro hilo). */
    public void setLookAllowed(boolean allowed) {
        lookAllowed = allowed;
        if (!allowed) {
            post(new Runnable() {
                @Override
                public void run() {
                    stopLook();
                }
            });
        }
    }

    public void releaseAll() {
        for (int i = 0; i < pressedState.length; i++) {
            if (pressedState[i]) releaseIndex(i);
        }
        pointerElement.clear();
        stopLook();
        invalidate();
    }

    // ------------------------------------------------------------------ geometría

    private float heightPx(ControlsConfig.Element e) {
        return e.sizeDp * density * cfg.scalePct / 100f;
    }

    private float widthPx(ControlsConfig.Element e) {
        return heightPx(e) * e.aspect;
    }

    private float cx(ControlsConfig.Element e) {
        return e.x * getWidth();
    }

    private float cy(ControlsConfig.Element e) {
        return e.y * getHeight();
    }

    private boolean hit(ControlsConfig.Element e, float px, float py) {
        float slop = 8 * density;
        float ex = cx(e);
        float ey = cy(e);
        if (e.kind == ControlsConfig.KIND_PILL) {
            return Math.abs(px - ex) <= widthPx(e) / 2f + slop
                    && Math.abs(py - ey) <= heightPx(e) / 2f + slop;
        }
        float dx = px - ex;
        float dy = py - ey;
        float r = heightPx(e) / 2f + slop;
        return dx * dx + dy * dy <= r * r;
    }

    private void updateDoneRect() {
        float w = 150 * density;
        float h = 48 * density;
        doneRect.set(getWidth() / 2f - w / 2f, getHeight() / 2f, getWidth() / 2f + w / 2f,
                getHeight() / 2f + h);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        updateDoneRect();
    }

    // ------------------------------------------------------------------ dibujo

    private static int withAlpha(int color, int alpha255) {
        int a = (Color.alpha(color) * alpha255) / 255;
        return (color & 0x00FFFFFF) | (a << 24);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        List<ControlsConfig.Element> list = cfg.elements;
        if (editMode) {
            canvas.drawColor(0x66000000);
        }
        for (int i = 0; i < list.size(); i++) {
            ControlsConfig.Element e = list.get(i);
            if (!editMode) {
                if (!e.visible) continue;
                if (cfg.hidden && e.action != ControlsConfig.ACT_TOGGLE
                        && e.action != ControlsConfig.ACT_SETTINGS) continue;
            }
            drawElement(canvas, e, i, !e.visible);
        }
        if (editMode) drawEditUi(canvas);
    }

    private void drawElement(Canvas canvas, ControlsConfig.Element e, int index, boolean ghost) {
        int alpha = Math.round(255f * e.opacity / 100f * cfg.opacityPct / 100f);
        if (ghost) alpha = 70;
        boolean down = pressedState[index];
        float ex = cx(e);
        float ey = cy(e);
        float h = heightPx(e);
        float w = widthPx(e);
        float sw = cfg.strokeDp * density;

        strokePaint.setStrokeWidth(sw);

        if (e.kind == ControlsConfig.KIND_STICK) {
            float r = h / 2f;
            fillPaint.setColor(withAlpha(e.fill, alpha));
            canvas.drawCircle(ex, ey, r, fillPaint);
            strokePaint.setColor(withAlpha(e.stroke, alpha));
            canvas.drawCircle(ex, ey, r - sw / 2f, strokePaint);

            float knobR = h * 0.30f;
            float kx = ex + stickDx[index];
            float ky = ey + stickDy[index];
            fillPaint.setColor(withAlpha(down ? e.pressed : e.text, alpha));
            canvas.drawCircle(kx, ky, knobR, fillPaint);
            strokePaint.setColor(withAlpha(e.stroke, alpha));
            canvas.drawCircle(kx, ky, knobR - sw / 2f, strokePaint);
        } else {
            boolean circle = e.kind == ControlsConfig.KIND_BUTTON;
            fillPaint.setColor(withAlpha(down ? e.pressed : e.fill, alpha));
            strokePaint.setColor(withAlpha(e.stroke, alpha));
            if (circle) {
                float r = h / 2f;
                canvas.drawCircle(ex, ey, r, fillPaint);
                canvas.drawCircle(ex, ey, r - sw / 2f, strokePaint);
            } else {
                rect.set(ex - w / 2f, ey - h / 2f, ex + w / 2f, ey + h / 2f);
                canvas.drawRoundRect(rect, h / 2f, h / 2f, fillPaint);
                rect.inset(sw / 2f, sw / 2f);
                canvas.drawRoundRect(rect, h / 2f, h / 2f, strokePaint);
            }
            if (e.label != null && e.label.length() > 0) {
                textPaint.setColor(withAlpha(e.text, alpha));
                textPaint.setTextSize(h * (circle ? 0.42f : 0.40f));
                Paint.FontMetrics fm = textPaint.getFontMetrics();
                canvas.drawText(e.label, ex, ey - (fm.ascent + fm.descent) / 2f, textPaint);
            }
        }

        if (editMode) {
            boolean selected = index == selectedIndex;
            dashPaint.setColor(selected ? 0xFFFFEB3B : 0xCCFFFFFF);
            float pad = 6 * density;
            if (e.kind == ControlsConfig.KIND_PILL) {
                rect.set(ex - w / 2f - pad, ey - h / 2f - pad, ex + w / 2f + pad, ey + h / 2f + pad);
                canvas.drawRoundRect(rect, h / 2f + pad, h / 2f + pad, dashPaint);
            } else {
                canvas.drawCircle(ex, ey, h / 2f + pad, dashPaint);
            }
        }
    }

    private void drawEditUi(Canvas canvas) {
        updateDoneRect();
        textPaint.setColor(0xFFFFFFFF);
        textPaint.setTextSize(16 * density);
        canvas.drawText("Arrastra para mover \u00B7 Toca un bot\u00F3n para editarlo",
                getWidth() / 2f, doneRect.top - 16 * density, textPaint);
        fillPaint.setColor(0xFF2E7D32);
        canvas.drawRoundRect(doneRect, 10 * density, 10 * density, fillPaint);
        strokePaint.setStrokeWidth(2 * density);
        strokePaint.setColor(0xFFFFFFFF);
        canvas.drawRoundRect(doneRect, 10 * density, 10 * density, strokePaint);
        textPaint.setTextSize(18 * density);
        Paint.FontMetrics fm = textPaint.getFontMetrics();
        canvas.drawText("LISTO", doneRect.centerX(),
                doneRect.centerY() - (fm.ascent + fm.descent) / 2f, textPaint);
    }

    // ------------------------------------------------------------------ toques

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        final int action = e.getActionMasked();
        final int index = e.getActionIndex();
        switch (action) {
            case MotionEvent.ACTION_DOWN: {
                return editMode ? onEditDown(e, index) : onPlayDown(e, index);
            }
            case MotionEvent.ACTION_POINTER_DOWN: {
                if (editMode) onEditDown(e, index);
                else onPlayDown(e, index);
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                for (int i = 0; i < e.getPointerCount(); i++) {
                    if (editMode) onEditMove(e, i);
                    else onPlayMove(e, i);
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                if (editMode) onEditUp(e, index);
                else onPlayUp(e.getPointerId(index));
                return true;
            }
            case MotionEvent.ACTION_CANCEL: {
                if (editMode) {
                    editPointer = -1;
                    editIndex = -1;
                    editDonePressed = false;
                } else {
                    releaseAll();
                }
                return true;
            }
            default:
                return false;
        }
    }

    // ---- juego ----

    private boolean onPlayDown(MotionEvent e, int index) {
        int pid = e.getPointerId(index);
        float x = e.getX(index);
        float y = e.getY(index);
        List<ControlsConfig.Element> list = cfg.elements;
        for (int i = list.size() - 1; i >= 0; i--) {
            ControlsConfig.Element el = list.get(i);
            if (!el.visible) continue;
            if (cfg.hidden && el.action != ControlsConfig.ACT_TOGGLE
                    && el.action != ControlsConfig.ACT_SETTINGS) continue;
            if (!hit(el, x, y)) continue;
            pointerElement.put(pid, i);
            pressElement(el, i, pid, x, y);
            return true;
        }
        if (cfg.lookEnabled && lookAllowed && !cfg.hidden && lookPointer == -1
                && x > getWidth() * cfg.lookMinPct / 100f) {
            lookPointer = pid;
            lookLastX = x;
            lookLastY = y;
            return true;
        }
        return false; // el toque pasa al juego / menú ImGui
    }

    private void haptic() {
        if (cfg.haptics) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
    }

    private void pressElement(ControlsConfig.Element el, int i, int pid, float x, float y) {
        switch (el.action) {
            case ControlsConfig.ACT_BUTTON:
                pressedState[i] = true;
                haptic();
                listener.onButton(el.code, true);
                break;
            case ControlsConfig.ACT_AXIS:
                pressedState[i] = true;
                haptic();
                listener.onAxis(el.code, (short) el.axisValue);
                break;
            case ControlsConfig.ACT_STICK:
                pressedState[i] = true;
                updateStick(el, i, x, y);
                break;
            case ControlsConfig.ACT_TOGGLE:
                releaseAll();
                cfg.hidden = !cfg.hidden;
                cfg.save(getContext());
                haptic();
                invalidate();
                break;
            case ControlsConfig.ACT_SETTINGS:
                releaseAll();
                haptic();
                listener.onOpenSettings();
                break;
            default:
                break;
        }
    }

    private void updateStick(ControlsConfig.Element el, int i, float x, float y) {
        float h = heightPx(el);
        float maxR = h / 2f - h * 0.30f; // radio de la base menos radio de la perilla
        if (maxR < 1f) return;
        float dx = x - cx(el);
        float dy = y - cy(el);
        float d = (float) Math.sqrt(dx * dx + dy * dy);
        if (d > maxR) {
            float s = maxR / d;
            dx *= s;
            dy *= s;
        }
        stickDx[i] = dx;
        stickDy[i] = dy;
        short sx = (short) Math.round(dx / maxR * Short.MAX_VALUE);
        short sy = (short) Math.round(dy / maxR * Short.MAX_VALUE);
        listener.onAxis(ControllerButtons.AXIS_LX, sx);
        listener.onAxis(ControllerButtons.AXIS_LY, sy);
        invalidate();
    }

    private void onPlayMove(MotionEvent e, int pointerIndex) {
        int pid = e.getPointerId(pointerIndex);
        int idx = pointerElement.get(pid, -1);
        if (idx >= 0) {
            ControlsConfig.Element el = cfg.elements.get(idx);
            if (el.action == ControlsConfig.ACT_STICK) {
                updateStick(el, idx, e.getX(pointerIndex), e.getY(pointerIndex));
            }
        } else if (pid == lookPointer) {
            float x = e.getX(pointerIndex);
            float y = e.getY(pointerIndex);
            float rx = (x - lookLastX) * cfg.lookSens;
            float ry = (y - lookLastY) * cfg.lookSens;
            lookLastX = x;
            lookLastY = y;
            listener.onCamera(0, rx);
            listener.onCamera(1, ry);
        }
    }

    private void onPlayUp(int pid) {
        int idx = pointerElement.get(pid, -1);
        if (idx >= 0) {
            releaseIndex(idx);
            pointerElement.delete(pid);
        } else if (pid == lookPointer) {
            stopLook();
        }
    }

    private void stopLook() {
        if (lookPointer != -1) {
            lookPointer = -1;
            listener.onCamera(0, 0f);
            listener.onCamera(1, 0f);
        }
    }

    private void releaseIndex(int i) {
        ControlsConfig.Element el = cfg.elements.get(i);
        switch (el.action) {
            case ControlsConfig.ACT_BUTTON:
                listener.onButton(el.code, false);
                break;
            case ControlsConfig.ACT_AXIS:
                listener.onAxis(el.code, (short) 0);
                break;
            case ControlsConfig.ACT_STICK:
                listener.onAxis(ControllerButtons.AXIS_LX, (short) 0);
                listener.onAxis(ControllerButtons.AXIS_LY, (short) 0);
                stickDx[i] = 0f;
                stickDy[i] = 0f;
                break;
            default:
                break;
        }
        pressedState[i] = false;
        invalidate();
    }

    // ---- edición ----

    private boolean onEditDown(MotionEvent e, int index) {
        if (editPointer != -1) return true;
        int pid = e.getPointerId(index);
        float x = e.getX(index);
        float y = e.getY(index);
        updateDoneRect();
        if (doneRect.contains(x, y)) {
            editPointer = pid;
            editDonePressed = true;
            return true;
        }
        List<ControlsConfig.Element> list = cfg.elements;
        for (int i = list.size() - 1; i >= 0; i--) {
            ControlsConfig.Element el = list.get(i);
            if (hit(el, x, y)) {
                editPointer = pid;
                editIndex = i;
                selectedIndex = i;
                editDownX = x;
                editDownY = y;
                editStartX = el.x;
                editStartY = el.y;
                editMoved = false;
                invalidate();
                return true;
            }
        }
        return true;
    }

    private void onEditMove(MotionEvent e, int pointerIndex) {
        if (e.getPointerId(pointerIndex) != editPointer || editIndex < 0) return;
        float x = e.getX(pointerIndex);
        float y = e.getY(pointerIndex);
        float dx = x - editDownX;
        float dy = y - editDownY;
        if (!editMoved && Math.sqrt(dx * dx + dy * dy) > 8 * density) editMoved = true;
        if (editMoved && getWidth() > 0 && getHeight() > 0) {
            ControlsConfig.Element el = cfg.elements.get(editIndex);
            el.x = Math.max(0.01f, Math.min(0.99f, editStartX + dx / getWidth()));
            el.y = Math.max(0.01f, Math.min(0.99f, editStartY + dy / getHeight()));
            invalidate();
        }
    }

    private void onEditUp(MotionEvent e, int pointerIndex) {
        if (e.getPointerId(pointerIndex) != editPointer) return;
        float x = e.getX(pointerIndex);
        float y = e.getY(pointerIndex);
        if (editDonePressed) {
            editDonePressed = false;
            editPointer = -1;
            if (doneRect.contains(x, y)) listener.onEditDone();
            return;
        }
        if (editIndex >= 0) {
            ControlsConfig.Element el = cfg.elements.get(editIndex);
            if (editMoved) {
                cfg.save(getContext());
            } else {
                listener.onElementTapped(el);
            }
        }
        editPointer = -1;
        editIndex = -1;
        invalidate();
    }
}

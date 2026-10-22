package com.zaiyii.wolves;

import java.util.Random;

/**
 * 引擎关键帧求值、各动画时间轴与每帧参数。逐项移植自 native/main.cpp（那里的数值和公式来自游戏数据与反编译结果）。
 */
final class Curves {
    private Curves() {}

    static final int STEPPED = 1, KNOT = 2, SMOOTH = 3, FLAT = 4;
    static final int SCENE_TITLE = 0, SCENE_MENU = 1;
    static final int TONE_ORIGINAL = 0, TONE_BRIGHT = 1, TONE_LIGHT = 2;

    static final class Key {
        final float t;
        final int mode;
        final float[] v;

        Key(float t, int mode, float a, float b, float c) {
            this.t = t;
            this.mode = mode;
            this.v = new float[]{a, b, c};
        }
    }

    // ui_menu_background_color.chore（长度 20 秒）：各图层的 Render Diffuse Color
    static final Key[] COL_FAR = {
            new Key(0, FLAT, 0.2f, 0.051f, 0.098f), new Key(5, FLAT, 0.2f, 0.078f, 0.057f),
            new Key(15, FLAT, 0.2f, 0.051f, 0.188f), new Key(20, FLAT, 0.2f, 0.051f, 0.098f)};
    static final Key[] COL_NEAR = {
            new Key(0, FLAT, 0.2f, 0.051f, 0.2f), new Key(5, FLAT, 0.2f, 0.051f, 0.125f),
            new Key(15, FLAT, 0.11f, 0.051f, 0.2f), new Key(20, FLAT, 0.2f, 0.051f, 0.2f)};
    static final Key[] COL_FILL = {
            new Key(0, SMOOTH, 0.102f, 0.024f, 0.051f), new Key(5, SMOOTH, 0.102f, 0.035f, 0.024f),
            new Key(15, SMOOTH, 0.102f, 0.024f, 0.098f), new Key(20, SMOOTH, 0.102f, 0.024f, 0.051f)};
    static final Key[] COL_GRAD = {
            new Key(0, SMOOTH, 0.024f, 0.0f, 0.149f), new Key(5, SMOOTH, 0.098f, 0.0f, 0.149f),
            new Key(15, SMOOTH, 0.0f, 0.067f, 0.149f), new Key(19.980469f, SMOOTH, 0.024f, 0.0f, 0.149f)};

    // 旋转 300 秒转 360°；近景 / 远景 UV 位移 0→2，分别 90 / 120 秒；颜色动画 20 秒
    static final float ROT_LEN = 300, NEAR_LEN = 90, FAR_LEN = 120, COLOR_LEN = 20;

    // 主菜单场景额外的动画：ui_menu_keyart_lighting_brightness.chore（长度 12 秒，Flat 切线）
    static final float BRI_LEN = 12;
    static final Key[] BRI_FILL = {new Key(3, FLAT, 1.0666667f, 0, 0), new Key(6, FLAT, 2, 0, 0), new Key(9, FLAT, 1, 0, 0)};
    static final Key[] BRI_OTHER = {new Key(3, FLAT, 1, 0, 0), new Key(6, FLAT, 2, 0, 0), new Key(9, FLAT, 1, 0, 0)};
    static final Key[] SPOT_POS = {new Key(0, FLAT, 12, 3, 0), new Key(6, FLAT, -10, -3, 0), new Key(12, FLAT, 12, 3, 0)};

    /** 引擎 KeyframedValue 求值（Knot=线性切线，Smooth=Catmull-Rom 邻点切线，Flat=零切线，Stepped=保持）。 */
    static void evalKfv(Key[] s, float t, float[] out) {
        int n = s.length;
        if (t < s[0].t || n == 1) {
            System.arraycopy(s[0].v, 0, out, 0, 3);
            return;
        }
        if (!(t < s[n - 1].t)) {
            System.arraycopy(s[n - 1].v, 0, out, 0, 3);
            return;
        }
        int lo = 0, hi = n - 1;
        while (hi - lo > 1) {
            int mid = (lo + hi) >> 1;
            if (t < s[mid].t) hi = mid;
            else lo = mid;
        }
        Key a0 = s[lo], b0 = s[hi];
        float x = (t - a0.t) / (b0.t - a0.t);
        if ((b0.mode == STEPPED || b0.mode == KNOT) && a0.mode == KNOT) {
            for (int i = 0; i < 3; i++) out[i] = (b0.v[i] - a0.v[i]) * x + a0.v[i];
            return;
        }
        if (a0.mode == STEPPED) {
            System.arraycopy(a0.v, 0, out, 0, 3);
            return;
        }
        x = Math.min(Math.max(x, 0.0f), 1.0f);
        for (int i = 0; i < 3; i++) {
            float b = a0.v[i], c = b0.v[i];
            float a = a0.mode == FLAT ? c : a0.mode == SMOOTH ? (lo - 1 >= 0 ? s[lo - 1].v[i] : b) : 2 * b - c;
            float d = b0.mode == FLAT ? b : b0.mode == SMOOTH ? (hi + 1 < n ? s[hi + 1].v[i] : c) : 2 * c - b;
            float c3 = 1.5f * b - 0.5f * a - 1.5f * c + 0.5f * d;
            float c2 = a - 2.5f * b + 2 * c - 0.5f * d;
            float c1 = -0.5f * a + 0.5f * c;
            out[i] = ((c3 * x + c2) * x + c1) * x + b;
        }
    }

    /** 游戏待机动画的时间轴：每个动画各有速度系数（0.85~1.15）和起始位置。 */
    static final class Clock {
        final float len, speed, phase;

        Clock(float len, Random rng) {
            this.len = len;
            this.speed = 0.85f + 0.30f * rng.nextFloat();
            this.phase = len * rng.nextFloat();
        }

        float at(double el) {
            double x = ((double) phase + (double) speed * el) % (double) len;
            if (x < 0) x += len;
            return (float) x;
        }
    }

    static final class Clocks {
        final Clock rot, near, far, col, bri;

        Clocks(Random rng) {
            rot = new Clock(ROT_LEN, rng);
            near = new Clock(NEAR_LEN, rng);
            far = new Clock(FAR_LEN, rng);
            col = new Clock(COLOR_LEN, rng);
            bri = new Clock(BRI_LEN, rng);
        }
    }

    static final class Params {
        float ang, farT, nearT;
        final float[] cFill = new float[3], cFar = new float[3], cNear = new float[3], cGrad = new float[3];
        final float[] br = new float[4];
        final float[] spot = new float[2];
    }

    /** el：已按播放速度累加的动画时间（秒）。 */
    static void compute(double el, int scene, int tone, Clocks k, Params p) {
        p.ang = (float) (2 * Math.PI) * k.rot.at(el) / ROT_LEN;
        p.farT = 2 * k.far.at(el) / FAR_LEN;
        p.nearT = 2 * k.near.at(el) / NEAR_LEN;
        float tc = k.col.at(el);
        evalKfv(COL_FILL, tc, p.cFill);
        evalKfv(COL_FAR, tc, p.cFar);
        evalKfv(COL_NEAR, tc, p.cNear);
        evalKfv(COL_GRAD, tc, p.cGrad);
        if (scene == SCENE_MENU) {
            float tb = k.bri.at(el);
            float[] v = new float[3];
            evalKfv(BRI_FILL, tb, v);
            p.br[0] = v[0];
            evalKfv(BRI_OTHER, tb, v);
            p.br[1] = p.br[2] = p.br[3] = v[0];
            evalKfv(SPOT_POS, tb, v);
            p.spot[0] = v[0];
            p.spot[1] = v[1];
        } else {
            p.br[0] = p.br[1] = p.br[2] = p.br[3] = 1.0f;
            p.spot[0] = -0.14144018f;   // 标题界面的聚光静止
            p.spot[1] = -0.052455753f;
        }
        // 提亮色调：亮度倍率取游戏自己亮度动画的峰值 2
        if (tone == TONE_BRIGHT) for (int i = 0; i < 4; i++) p.br[i] *= 2.0f;
    }
}

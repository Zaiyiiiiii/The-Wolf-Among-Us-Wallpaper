package com.zaiyii.wolves;

import android.content.res.AssetManager;
import android.opengl.GLES30;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;

/**
 * OpenGL ES 3.0 渲染：一个全屏三角形 + 像素着色器（移植自 native/ps.hlsl）。
 * 六个图层按引擎顺序在 8 位渲染目标上混合，层间取整。必须在持有 GL 上下文的线程里调用。
 */
final class WolfRenderer {
    private static final String TAG = "WolfRenderer";

    // 引擎投影：垂直方向固定为 ±5.39776 世界单位，横向按宽高比放大
    private static final float HALF_H = 41.0f / 7.5957537f;
    private static final float MAX_RADIUS = 11.4f;     // 花卉层网格的内切圆半径 11.52，留一点余量

    private static final String VS =
            "#version 300 es\n"
                    + "void main() {\n"
                    + "  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
                    + "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                    + "}\n";

    private static final String FS =
            "#version 300 es\n"
                    + "precision highp float;\n"
                    + "precision highp sampler2D;\n"
                    + "uniform vec2 uRes; uniform vec2 uExt;\n"
                    + "uniform float uAng, uFarT, uNearT;\n"
                    + "uniform vec4 uCFill, uCFar, uCNear, uCGrad, uBr, uGeo, uSpot, uTone;\n"
                    + "uniform sampler2D tWall;\n"
                    + "uniform sampler2D tSpot;\n"
                    + "out vec4 outColor;\n"
                    + "vec3 q8(vec3 x) { return floor(clamp(x, 0.0, 1.0) * 255.0 + 0.5) / 255.0; }\n"
                    + "bool inside2(vec2 v) { return v.x >= 0.0 && v.x <= 1.0 && v.y >= 0.0 && v.y <= 1.0; }\n"
                    // 墙纸层：网格 5.12x5.12，整体缩放 4.5，绕原点旋转；UV = 缩放*(u,v) + 位移（位移最后加）；混合 SrcAlpha, InvSrcAlpha
                    + "vec3 wallLayer(vec3 dst, vec2 w, float cs, float sn, float tile, float tr, float alphaK, vec3 col) {\n"
                    + "  vec2 loc = vec2(cs * w.x + sn * w.y, -sn * w.x + cs * w.y) / 4.5;\n"
                    + "  vec2 v = (loc + 2.56) / 5.12;\n"
                    + "  if (!inside2(v)) return dst;\n"
                    + "  vec2 uv = vec2(v.x, 1.0 - v.y) * tile + tr;\n"
                    + "  vec4 t = textureLod(tWall, uv, 0.0);\n"
                    + "  float a = clamp(t.a, 0.0, 1.0) * alphaK;\n"
                    + "  return q8(dst * (1.0 - a) + t.rgb * col * a);\n"
                    + "}\n"
                    + "void main() {\n"
                    + "  vec2 ndc = vec2(gl_FragCoord.x / uRes.x * 2.0 - 1.0, gl_FragCoord.y / uRes.y * 2.0 - 1.0);\n"
                    + "  vec2 w = ndc * uExt;\n"
                    + "  float cs = cos(uAng), sn = sin(uAng);\n"
                    + "  vec3 c = vec3(0.0);\n"
                    + "  if (uTone.x > 0.5) {\n"
                    // 浅色色调：几何、旋转、滚动、图案与游戏一致；底色近白，花卉淡灰蓝，暗角与底部渐变改为白色版本
                    + "    vec3 hA = uCFar.rgb / max(max(uCFar.r, max(uCFar.g, uCFar.b)), 1e-4);\n"
                    + "    vec3 hN = uCNear.rgb / max(max(uCNear.r, max(uCNear.g, uCNear.b)), 1e-4);\n"
                    + "    vec3 dA = hA - dot(hA, vec3(1.0 / 3.0)), dN = hN - dot(hN, vec3(1.0 / 3.0));\n"
                    + "    vec3 base = vec3(0.968, 0.970, 0.976);\n"
                    + "    c = q8(base);\n"
                    + "    c = wallLayer(c, w, cs, sn, 6.0, 0.0, 0.09, vec3(0.0));\n"
                    + "    c = wallLayer(c, w, cs, sn, uGeo.y, uFarT, 0.55, vec3(0.70, 0.77, 0.88) + dA * 0.07);\n"
                    + "    c = wallLayer(c, w, cs, sn, 2.0, uNearT, 0.65, vec3(0.52, 0.62, 0.79) + dN * 0.07);\n"
                    + "    vec2 sv2 = ((w - uSpot.xy) / uGeo.z + 10.24) / 20.48;\n"
                    + "    if (inside2(sv2)) {\n"
                    + "      vec2 d2 = (sv2 - 0.5) * 2.0;\n"
                    + "      vec3 s2 = textureLod(tSpot, (1.0 - abs(d2)) * 0.991285 + 0.0043573, 0.0).rgb;\n"
                    + "      vec3 k2 = clamp(s2 * 1.3, 0.0, 1.0);\n"
                    + "      c = q8(mix(vec3(1.0), c, k2 * k2));\n"
                    + "    }\n"
                    + "    vec2 gv2 = (vec2(w.x / uSpot.z, (w.y - uGeo.w) / uSpot.w) + 10.0) / 20.0;\n"
                    + "    if (gv2.y <= 1.0) c = q8(mix(c, vec3(1.0), 0.62 * clamp(1.0 - gv2.y, 0.0, 1.0)));\n"
                    + "    outColor = vec4(c, 1.0);\n"
                    + "    return;\n"
                    + "  }\n"
                    // 层 0 fill：纯色 × 亮度倍率。游戏里它是 19.2x10.8（乘缩放）的矩形，正好盖住 16:9 的屏幕；
                    // 手机的竖屏 / 超宽屏会超出矩形，这里让它铺满整个屏幕（16:9 下结果相同）
                    + "  c = q8(uCFill.rgb * uBr.x);\n"
                    // 层 1 back：黑色，常量透明度 0.5，UV 缩放 6
                    + "  c = wallLayer(c, w, cs, sn, 6.0, 0.0, 0.5, vec3(0.0));\n"
                    // 层 2 far / 层 3 near
                    + "  c = wallLayer(c, w, cs, sn, uGeo.y, uFarT, 1.0, uCFar.rgb * uBr.y);\n"
                    + "  c = wallLayer(c, w, cs, sn, 2.0, uNearT, 1.0, uCNear.rgb * uBr.z);\n"
                    // 层 4 spotlight：混合 DstCol*Zero；UV = (1-|dx|, 1-|dy|) 再经 (0.0043573, 0.991285) 的变换
                    + "  vec2 sv = ((w - uSpot.xy) / uGeo.z + 10.24) / 20.48;\n"
                    + "  if (inside2(sv)) {\n"
                    + "    vec2 d = (sv - 0.5) * 2.0;\n"
                    + "    vec2 suv = (1.0 - abs(d)) * 0.991285 + 0.0043573;\n"
                    + "    c = q8(c * textureLod(tSpot, suv, 0.0).rgb);\n"
                    + "  }\n"
                    // 层 5 gradient：20x20 网格，顶点 alpha 自下而上 1→0；混合 SrcAlpha, One。
                    // 游戏里网格的下缘和左右缘在 16:9 屏幕之外；竖屏 / 超宽屏放大后会露出来，所以向下和左右延续（alpha 保持 1），16:9 下结果相同
                    + "  vec2 gv = (vec2(w.x / uSpot.z, (w.y - uGeo.w) / uSpot.w) + 10.0) / 20.0;\n"
                    + "  if (gv.y <= 1.0) c = q8(c + uCGrad.rgb * uBr.w * clamp(1.0 - gv.y, 0.0, 1.0));\n"
                    + "  outColor = vec4(c, 1.0);\n"
                    + "}\n";

    private int program, vao, texWall, texSpot;
    private int uRes, uExt, uAng, uFarT, uNearT, uCFill, uCFar, uCNear, uCGrad, uBr, uGeo, uSpot, uTone;

    /** 创建着色器和贴图；失败时抛出 IllegalStateException。 */
    void init(AssetManager assets) throws IOException {
        program = link(compile(GLES30.GL_VERTEX_SHADER, VS), compile(GLES30.GL_FRAGMENT_SHADER, FS));
        uRes = GLES30.glGetUniformLocation(program, "uRes");
        uExt = GLES30.glGetUniformLocation(program, "uExt");
        uAng = GLES30.glGetUniformLocation(program, "uAng");
        uFarT = GLES30.glGetUniformLocation(program, "uFarT");
        uNearT = GLES30.glGetUniformLocation(program, "uNearT");
        uCFill = GLES30.glGetUniformLocation(program, "uCFill");
        uCFar = GLES30.glGetUniformLocation(program, "uCFar");
        uCNear = GLES30.glGetUniformLocation(program, "uCNear");
        uCGrad = GLES30.glGetUniformLocation(program, "uCGrad");
        uBr = GLES30.glGetUniformLocation(program, "uBr");
        uGeo = GLES30.glGetUniformLocation(program, "uGeo");
        uSpot = GLES30.glGetUniformLocation(program, "uSpot");
        uTone = GLES30.glGetUniformLocation(program, "uTone");

        int[] o = new int[1];
        GLES30.glGenVertexArrays(1, o, 0);
        vao = o[0];

        texWall = upload(TextureDecoder.decodeBc3(readAsset(assets, "wall.bc3"), 2048, 2048), 2048, GLES30.GL_REPEAT);
        texSpot = upload(TextureDecoder.decodeBc1(readAsset(assets, "spot.bc1"), 512, 512), 512, GLES30.GL_CLAMP_TO_EDGE);
    }

    void draw(int w, int h, int scene, int tone, Curves.Params p) {
        float aspect = (float) w / (float) h;
        float extX = aspect >= 1 ? HALF_H * aspect : HALF_H;     // 竖屏：短边对应引擎的垂直范围
        float extY = aspect >= 1 ? HALF_H : HALF_H / aspect;
        // 花卉层是 23.04 见方的网格并绕原点旋转，只有半径 11.52 的圆内一定被覆盖。16:9 的半对角线是 11.0，游戏里不会露边；
        // 手机的竖屏 / 超宽屏更长，四角会露出底色，所以超出时整体放大（缩小可见的世界范围）直到铺满。16:9 不受影响。
        float diag = (float) Math.hypot(extX, extY);
        if (diag > MAX_RADIUS) {
            float k = MAX_RADIUS / diag;
            extX *= k;
            extY *= k;
        }

        // 场景几何（来自两个场景文件）：标题 = ui_menu_background_pause.scene，主菜单 = ui_menu_background_main.scene
        float fillScale, farTile, spotScale, gradY, gradSx, gradSy;
        if (scene == Curves.SCENE_MENU) {
            fillScale = 1.0f; farTile = 3.5f; spotScale = 3.0f; gradY = -1.5f; gradSx = 1.0f; gradSy = 0.4f;
        } else {
            fillScale = 2.0f; farTile = 3.0f; spotScale = 2.0f; gradY = -0.5f; gradSx = 2.0f; gradSy = 1.0f;
        }

        GLES30.glViewport(0, 0, w, h);
        GLES30.glDisable(GLES30.GL_BLEND);
        GLES30.glDisable(GLES30.GL_DEPTH_TEST);
        GLES30.glDisable(GLES30.GL_CULL_FACE);
        GLES30.glUseProgram(program);
        GLES30.glUniform2f(uRes, w, h);
        GLES30.glUniform2f(uExt, extX, extY);
        GLES30.glUniform1f(uAng, p.ang);
        GLES30.glUniform1f(uFarT, p.farT);
        GLES30.glUniform1f(uNearT, p.nearT);
        GLES30.glUniform4f(uCFill, p.cFill[0], p.cFill[1], p.cFill[2], 0f);
        GLES30.glUniform4f(uCFar, p.cFar[0], p.cFar[1], p.cFar[2], 0f);
        GLES30.glUniform4f(uCNear, p.cNear[0], p.cNear[1], p.cNear[2], 0f);
        GLES30.glUniform4f(uCGrad, p.cGrad[0], p.cGrad[1], p.cGrad[2], 0f);
        GLES30.glUniform4f(uBr, p.br[0], p.br[1], p.br[2], p.br[3]);
        GLES30.glUniform4f(uGeo, fillScale, farTile, spotScale, gradY);
        GLES30.glUniform4f(uSpot, p.spot[0], p.spot[1], gradSx, gradSy);
        GLES30.glUniform4f(uTone, tone == Curves.TONE_LIGHT ? 1f : 0f, 0f, 0f, 0f);

        GLES30.glActiveTexture(GLES30.GL_TEXTURE0);
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, texWall);
        GLES30.glUniform1i(GLES30.glGetUniformLocation(program, "tWall"), 0);
        GLES30.glActiveTexture(GLES30.GL_TEXTURE1);
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, texSpot);
        GLES30.glUniform1i(GLES30.glGetUniformLocation(program, "tSpot"), 1);

        GLES30.glBindVertexArray(vao);
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3);
    }

    void release() {
        if (program != 0) GLES30.glDeleteProgram(program);
        if (texWall != 0) GLES30.glDeleteTextures(1, new int[]{texWall}, 0);
        if (texSpot != 0) GLES30.glDeleteTextures(1, new int[]{texSpot}, 0);
        if (vao != 0) GLES30.glDeleteVertexArrays(1, new int[]{vao}, 0);
        program = texWall = texSpot = vao = 0;
    }

    private static int upload(ByteBuffer rgba, int size, int wrap) {
        int[] t = new int[1];
        GLES30.glGenTextures(1, t, 0);
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, t[0]);
        GLES30.glPixelStorei(GLES30.GL_UNPACK_ALIGNMENT, 4);
        GLES30.glTexImage2D(GLES30.GL_TEXTURE_2D, 0, GLES30.GL_RGBA8, size, size, 0, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, rgba);
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR);   // 引擎采样器等效于 mip0 双线性
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR);
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, wrap);
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, wrap);
        return t[0];
    }

    private static byte[] readAsset(AssetManager am, String name) throws IOException {
        try (InputStream in = am.open(name)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream(in.available() + 16);
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return out.toByteArray();
        }
    }

    private static int compile(int type, String src) {
        int s = GLES30.glCreateShader(type);
        GLES30.glShaderSource(s, src);
        GLES30.glCompileShader(s);
        int[] ok = new int[1];
        GLES30.glGetShaderiv(s, GLES30.GL_COMPILE_STATUS, ok, 0);
        if (ok[0] == 0) {
            String log = GLES30.glGetShaderInfoLog(s);
            Log.e(TAG, "着色器编译失败: " + log);
            GLES30.glDeleteShader(s);
            throw new IllegalStateException("shader compile failed: " + log);
        }
        return s;
    }

    private static int link(int vs, int fs) {
        int p = GLES30.glCreateProgram();
        GLES30.glAttachShader(p, vs);
        GLES30.glAttachShader(p, fs);
        GLES30.glLinkProgram(p);
        int[] ok = new int[1];
        GLES30.glGetProgramiv(p, GLES30.GL_LINK_STATUS, ok, 0);
        if (ok[0] == 0) {
            String log = GLES30.glGetProgramInfoLog(p);
            Log.e(TAG, "着色器链接失败: " + log);
            throw new IllegalStateException("program link failed: " + log);
        }
        GLES30.glDeleteShader(vs);
        GLES30.glDeleteShader(fs);
        return p;
    }
}

package com.zaiyii.wolves;

import android.content.SharedPreferences;
import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLExt;
import android.opengl.EGLSurface;
import android.preference.PreferenceManager;
import android.service.wallpaper.WallpaperService;
import android.util.Log;
import android.view.SurfaceHolder;

import java.util.Random;

public class WolfWallpaperService extends WallpaperService {
    @Override
    public Engine onCreateEngine() {
        return new WolfEngine();
    }

    private final class WolfEngine extends Engine implements SharedPreferences.OnSharedPreferenceChangeListener {
        private static final String TAG = "WolfEngine";

        private SharedPreferences prefs;
        private volatile int scene = Curves.SCENE_TITLE;
        private volatile int tone = Curves.TONE_ORIGINAL;
        private volatile float speed = 1.0f;
        private volatile int fps = 30;

        private final Object lock = new Object();
        private Thread thread;
        private volatile boolean running;
        private volatile boolean visible;

        @Override
        public void onCreate(SurfaceHolder holder) {
            super.onCreate(holder);
            prefs = PreferenceManager.getDefaultSharedPreferences(WolfWallpaperService.this);
            prefs.registerOnSharedPreferenceChangeListener(this);
            loadPrefs();
        }

        @Override
        public void onDestroy() {
            prefs.unregisterOnSharedPreferenceChangeListener(this);
            super.onDestroy();
        }

        @Override
        public void onSharedPreferenceChanged(SharedPreferences sp, String key) {
            loadPrefs();
        }

        private void loadPrefs() {
            scene = "menu".equals(prefs.getString("scene", "title")) ? Curves.SCENE_MENU : Curves.SCENE_TITLE;
            String t = prefs.getString("tone", "original");
            tone = "light".equals(t) ? Curves.TONE_LIGHT : "bright".equals(t) ? Curves.TONE_BRIGHT : Curves.TONE_ORIGINAL;
            try {
                speed = Math.max(0f, Math.min(5f, Float.parseFloat(prefs.getString("speed", "1"))));
            } catch (NumberFormatException e) {
                speed = 1.0f;
            }
            try {
                fps = Math.max(5, Math.min(120, Integer.parseInt(prefs.getString("fps", "30"))));
            } catch (NumberFormatException e) {
                fps = 30;
            }
        }

        @Override
        public void onSurfaceCreated(SurfaceHolder holder) {
            super.onSurfaceCreated(holder);
            running = true;
            thread = new Thread(() -> renderLoop(holder), "wolf-render");
            thread.start();
        }

        @Override
        public void onSurfaceDestroyed(SurfaceHolder holder) {
            running = false;
            synchronized (lock) {
                lock.notifyAll();
            }
            if (thread != null) {
                try {
                    thread.join(2000);
                } catch (InterruptedException ignored) {
                    Thread.currentThread().interrupt();
                }
                thread = null;
            }
            super.onSurfaceDestroyed(holder);
        }

        @Override
        public void onVisibilityChanged(boolean v) {
            visible = v;
            synchronized (lock) {
                lock.notifyAll();
            }
        }

        // ---------------- 渲染线程 ----------------
        private void renderLoop(SurfaceHolder holder) {
            EGLDisplay dpy = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
            int[] ver = new int[2];
            if (!EGL14.eglInitialize(dpy, ver, 0, ver, 1)) {
                Log.e(TAG, "eglInitialize 失败");
                return;
            }
            EGLContext ctx = EGL14.EGL_NO_CONTEXT;
            EGLSurface surf = EGL14.EGL_NO_SURFACE;
            WolfRenderer renderer = new WolfRenderer();
            try {
                int[] attr = {
                        EGL14.EGL_RENDERABLE_TYPE, EGLExt.EGL_OPENGL_ES3_BIT_KHR,
                        EGL14.EGL_RED_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8, EGL14.EGL_BLUE_SIZE, 8, EGL14.EGL_ALPHA_SIZE, 8,
                        EGL14.EGL_NONE};
                EGLConfig[] cfgs = new EGLConfig[1];
                int[] num = new int[1];
                if (!EGL14.eglChooseConfig(dpy, attr, 0, cfgs, 0, 1, num, 0) || num[0] < 1) {
                    Log.e(TAG, "找不到 OpenGL ES 3.0 的 EGL 配置");
                    return;
                }
                ctx = EGL14.eglCreateContext(dpy, cfgs[0], EGL14.EGL_NO_CONTEXT, new int[]{EGL14.EGL_CONTEXT_CLIENT_VERSION, 3, EGL14.EGL_NONE}, 0);
                surf = EGL14.eglCreateWindowSurface(dpy, cfgs[0], holder.getSurface(), new int[]{EGL14.EGL_NONE}, 0);
                if (ctx == EGL14.EGL_NO_CONTEXT || surf == EGL14.EGL_NO_SURFACE || !EGL14.eglMakeCurrent(dpy, surf, surf, ctx)) {
                    Log.e(TAG, "创建 EGL 上下文 / 表面失败");
                    return;
                }
                EGL14.eglSwapInterval(dpy, 1);
                renderer.init(getAssets());

                // 每次启动：各动画随机取速度系数（0.85~1.15）和起始位置，与游戏的待机动画一致
                Curves.Clocks clocks = new Curves.Clocks(new Random());
                Curves.Params params = new Curves.Params();
                double anim = 0;
                long last = System.nanoTime();
                int[] size = new int[1];
                while (running) {
                    synchronized (lock) {
                        while (running && !visible) {
                            try {
                                lock.wait();
                            } catch (InterruptedException e) {
                                return;
                            }
                            last = System.nanoTime();     // 不可见期间不累计动画时间
                        }
                    }
                    if (!running) break;
                    long frameStart = System.nanoTime();
                    double dt = (frameStart - last) / 1e9;
                    last = frameStart;
                    if (dt > 0.25) dt = 0.25;            // 卡顿 / 睡眠恢复后不要一下跳很远
                    anim += dt * speed;

                    EGL14.eglQuerySurface(dpy, surf, EGL14.EGL_WIDTH, size, 0);
                    int w = size[0];
                    EGL14.eglQuerySurface(dpy, surf, EGL14.EGL_HEIGHT, size, 0);
                    int h = size[0];
                    if (w > 0 && h > 0) {
                        Curves.compute(anim, scene, tone, clocks, params);
                        renderer.draw(w, h, scene, tone, params);
                        if (!EGL14.eglSwapBuffers(dpy, surf)) break;
                    }

                    long budget = 1_000_000_000L / fps;
                    long rest = budget - (System.nanoTime() - frameStart);
                    if (rest > 1_000_000L) {
                        try {
                            Thread.sleep(rest / 1_000_000L);
                        } catch (InterruptedException e) {
                            return;
                        }
                    }
                }
            } catch (Exception e) {
                Log.e(TAG, "渲染线程异常", e);
            } finally {
                try {
                    renderer.release();
                } catch (Exception ignored) {
                    // 上下文可能已失效
                }
                EGL14.eglMakeCurrent(dpy, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT);
                if (surf != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(dpy, surf);
                if (ctx != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(dpy, ctx);
                EGL14.eglTerminate(dpy);
            }
        }
    }
}

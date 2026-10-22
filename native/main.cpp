// 《The Wolf Among Us》重制版 标题界面背景 —— 原生 Win32 + Direct3D 11 实现（无任何运行库依赖）。
//
// 数据与逻辑来源（均为读取，不是估算）：
//   * 场景/材质/网格/动画关键帧：重制版 Fables_pc_Boot_ar_* 归档（ui_menu_background_pause.scene 及其 chore）。
//   * 贴图：同一归档里的原始 BC3 / BC1 压缩数据，原样嵌入，由 GPU 解码。
//   * 像素公式、混合模式、UV 变换顺序：游戏着色器反汇编，以及 RenderDoc 抓帧里的真实常量缓冲。
//   * 曲线插值：游戏可执行文件中 KeyframedValue<T> 求值函数（Ghidra 反编译）：
//       Knot=线性切线, Smooth=Catmull-Rom 邻点切线, Flat=零切线, Stepped=保持；系数 0.5/1.5/2.5；四元数为球面插值。
//   * 验证：网页同款逻辑与游戏自己渲染的背景逐像素比对，平均误差约 0.15/255。
//
// 用法:
//   WolfAmongUsTitleLive.exe                全屏播放，Esc 退出
//   WolfAmongUsTitleLive.exe --window       窗口播放
//   WolfAmongUsTitleLive.exe --wallpaper    嵌入桌面壁纸层（图标之下），在托盘图标上右键：勾选「标题界面」/「主菜单」，或退出
//   可选: --scene title|menu  起始场景（默认沿用上次选择）   --seed N  固定随机   --speed K  全局速度倍率   --t SEC  冻结在第 SEC 秒
//         --tone original|bright|light  色调   --lang auto|zh|en|ja  界面语言（默认沿用上次选择，首次为自动：跟随系统界面语言）
//   托盘图标（所有模式都有）右键：场景（可勾选）、播放速度（子菜单，可勾选）、全屏（可勾选，壁纸模式没有）、退出
//   全屏/窗口模式下：Tab 切换场景，T=标题界面，M=主菜单；↑/↓ 调节播放速度；F / F11 / Alt+Enter / 双击 切换全屏与窗口；窗口模式可右键弹出同一个菜单
//   场景：标题界面 = ui_menu_background_pause.scene；主菜单 = ui_menu_background_main.scene
//         （主菜单多出：材质色亮度倍率 1→2→1、聚光 12 秒来回移动，来自 ui_menu_keyart_lighting_brightness.chore）
//   离屏导出(验证用): --dump out.bmp --size 3740x2104 [--dbg ang,far,near,cfill(3),cfar(3),cnear(3),cgrad(3)]
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

#define IDI_ICON1 1
#define IDR_WALL 101
#define IDR_SPOT 102
#define IDR_PS 103
#define IDR_VS 104
#define WM_TRAY (WM_APP + 1)
#define ID_EXIT 1001
#define ID_TITLE 1002
#define ID_MENU 1003
#define ID_FULL 1004
#define ID_WIN 1005
#define ID_WALL 1006
#define ID_TONE0 1007
#define ID_TONE1 1008
#define ID_TONE2 1009
#define ID_SPEED0 2000
#define ID_LANG0 3000
#define ID_AUTOSTART 1010

// ===================== 引擎关键帧求值（逐字对应反编译代码） =====================
enum { STEPPED = 1, KNOT = 2, SMOOTH = 3, FLAT = 4 };
struct Key { float t; int mode; float v[3]; };

static void evalKfv(const Key* S, int n, float t, float out[3])
{
    if (t < S[0].t || n == 1) { for (int i = 0; i < 3; i++) out[i] = S[0].v[i]; return; }
    if (!(t < S[n - 1].t)) { for (int i = 0; i < 3; i++) out[i] = S[n - 1].v[i]; return; }
    int lo = 0, hi = n - 1;
    while (hi - lo > 1) { int mid = (lo + hi) >> 1; if (t < S[mid].t) hi = mid; else lo = mid; }
    const Key& A = S[lo]; const Key& B = S[hi];
    float x = (t - A.t) / (B.t - A.t);
    if ((B.mode == STEPPED || B.mode == KNOT) && A.mode == KNOT) { for (int i = 0; i < 3; i++) out[i] = (B.v[i] - A.v[i]) * x + A.v[i]; return; }
    if (A.mode == STEPPED) { for (int i = 0; i < 3; i++) out[i] = A.v[i]; return; }
    x = std::fmin(std::fmax(x, 0.0f), 1.0f);
    for (int i = 0; i < 3; i++)
    {
        float b = A.v[i], c = B.v[i];
        float a = A.mode == FLAT ? c : A.mode == SMOOTH ? (lo - 1 >= 0 ? S[lo - 1].v[i] : b) : 2 * b - c;
        float d = B.mode == FLAT ? b : B.mode == SMOOTH ? (hi + 1 < n ? S[hi + 1].v[i] : c) : 2 * c - b;
        float c3 = 1.5f * b - 0.5f * a - 1.5f * c + 0.5f * d;
        float c2 = a - 2.5f * b + 2 * c - 0.5f * d;
        float c1 = -0.5f * a + 0.5f * c;
        out[i] = ((c3 * x + c2) * x + c1) * x + b;
    }
}

// ui_menu_background_color.chore（长度 20 秒）：各图层的 Render Diffuse Color
static const Key COL_FAR[]  = { {0, FLAT, {0.2f, 0.051f, 0.098f}}, {5, FLAT, {0.2f, 0.078f, 0.057f}}, {15, FLAT, {0.2f, 0.051f, 0.188f}}, {20, FLAT, {0.2f, 0.051f, 0.098f}} };
static const Key COL_NEAR[] = { {0, FLAT, {0.2f, 0.051f, 0.2f}}, {5, FLAT, {0.2f, 0.051f, 0.125f}}, {15, FLAT, {0.11f, 0.051f, 0.2f}}, {20, FLAT, {0.2f, 0.051f, 0.2f}} };
static const Key COL_FILL[] = { {0, SMOOTH, {0.102f, 0.024f, 0.051f}}, {5, SMOOTH, {0.102f, 0.035f, 0.024f}}, {15, SMOOTH, {0.102f, 0.024f, 0.098f}}, {20, SMOOTH, {0.102f, 0.024f, 0.051f}} };
static const Key COL_GRAD[] = { {0, SMOOTH, {0.024f, 0.0f, 0.149f}}, {5, SMOOTH, {0.098f, 0.0f, 0.149f}}, {15, SMOOTH, {0.0f, 0.067f, 0.149f}}, {19.980469f, SMOOTH, {0.024f, 0.0f, 0.149f}} };
// 其它动画：旋转 300 秒转 360°（四元数关键帧 0/90/180/270/360°，球面插值 => 角度线性）；近景/远景 UV 位移 0→2，90 / 120 秒
static const float ROT_LEN = 300, NEAR_LEN = 90, FAR_LEN = 120, COLOR_LEN = 20;

// 主菜单场景 (ui_menu_background_main.scene) 额外的动画：ui_menu_keyart_lighting_brightness.chore（长度 12 秒，Flat 切线）
//   * fill / gradient / near / far 的材质色亮度倍率（PS cb3_v1）：1 → 2 → 1；fill 的第一个关键帧是 1.0666667
//   * spotlight 的位置（AbsoluteNode）：(12,3) → (-10,-3) → (12,3)
static const float BRI_LEN = 12;
static const Key BRI_FILL[] = { {3, FLAT, {1.0666667f, 0, 0}}, {6, FLAT, {2, 0, 0}}, {9, FLAT, {1, 0, 0}} };
static const Key BRI_OTHER[] = { {3, FLAT, {1, 0, 0}}, {6, FLAT, {2, 0, 0}}, {9, FLAT, {1, 0, 0}} };
static const Key SPOT_POS[] = { {0, FLAT, {12, 3, 0}}, {6, FLAT, {-10, -3, 0}}, {12, FLAT, {12, 3, 0}} };

enum Scene { SCENE_TITLE = 0, SCENE_MENU = 1 };

// 引擎的待机动画由 Walk Animator 播放：Idle Scale Range = 0.85~1.15，Idle Force Start = -1。
struct Clock
{
    float len, speed, phase;
    float at(double el, float g) const { double x = std::fmod((double)phase + (double)speed * el * g, (double)len); if (x < 0) x += len; return (float)x; }
};

struct Params { float ang, farT, nearT, cF[3], cFar[3], cNear[3], cG[3]; float br[4]; float spot[2]; };

struct Cfg
{
    bool window = false, wallpaper = false, hasSeed = false, hasT = false, hasDbg = false, hasScene = false, hasSpeed = false, hasTone = false, hasLang = false;
    int lang = 0;
    int tone = 0;
    unsigned seed = 0; float speed = 1.0f, freezeT = 0; Params dbg{}; int scene = SCENE_TITLE;
    std::wstring dump, log; int dumpW = 1920, dumpH = 1080;
};

struct CB
{
    float res[2], ext[2];
    float ang, farT, nearT, pad;
    float cFill[4], cFar[4], cNear[4], cGrad[4];
    float br[4], geo[4], spot[4], tone[4];
};

// ===================== 全局状态 =====================
static Cfg g_cfg;
static Clock g_rot, g_near, g_far, g_col, g_bri;
static int g_scene = SCENE_TITLE;                 // 当前场景：标题界面 / 主菜单
enum Tone { TONE_ORIGINAL, TONE_BRIGHT, TONE_LIGHT };   // 原版 / 亮度 2× / 浅色（白/亮灰底）
static int g_tone = TONE_ORIGINAL;                // 色调：原版 / 亮色
static HWND g_hwnd;
static bool g_wall = false;                       // 当前显示模式：背景（嵌入桌面）
static bool g_full = false;                       // 当前显示模式：全屏（g_wall 和 g_full 都为 false = 窗口）
static bool g_switching = false;                  // 正在重建主窗口：旧窗口销毁时不要退出程序
static HINSTANCE g_inst;
static ComPtr<ID3D11Device> g_dev;
static ComPtr<ID3D11DeviceContext> g_ctx;
static ComPtr<IDXGISwapChain1> g_sc;
static ComPtr<ID3D11RenderTargetView> g_rtv;
static ComPtr<ID3D11VertexShader> g_vs;
static ComPtr<ID3D11PixelShader> g_ps;
static ComPtr<ID3D11Buffer> g_cb;
static ComPtr<ID3D11ShaderResourceView> g_wallSrv, g_spotSrv;
static ComPtr<ID3D11SamplerState> g_sWall, g_sSpot;
static UINT g_w = 1280, g_h = 720;
static std::wstring g_prevWallpaper;
static LARGE_INTEGER g_qpf, g_t0;

// ===================== 多语言 =====================
// 界面文字按「语言设置」选择：自动（跟随系统界面语言：中文 / 日语，其余用英文）或手动指定；托盘菜单里可切换并记住。
enum LangSetting { LP_AUTO, LP_ZH, LP_EN, LP_JA, LP_COUNT };
enum Str
{
    S_APP_TITLE, S_ERR_CODE, S_ERR_SWAP, S_ERR_DEVICE, S_ERR_SHADER, S_ERR_TEXTURE, S_ERR_WINDOW,
    S_SPEED_PAUSE, S_SPEED_NORMAL, S_SCENE_TITLE, S_SCENE_MENU, S_SPEED, S_TONE_ORIG, S_TONE_BRIGHT, S_TONE_LIGHT,
    S_FULL, S_WIN, S_WALL, S_EXIT, S_LANG, S_LANG_AUTO, S_AUTOSTART, S_TIP, S_COUNT
};
static const wchar_t* const STR[3][S_COUNT] =
{
    { // 简体中文
        L"Wolf Among Us 标题背景", L"%s\n错误代码: 0x%08X", L"无法创建交换链", L"无法创建 Direct3D 11 设备", L"着色器创建失败", L"贴图创建失败", L"无法创建窗口",
        L"暂停", L"1×（原速）", L"标题界面", L"主菜单", L"播放速度", L"色调：原版", L"色调：提亮（亮度 2×）", L"色调：浅色（白/亮灰底）",
        L"全屏", L"窗口", L"背景（桌面壁纸）", L"退出动态背景", L"语言 / Language", L"自动", L"随系统自动启动",
        L"Wolf Among Us 动态背景（右键：场景 / 速度 / 色调 / 显示模式 / 语言 / 退出）"
    },
    { // English
        L"Wolf Among Us Title Background", L"%s\nError code: 0x%08X", L"Failed to create the swap chain", L"Failed to create the Direct3D 11 device", L"Failed to create the shaders", L"Failed to create the textures", L"Failed to create the window",
        L"Paused", L"1× (original)", L"Title screen", L"Main menu", L"Playback speed", L"Tone: Original", L"Tone: Brightened (2× brightness)", L"Tone: Light (white / light gray)",
        L"Fullscreen", L"Windowed", L"Wallpaper (desktop background)", L"Quit", L"语言 / Language", L"Auto", L"Start with Windows",
        L"Wolf Among Us live background (right-click: scene / speed / tone / display mode / language / quit)"
    },
    { // 日本語
        L"Wolf Among Us タイトル背景", L"%s\nエラーコード: 0x%08X", L"スワップチェーンを作成できません", L"Direct3D 11 デバイスを作成できません", L"シェーダーの作成に失敗しました", L"テクスチャの作成に失敗しました", L"ウィンドウを作成できません",
        L"一時停止", L"1×（等速）", L"タイトル画面", L"メインメニュー", L"再生速度", L"色調：オリジナル", L"色調：明るめ（明るさ 2×）", L"色調：ライト（白／明るいグレー）",
        L"フルスクリーン", L"ウィンドウ", L"壁紙（デスクトップ背景）", L"終了", L"言語 / Language", L"自動", L"Windows 起動時に自動起動",
        L"Wolf Among Us 動く背景（右クリック：シーン / 速度 / 色調 / 表示モード / 言語 / 終了）"
    },
};
static int g_langSetting = LP_AUTO;
static int effectiveLang()                         // 返回 STR 的下标：0 中文 1 英文 2 日文
{
    if (g_langSetting != LP_AUTO) return g_langSetting - 1;
    switch (PRIMARYLANGID(GetUserDefaultUILanguage()))
    {
    case LANG_CHINESE: return 0;
    case LANG_JAPANESE: return 2;
    default: return 1;
    }
}
static const wchar_t* T(Str s) { return STR[effectiveLang()][s]; }

static void fail(const wchar_t* what, HRESULT hr)
{
    wchar_t buf[256]; swprintf(buf, 256, T(S_ERR_CODE), what, (unsigned)hr);
    MessageBoxW(nullptr, buf, T(S_APP_TITLE), MB_ICONERROR);
}

static const void* resData(int id, DWORD* size)
{
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    HGLOBAL g = LoadResource(nullptr, r);
    *size = SizeofResource(nullptr, r);
    return LockResource(g);
}

static void computeParams(double el, Params& p)
{
    if (g_cfg.hasDbg) { p = g_cfg.dbg; return; }
    const float PI = 3.14159265358979f;
    // el 是已经按播放速度累加过的动画时间（调速时不会跳变）
    p.ang = 2 * PI * g_rot.at(el, 1.0f) / ROT_LEN;
    p.farT = 2 * g_far.at(el, 1.0f) / FAR_LEN;
    p.nearT = 2 * g_near.at(el, 1.0f) / NEAR_LEN;
    float tc = g_col.at(el, 1.0f);
    evalKfv(COL_FILL, 4, tc, p.cF); evalKfv(COL_FAR, 4, tc, p.cFar); evalKfv(COL_NEAR, 4, tc, p.cNear); evalKfv(COL_GRAD, 4, tc, p.cG);
    if (g_scene == SCENE_MENU)
    {
        float tb = g_bri.at(el, 1.0f), v[3];
        evalKfv(BRI_FILL, 3, tb, v);  p.br[0] = v[0];                 // fill
        evalKfv(BRI_OTHER, 3, tb, v); p.br[1] = p.br[2] = p.br[3] = v[0]; // far, near, gradient
        evalKfv(SPOT_POS, 3, tb, v);  p.spot[0] = v[0]; p.spot[1] = v[1];
    }
    else
    {
        p.br[0] = p.br[1] = p.br[2] = p.br[3] = 1.0f;
        p.spot[0] = -0.14144018f; p.spot[1] = -0.052455753f;          // 标题界面的聚光静止
    }
    // 色调：原版 = 游戏原样；亮色 = 亮度倍率取游戏自己的亮度动画峰值 2（主菜单的亮度动画是 1→2→1），整个动画期间保持
    if (g_tone == TONE_BRIGHT) for (int i = 0; i < 4; i++) p.br[i] *= 2.0f;
}

// 为当前主窗口（g_hwnd，客户区 g_w x g_h）创建交换链与渲染目标；切换显示模式时会重新调用
static bool createSwap()
{
    ComPtr<IDXGIDevice> dxgiDev; g_dev.As(&dxgiDev);
    ComPtr<IDXGIAdapter> adapter; dxgiDev->GetAdapter(&adapter);
    ComPtr<IDXGIFactory2> factory; adapter->GetParent(IID_PPV_ARGS(&factory));
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = g_w; sd.Height = g_h; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; sd.Scaling = DXGI_SCALING_STRETCH; sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    HRESULT hr = factory->CreateSwapChainForHwnd(g_dev.Get(), g_hwnd, &sd, nullptr, nullptr, &g_sc);
    if (FAILED(hr))
    {   // 兼容回退：传统交换效果
        sd.BufferCount = 1; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        hr = factory->CreateSwapChainForHwnd(g_dev.Get(), g_hwnd, &sd, nullptr, nullptr, &g_sc);
    }
    if (FAILED(hr)) { fail(T(S_ERR_SWAP), hr); return false; }
    factory->MakeWindowAssociation(g_hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    ComPtr<ID3D11Texture2D> bb; g_sc->GetBuffer(0, IID_PPV_ARGS(&bb));
    g_dev->CreateRenderTargetView(bb.Get(), nullptr, &g_rtv);
    return true;
}

static bool initGfx(bool offscreen)
{
    UINT flags = 0;
    const D3D_FEATURE_LEVEL fl[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, fl, 3, D3D11_SDK_VERSION, &g_dev, &got, &g_ctx);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, fl, 3, D3D11_SDK_VERSION, &g_dev, &got, &g_ctx);
    if (FAILED(hr)) { fail(T(S_ERR_DEVICE), hr); return false; }

    if (!offscreen && !createSwap()) return false;

    DWORD n;
    const void* vsb = resData(IDR_VS, &n); g_dev->CreateVertexShader(vsb, n, nullptr, &g_vs);
    const void* psb = resData(IDR_PS, &n); hr = g_dev->CreatePixelShader(psb, n, nullptr, &g_ps);
    if (FAILED(hr)) { fail(T(S_ERR_SHADER), hr); return false; }

    D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = sizeof(CB); bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_dev->CreateBuffer(&bd, nullptr, &g_cb);

    auto makeTex = [&](int id, UINT size, DXGI_FORMAT fmt, UINT pitch, ComPtr<ID3D11ShaderResourceView>& srv) -> bool
    {
        DWORD sz; const void* data = resData(id, &sz);
        D3D11_TEXTURE2D_DESC td = {}; td.Width = size; td.Height = size; td.MipLevels = 1; td.ArraySize = 1; td.Format = fmt;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sr = { data, pitch, 0 };
        ComPtr<ID3D11Texture2D> t;
        HRESULT h = g_dev->CreateTexture2D(&td, &sr, &t);
        if (FAILED(h)) { fail(T(S_ERR_TEXTURE), h); return false; }
        g_dev->CreateShaderResourceView(t.Get(), nullptr, &srv);
        return true;
    };
    if (!makeTex(IDR_WALL, 2048, DXGI_FORMAT_BC3_UNORM, 2048 / 4 * 16, g_wallSrv)) return false;
    if (!makeTex(IDR_SPOT, 512, DXGI_FORMAT_BC1_UNORM, 512 / 4 * 8, g_spotSrv)) return false;

    D3D11_RASTERIZER_DESC rs = {}; rs.FillMode = D3D11_FILL_SOLID; rs.CullMode = D3D11_CULL_NONE; rs.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> rss; g_dev->CreateRasterizerState(&rs, &rss); g_ctx->RSSetState(rss.Get());

    D3D11_SAMPLER_DESC sm = {}; sm.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sm.MaxLOD = 0; sm.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; g_dev->CreateSamplerState(&sm, &g_sWall);
    sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; g_dev->CreateSamplerState(&sm, &g_sSpot);
    return true;
}

static void drawTo(ID3D11RenderTargetView* rtv, UINT w, UINT h, const Params& p)
{
    const float HALF_H = 41.0f / 7.5957537f;           // 引擎投影：垂直方向固定为 ±5.39776 世界单位
    float aspect = (float)w / (float)h;
    CB cb = {};
    cb.res[0] = (float)w; cb.res[1] = (float)h;
    cb.ext[0] = aspect >= 1 ? HALF_H * aspect : HALF_H;   // 竖屏：短边对应引擎的垂直范围
    cb.ext[1] = aspect >= 1 ? HALF_H : HALF_H / aspect;
    // 花卉层是 23.04 见方的网格并绕原点旋转，只有半径 11.52 的圆内一定被覆盖。16:9 的半对角线是 11.0，游戏里不会露边；
    // 竖屏 / 超宽屏更长，四角会露出底色，所以超出时整体放大（缩小可见的世界范围）直到铺满。16:9 不受影响。
    const float MAX_RADIUS = 11.4f;
    float diag = std::sqrt(cb.ext[0] * cb.ext[0] + cb.ext[1] * cb.ext[1]);
    if (diag > MAX_RADIUS) { float k = MAX_RADIUS / diag; cb.ext[0] *= k; cb.ext[1] *= k; }
    cb.ang = p.ang; cb.farT = p.farT; cb.nearT = p.nearT;
    for (int i = 0; i < 3; i++) { cb.cFill[i] = p.cF[i]; cb.cFar[i] = p.cFar[i]; cb.cNear[i] = p.cNear[i]; cb.cGrad[i] = p.cG[i]; }
    for (int i = 0; i < 4; i++) cb.br[i] = p.br[i];
    // 场景几何（来自两个场景文件）：标题 = ui_menu_background_pause.scene，主菜单 = ui_menu_background_main.scene
    if (g_scene == SCENE_MENU) { cb.geo[0] = 1.0f; cb.geo[1] = 3.5f; cb.geo[2] = 3.0f; cb.geo[3] = -1.5f; cb.spot[2] = 1.0f; cb.spot[3] = 0.4f; }
    else                       { cb.geo[0] = 2.0f; cb.geo[1] = 3.0f; cb.geo[2] = 2.0f; cb.geo[3] = -0.5f; cb.spot[2] = 2.0f; cb.spot[3] = 1.0f; }
    cb.spot[0] = p.spot[0]; cb.spot[1] = p.spot[1];
    cb.tone[0] = g_tone == TONE_LIGHT ? 1.0f : 0.0f;
    D3D11_MAPPED_SUBRESOURCE m; g_ctx->Map(g_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m); memcpy(m.pData, &cb, sizeof(cb)); g_ctx->Unmap(g_cb.Get(), 0);

    D3D11_VIEWPORT vp = { 0, 0, (float)w, (float)h, 0, 1 };
    g_ctx->RSSetViewports(1, &vp);
    g_ctx->OMSetRenderTargets(1, &rtv, nullptr);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->IASetInputLayout(nullptr);
    g_ctx->VSSetShader(g_vs.Get(), nullptr, 0);
    g_ctx->PSSetShader(g_ps.Get(), nullptr, 0);
    ID3D11Buffer* cbs[] = { g_cb.Get() }; g_ctx->PSSetConstantBuffers(0, 1, cbs);
    ID3D11ShaderResourceView* srvs[] = { g_wallSrv.Get(), g_spotSrv.Get() }; g_ctx->PSSetShaderResources(0, 2, srvs);
    ID3D11SamplerState* sms[] = { g_sWall.Get(), g_sSpot.Get() }; g_ctx->PSSetSamplers(0, 2, sms);
    g_ctx->Draw(3, 0);
}

static void resizeSwap(UINT w, UINT h)
{
    if (!g_sc || w == 0 || h == 0) return;
    g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
    g_rtv.Reset();
    if (FAILED(g_sc->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) return;
    ComPtr<ID3D11Texture2D> bb; g_sc->GetBuffer(0, IID_PPV_ARGS(&bb));
    g_dev->CreateRenderTargetView(bb.Get(), nullptr, &g_rtv);
    g_w = w; g_h = h;
}

// ===================== 离屏导出（验证用） =====================
static bool dumpBmp()
{
    UINT W = g_cfg.dumpW, H = g_cfg.dumpH;
    D3D11_TEXTURE2D_DESC td = {}; td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> rt; if (FAILED(g_dev->CreateTexture2D(&td, nullptr, &rt))) return false;
    ComPtr<ID3D11RenderTargetView> rtv; g_dev->CreateRenderTargetView(rt.Get(), nullptr, &rtv);
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    double el = g_cfg.hasT ? g_cfg.freezeT : 0.0;
    Params p; computeParams(el, p);
    drawTo(rtv.Get(), W, H, p);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> st; g_dev->CreateTexture2D(&td, nullptr, &st);
    g_ctx->CopyResource(st.Get(), rt.Get());
    D3D11_MAPPED_SUBRESOURCE m; if (FAILED(g_ctx->Map(st.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    UINT rowBytes = ((W * 3 + 3) / 4) * 4;
    std::vector<uint8_t> out(rowBytes * H);
    for (UINT y = 0; y < H; y++)
    {
        const uint8_t* s = (const uint8_t*)m.pData + (size_t)y * m.RowPitch; uint8_t* d = out.data() + (size_t)y * rowBytes;
        for (UINT x = 0; x < W; x++) { d[x * 3 + 0] = s[x * 4 + 2]; d[x * 3 + 1] = s[x * 4 + 1]; d[x * 3 + 2] = s[x * 4 + 0]; }
    }
    g_ctx->Unmap(st.Get(), 0);
    BITMAPFILEHEADER fh = {}; BITMAPINFOHEADER ih = {};
    fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + (DWORD)out.size();
    ih.biSize = sizeof(ih); ih.biWidth = (LONG)W; ih.biHeight = -(LONG)H; ih.biPlanes = 1; ih.biBitCount = 24; ih.biSizeImage = (DWORD)out.size();
    FILE* f = nullptr; _wfopen_s(&f, g_cfg.dump.c_str(), L"wb"); if (!f) return false;
    fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f); fwrite(out.data(), 1, out.size(), f); fclose(f);
    return true;
}

// ===================== 壁纸模式 =====================
static BOOL CALLBACK enumTop(HWND top, LPARAM lp)
{
    if (FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr)) *(HWND*)lp = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
    return TRUE;
}

static void attachToDesktop(const RECT& screen)
{
    wchar_t buf[520] = {};
    if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, 520, buf, 0)) g_prevWallpaper = buf;
    HWND progman = FindWindowW(L"Progman", nullptr);
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, nullptr);
    HWND workerW = nullptr;
    EnumWindows(enumTop, (LPARAM)&workerW);                       // 旧版布局
    if (!workerW) workerW = FindWindowExW(progman, nullptr, L"WorkerW", nullptr);   // Windows 11 24H2 布局
    HWND parent = workerW ? workerW : progman;
    LONG_PTR style = GetWindowLongPtrW(g_hwnd, GWL_STYLE);
    SetWindowLongPtrW(g_hwnd, GWL_STYLE, (style & ~WS_POPUP) | WS_CHILD);
    SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    SetParent(g_hwnd, parent);
    POINT o = { screen.left, screen.top }; ScreenToClient(parent, &o);
    SetWindowPos(g_hwnd, nullptr, o.x, o.y, screen.right - screen.left, screen.bottom - screen.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

// 退出后让桌面恢复：单张图片壁纸 -> 重新设置同一张；幻灯片（路径为空）-> 只重绘，绝不传空路径
static void restoreDesktop()
{
    if (!g_prevWallpaper.empty()) SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (PVOID)g_prevWallpaper.c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    RedrawWindow(nullptr, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// 托盘图标由一个独立的隐藏顶层窗口持有：壁纸模式下主窗口是桌面层的子窗口，不能直接拥有通知图标；
// 顶层窗口还能收到资源管理器重启后的 TaskbarCreated 广播，从而重新创建图标。
static HWND g_tray;
static NOTIFYICONDATAW g_nid;
static UINT g_taskbarCreated;

// 场景选择：记住上次的选择（HKCU\Software\WolfAmongUsTitleLive\Scene，0=标题界面 1=主菜单）
static void saveScene()
{
    DWORD v = (DWORD)g_scene;
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\WolfAmongUsTitleLive", L"Scene", REG_DWORD, &v, sizeof(v));
}
static int loadScene()
{
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WolfAmongUsTitleLive", L"Scene", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS && v <= 1) return (int)v;
    return SCENE_TITLE;
}
static void setScene(int s) { g_scene = s; saveScene(); }

// 语言设置：记住上次的选择（HKCU\Software\WolfAmongUsTitleLive\Lang，0=自动 1=中文 2=English 3=日本語）
static int loadLang()
{
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WolfAmongUsTitleLive", L"Lang", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS && v < LP_COUNT) return (int)v;
    return LP_AUTO;
}
static void setLang(int l)
{
    g_langSetting = l; DWORD v = (DWORD)l;
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\WolfAmongUsTitleLive", L"Lang", REG_DWORD, &v, sizeof(v));
    if (g_hwnd) SetWindowTextW(g_hwnd, T(S_APP_TITLE));            // 已打开的窗口标题与托盘提示立即换语言
    if (g_nid.hWnd) { wcscpy_s(g_nid.szTip, T(S_TIP)); Shell_NotifyIconW(NIM_MODIFY, &g_nid); }
}

// 随系统自动启动：写入 HKCU\Software\Microsoft\Windows\CurrentVersion\Run（仅当前用户，不需要管理员权限）。
// 启动命令只带 exe 路径，所以开机后沿用上次在菜单里选的显示模式 / 场景 / 色调 / 速度 / 语言。
static const wchar_t* const RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* const RUN_VALUE = L"WolfAmongUsTitleLive";
static std::wstring selfCommand()
{
    wchar_t p[MAX_PATH * 2]; DWORD n = GetModuleFileNameW(nullptr, p, (DWORD)(sizeof(p) / sizeof(p[0])));
    return L"\"" + std::wstring(p, n) + L"\"";
}
static bool isAutoStart()
{
    wchar_t buf[1024]; DWORD sz = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_VALUE, RRF_RT_REG_SZ, nullptr, buf, &sz) != ERROR_SUCCESS) return false;
    return _wcsicmp(buf, selfCommand().c_str()) == 0;       // 路径不是当前这个 exe（比如 exe 被移动过）时视为未勾选
}
static void setAutoStart(bool on)
{
    if (on)
    {
        std::wstring c = selfCommand();
        RegSetKeyValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_VALUE, REG_SZ, c.c_str(), (DWORD)((c.size() + 1) * sizeof(wchar_t)));
    }
    else RegDeleteKeyValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_VALUE);
}

// 色调选择：记住上次的选择（HKCU\Software\WolfAmongUsTitleLive\Tone，0=原版 1=提亮 2=浅色）
static int loadTone()
{
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WolfAmongUsTitleLive", L"Tone", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS && v <= TONE_LIGHT) return (int)v;
    return TONE_ORIGINAL;
}
static void setTone(int t)
{
    g_tone = t; DWORD v = (DWORD)t;
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\WolfAmongUsTitleLive", L"Tone", REG_DWORD, &v, sizeof(v));
}

// 播放速度（全局倍率，托盘「播放速度」子菜单 / ↑↓ 键）；记住上次的选择
static const float SPEEDS[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 5.0f };
static const int NSPEED = (int)(sizeof(SPEEDS) / sizeof(SPEEDS[0]));
static const int SPEED_IDX_PAUSE = 0, SPEED_IDX_NORMAL = 4;       // SPEEDS 中 0 倍与 1 倍的下标；这两项的文字随语言变化
static const wchar_t* speedName(int i)
{
    static const wchar_t* const num[] = { L"", L"0.25×", L"0.5×", L"0.75×", L"", L"1.5×", L"2×", L"3×", L"5×" };
    if (i == SPEED_IDX_PAUSE) return T(S_SPEED_PAUSE);
    if (i == SPEED_IDX_NORMAL) return T(S_SPEED_NORMAL);
    return num[i];
}
static float g_speed = 1.0f;
static void setSpeed(float v)
{
    g_speed = v;
    DWORD x = (DWORD)(v * 100.0f + 0.5f);
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\WolfAmongUsTitleLive", L"Speed", REG_DWORD, &x, sizeof(x));
}
static float loadSpeed()
{
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WolfAmongUsTitleLive", L"Speed", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS && v <= 1000) return v / 100.0f;
    return 1.0f;
}
static int nearestSpeedIndex() { int b = 0; for (int i = 1; i < NSPEED; i++) if (std::fabs(SPEEDS[i] - g_speed) < std::fabs(SPEEDS[b] - g_speed)) b = i; return b; }
static void stepSpeed(int d) { int i = nearestSpeedIndex() + d; if (i < 0) i = 0; if (i >= NSPEED) i = NSPEED - 1; setSpeed(SPEEDS[i]); }

// ===================== 显示模式：全屏 / 窗口 / 背景 =====================
enum Mode { MODE_FULL, MODE_WINDOW, MODE_WALL };
static LONG_PTR g_winStyle = WS_OVERLAPPEDWINDOW;
static RECT g_winRect = { 0, 0, 0, 0 };                // 窗口模式的位置；空 = 首次，1280x720 居中

static void saveMode()
{
    DWORD v = g_wall ? MODE_WALL : g_full ? MODE_FULL : MODE_WINDOW;
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\WolfAmongUsTitleLive", L"Mode", REG_DWORD, &v, sizeof(v));
}
static int loadMode()
{
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WolfAmongUsTitleLive", L"Mode", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS && v <= MODE_WALL) return (int)v;
    return MODE_FULL;
}

// 按目标模式（新建/重建）主窗口；窗口销毁并重建，交换链随之重建。mon 为 null 时取主显示器。
static void rebuildWindow(int mode)
{
    HMONITOR mon = nullptr;
    if (g_hwnd && !g_wall)
    {
        LONG_PTR st = GetWindowLongPtrW(g_hwnd, GWL_STYLE);
        if (st & WS_CAPTION) { g_winStyle = st; GetWindowRect(g_hwnd, &g_winRect); }   // 记住窗口位置
        mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    }
    if (!mon) { POINT zero = { 0, 0 }; mon = MonitorFromPoint(zero, MONITOR_DEFAULTTOPRIMARY); }
    MONITORINFO mi = { sizeof(mi) }; GetMonitorInfoW(mon, &mi);
    RECT scr = mi.rcMonitor;

    if (g_hwnd)
    {
        if (g_ctx) { g_ctx->OMSetRenderTargets(0, nullptr, nullptr); g_ctx->Flush(); }
        g_rtv.Reset(); g_sc.Reset();
        bool wasWall = g_wall;
        g_switching = true; DestroyWindow(g_hwnd); g_switching = false; g_hwnd = nullptr;
        if (wasWall) restoreDesktop();
    }

    DWORD style = WS_POPUP, ex = 0; RECT r = scr;
    if (mode == MODE_WINDOW)
    {
        style = (DWORD)g_winStyle;
        if (g_winRect.right <= g_winRect.left)   // 首次：1280x720 居中
        {
            RECT adj = { 0, 0, 1280, 720 }; AdjustWindowRect(&adj, WS_OVERLAPPEDWINDOW, FALSE);
            int cx = scr.left + (scr.right - scr.left - 1280) / 2, cy = scr.top + (scr.bottom - scr.top - 720) / 2;
            g_winRect = { cx, cy, cx + (adj.right - adj.left), cy + (adj.bottom - adj.top) };
        }
        r = g_winRect;
    }
    else if (mode == MODE_FULL) ex = WS_EX_TOPMOST;
    g_hwnd = CreateWindowExW(ex, L"WolfAmongUsTitleLive", T(S_APP_TITLE), style, r.left, r.top, r.right - r.left, r.bottom - r.top, nullptr, nullptr, g_inst, nullptr);
    if (!g_hwnd) { fail(T(S_ERR_WINDOW), GetLastError()); ExitProcess(1); }
    g_wall = (mode == MODE_WALL); g_full = (mode == MODE_FULL);
    if (g_wall) attachToDesktop(scr);
    else { ShowWindow(g_hwnd, SW_SHOW); SetForegroundWindow(g_hwnd); }
    RECT rc; GetClientRect(g_hwnd, &rc); g_w = rc.right - rc.left; g_h = rc.bottom - rc.top;
    if (g_dev && !createSwap()) ExitProcess(1);
}

static void setMode(int mode)
{
    if (mode == (g_wall ? MODE_WALL : g_full ? MODE_FULL : MODE_WINDOW)) return;
    rebuildWindow(mode);
    saveMode();
}

static void toggleFullscreen(HWND h)
{
    if (g_wall) return;
    setMode(g_full ? MODE_WINDOW : MODE_FULL);
}

#if 0   // 旧的原地切换实现（已由 rebuildWindow 取代）
static void toggleFullscreenInPlace(HWND h)
{
    if (!g_full)
    {
        LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE);
        if (st & WS_CAPTION) { g_winStyle = st; GetWindowRect(h, &g_winRect); }   // 记住窗口状态，切回时恢复
        MONITORINFO mi = { sizeof(mi) }; GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrW(h, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(h, HWND_TOPMOST, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_full = true;
    }
    else
    {
        SetWindowLongPtrW(h, GWL_STYLE, g_winStyle | WS_VISIBLE);
        SetWindowPos(h, HWND_NOTOPMOST, g_winRect.left, g_winRect.top, g_winRect.right - g_winRect.left, g_winRect.bottom - g_winRect.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_full = false;
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    }
}
#endif

// 右键菜单：可勾选的场景、播放速度子菜单、可勾选的显示模式（全屏/窗口/背景）、退出。托盘图标与窗口模式下的窗口共用。
static void showMenu(HWND h)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_scene == SCENE_TITLE ? MF_CHECKED : 0), ID_TITLE, T(S_SCENE_TITLE));
    AppendMenuW(menu, MF_STRING | (g_scene == SCENE_MENU ? MF_CHECKED : 0), ID_MENU, T(S_SCENE_MENU));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    HMENU sp = CreatePopupMenu();                       // 播放速度子菜单（销毁主菜单时一并销毁）
    int cur = nearestSpeedIndex();
    for (int i = 0; i < NSPEED; i++) AppendMenuW(sp, MF_STRING | (i == cur && std::fabs(SPEEDS[i] - g_speed) < 0.005f ? MF_CHECKED : 0), ID_SPEED0 + i, speedName(i));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)sp, T(S_SPEED));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g_tone == TONE_ORIGINAL ? MF_CHECKED : 0), ID_TONE0, T(S_TONE_ORIG));
    AppendMenuW(menu, MF_STRING | (g_tone == TONE_BRIGHT ? MF_CHECKED : 0), ID_TONE1, T(S_TONE_BRIGHT));
    AppendMenuW(menu, MF_STRING | (g_tone == TONE_LIGHT ? MF_CHECKED : 0), ID_TONE2, T(S_TONE_LIGHT));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (!g_wall && g_full ? MF_CHECKED : 0), ID_FULL, T(S_FULL));
    AppendMenuW(menu, MF_STRING | (!g_wall && !g_full ? MF_CHECKED : 0), ID_WIN, T(S_WIN));
    AppendMenuW(menu, MF_STRING | (g_wall ? MF_CHECKED : 0), ID_WALL, T(S_WALL));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    HMENU lm = CreatePopupMenu();                       // 语言子菜单：自动 / 各语言用自己的名字显示
    static const wchar_t* const langNames[LP_COUNT] = { nullptr, L"简体中文", L"English", L"日本語" };
    for (int i = 0; i < LP_COUNT; i++) AppendMenuW(lm, MF_STRING | (i == g_langSetting ? MF_CHECKED : 0), ID_LANG0 + i, i == LP_AUTO ? T(S_LANG_AUTO) : langNames[i]);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)lm, T(S_LANG));
    AppendMenuW(menu, MF_STRING | (isAutoStart() ? MF_CHECKED : 0), ID_AUTOSTART, T(S_AUTOSTART));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, T(S_EXIT));
    POINT pt; GetCursorPos(&pt); SetForegroundWindow(h);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, nullptr); DestroyMenu(menu);
    PostMessageW(h, WM_NULL, 0, 0);
}

static void command(UINT id)
{
    if (id == ID_TITLE) setScene(SCENE_TITLE);
    else if (id == ID_MENU) setScene(SCENE_MENU);
    else if (id >= ID_SPEED0 && id < ID_SPEED0 + (UINT)NSPEED) setSpeed(SPEEDS[id - ID_SPEED0]);
    else if (id == ID_TONE0) setTone(TONE_ORIGINAL);
    else if (id == ID_TONE1) setTone(TONE_BRIGHT);
    else if (id == ID_TONE2) setTone(TONE_LIGHT);
    else if (id == ID_FULL) setMode(MODE_FULL);
    else if (id == ID_WIN) setMode(MODE_WINDOW);
    else if (id == ID_WALL) setMode(MODE_WALL);
    else if (id >= ID_LANG0 && id < ID_LANG0 + (UINT)LP_COUNT) setLang((int)(id - ID_LANG0));
    else if (id == ID_AUTOSTART) setAutoStart(!isAutoStart());
    else if (id == ID_EXIT) DestroyWindow(g_hwnd);
}

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_KEYDOWN:
        if (w == VK_ESCAPE && !g_wall) DestroyWindow(h);
        else if (w == VK_TAB) setScene(g_scene == SCENE_TITLE ? SCENE_MENU : SCENE_TITLE);   // Tab 在两个场景间切换
        else if (w == 'T') setScene(SCENE_TITLE);
        else if (w == 'M') setScene(SCENE_MENU);
        else if (w == 'F' || w == VK_F11) toggleFullscreen(h);
        else if (w == VK_UP) stepSpeed(+1);          // ↑ 加速
        else if (w == VK_DOWN) stepSpeed(-1);        // ↓ 减速
        return 0;
    case WM_SYSKEYDOWN: if (w == VK_RETURN) { toggleFullscreen(h); return 0; } break;   // Alt+Enter
    case WM_LBUTTONDBLCLK: toggleFullscreen(h); return 0;
    case WM_SIZE: if (w != SIZE_MINIMIZED) resizeSwap(LOWORD(l), HIWORD(l)); return 0;
    case WM_SETCURSOR: if (g_full && !g_wall) { SetCursor(nullptr); return TRUE; } break;
    case WM_ERASEBKGND: return 1;
    case WM_RBUTTONUP: if (!g_wall && !g_full) showMenu(h); return 0;
    case WM_COMMAND: command(LOWORD(w)); return 0;
    case WM_DESTROY: if (!g_switching) PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---- 托盘（所有模式都创建）----
static bool addTrayIcon()
{
    g_nid = {}; g_nid.cbSize = sizeof(g_nid); g_nid.hWnd = g_tray; g_nid.uID = 1; g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY; g_nid.hIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(IDI_ICON1));
    wcscpy_s(g_nid.szTip, T(S_TIP));
    bool ok = Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
    if (ok) { g_nid.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &g_nid); }
    return ok;
}

static LRESULT CALLBACK trayProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_TRAY)
    {
        UINT ev = LOWORD(l);
        if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU || ev == WM_LBUTTONUP) showMenu(h);
        return 0;
    }
    if (m == WM_COMMAND) { command(LOWORD(w)); return 0; }
    if (g_taskbarCreated && m == g_taskbarCreated) { addTrayIcon(); return 0; }      // 资源管理器重启后重新创建图标
    return DefWindowProcW(h, m, w, l);
}

static bool createTray(HINSTANCE inst)
{
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSEXW tc = { sizeof(tc) }; tc.lpfnWndProc = trayProc; tc.hInstance = inst; tc.lpszClassName = L"WolfAmongUsTitleLiveTray";
    RegisterClassExW(&tc);
    // 隐藏的顶层窗口（不是消息窗口：消息窗口收不到 TaskbarCreated 广播）
    g_tray = CreateWindowExW(WS_EX_TOOLWINDOW, tc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    return g_tray && addTrayIcon();
}

static void parseArgs()
{
    int argc; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; i++)
    {
        std::wstring a = argv[i];
        auto next = [&]() -> std::wstring { return i + 1 < argc ? argv[++i] : L""; };
        if (a == L"--window") g_cfg.window = true;
        else if (a == L"--wallpaper") g_cfg.wallpaper = true;
        else if (a == L"--seed") { g_cfg.hasSeed = true; g_cfg.seed = (unsigned)_wtoi(next().c_str()); }
        else if (a == L"--speed") { g_cfg.hasSpeed = true; g_cfg.speed = (float)_wtof(next().c_str()); }
        else if (a == L"--t") { g_cfg.hasT = true; g_cfg.freezeT = (float)_wtof(next().c_str()); }
        else if (a == L"--scene") { std::wstring s = next(); g_cfg.hasScene = true; g_cfg.scene = (s == L"menu" || s == L"main") ? SCENE_MENU : SCENE_TITLE; }
        else if (a == L"--lang") { std::wstring s = next(); g_cfg.hasLang = true; g_cfg.lang = (s == L"zh" || s == L"1") ? LP_ZH : (s == L"en" || s == L"2") ? LP_EN : (s == L"ja" || s == L"3") ? LP_JA : LP_AUTO; }
        else if (a == L"--tone") { std::wstring s = next(); g_cfg.hasTone = true; g_cfg.tone = (s == L"light" || s == L"2") ? 2 : (s == L"bright" || s == L"1") ? 1 : 0; }
        else if (a == L"--log") g_cfg.log = next();     // 启动状态写入文件（调试用）
        else if (a == L"--dump") g_cfg.dump = next();
        else if (a == L"--size") { std::wstring s = next(); swscanf_s(s.c_str(), L"%dx%d", &g_cfg.dumpW, &g_cfg.dumpH); }
        else if (a == L"--dbg")
        {
            std::wstring s = next(); float v[15] = {}; int k = 0; size_t pos = 0;
            while (k < 15 && pos < s.size()) { v[k++] = (float)_wtof(s.c_str() + pos); pos = s.find(L',', pos); if (pos == std::wstring::npos) break; pos++; }
            g_cfg.hasDbg = true; Params& p = g_cfg.dbg;
            p.br[0] = p.br[1] = p.br[2] = p.br[3] = 1.0f; p.spot[0] = -0.14144018f; p.spot[1] = -0.052455753f;
            p.ang = v[0]; p.farT = v[1]; p.nearT = v[2];
            for (int j = 0; j < 3; j++) { p.cF[j] = v[3 + j]; p.cFar[j] = v[6 + j]; p.cNear[j] = v[9 + j]; p.cG[j] = v[12 + j]; }
        }
    }
    LocalFree(argv);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
    g_inst = inst;
    parseArgs();
    // 每个动画各取速度系数 0.85~1.15 与随机起始位置
    std::mt19937 rng(g_cfg.hasSeed ? g_cfg.seed : (unsigned)GetTickCount64() ^ (unsigned)GetCurrentProcessId());
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    auto mk = [&](float len) { Clock c; c.len = len; c.speed = 0.85f + 0.30f * U(rng); c.phase = len * U(rng); return c; };
    g_rot = mk(ROT_LEN); g_near = mk(NEAR_LEN); g_far = mk(FAR_LEN); g_col = mk(COLOR_LEN); g_bri = mk(BRI_LEN);
    g_scene = g_cfg.hasScene ? g_cfg.scene : loadScene();
    g_speed = g_cfg.hasSpeed ? g_cfg.speed : loadSpeed();
    g_tone = g_cfg.hasTone ? g_cfg.tone : loadTone();
    g_langSetting = g_cfg.hasLang ? g_cfg.lang : loadLang();

    if (!g_cfg.dump.empty())
    {
        if (!initGfx(true)) return 1;
        return dumpBmp() ? 0 : 2;
    }

    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"WolfAmongUsTitleLive.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    WNDCLASSEXW wc = { sizeof(wc) }; wc.style = CS_DBLCLKS; wc.lpfnWndProc = wndProc; wc.hInstance = inst; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_ICON1)); wc.lpszClassName = L"WolfAmongUsTitleLive";
    RegisterClassExW(&wc);

    // 启动模式：命令行 --window / --wallpaper 优先，否则沿用上次在菜单里选的（默认全屏）
    int startMode = g_cfg.wallpaper ? MODE_WALL : g_cfg.window ? MODE_WINDOW : loadMode();
    rebuildWindow(startMode);                              // 此时还没有 D3D 设备，只建窗口
    if (!initGfx(false)) return 1;
    bool trayOk = createTray(inst);
    if (!g_cfg.log.empty()) { FILE* lf = nullptr; _wfopen_s(&lf, g_cfg.log.c_str(), L"w"); if (lf) { fprintf(lf, "tray=%d full=%d wallpaper=%d scene=%d\n", trayOk, g_full, g_wall, g_scene); fclose(lf); } }

    QueryPerformanceFrequency(&g_qpf); QueryPerformanceCounter(&g_t0);
    LARGE_INTEGER last = g_t0; double anim = 0.0;          // 动画时间：每帧按当前播放速度累加，调速不会让画面跳变
    MSG msg = {};
    for (;;)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) goto done; TranslateMessage(&msg); DispatchMessageW(&msg); }
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        double dt = (double)(now.QuadPart - last.QuadPart) / (double)g_qpf.QuadPart; last = now;
        if (dt > 0.25) dt = 0.25;                           // 睡眠/卡顿后恢复时不要一下跳很远
        anim += dt * g_speed;
        double el = g_cfg.hasT ? g_cfg.freezeT : anim;
        Params p; computeParams(el, p);
        if (g_rtv)
        {
            drawTo(g_rtv.Get(), g_w, g_h, p);
            g_sc->Present(1, 0);
        }
        else Sleep(50);
    }
done:
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_tray) DestroyWindow(g_tray);
    if (g_wall) restoreDesktop();
    CloseHandle(mtx);
    return 0;
}

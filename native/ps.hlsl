// 《The Wolf Among Us》重制版 标题界面背景 (ui_menu_background_pause.scene) 的像素着色器。
// 逻辑来源与验证见 main.cpp 顶部说明；各图层按引擎绘制顺序在 8 位 UNORM 渲染目标上混合，层间取整。
cbuffer C : register(b0)
{
    float2 uRes;   float2 uExt;          // 渲染目标像素尺寸；屏幕覆盖的世界范围的一半 (x, y)
    float  uAng;   float  uFarT;  float uNearT; float pad0;
    float4 uCFill; float4 uCFar;  float4 uCNear; float4 uCGrad;
    float4 uBr;                            // 材质色亮度倍率 (PS cb3_v1)：fill, far, near, gradient
    float4 uGeo;                           // fill 缩放, far UV 图块数, spotlight 缩放, gradient 位置 y
    float4 uSpot;                          // spotlight 位置 xy, gradient 缩放 xy
    float4 uTone;                          // x = 1：浅色色调（白/亮灰底）；0 = 游戏原样
};
Texture2D tWall : register(t0);                    // BC3_UNORM 2048x2048，alpha = 花卉图案
Texture2D tSpot : register(t1);                    // BC1_UNORM 512x512
SamplerState sWall : register(s0);                 // 双线性, Wrap
SamplerState sSpot : register(s1);                 // 双线性, ClampEdge

float3 q8(float3 x) { return floor(saturate(x) * 255.0 + 0.5) / 255.0; }
bool inside(float2 v) { return v.x >= 0.0 && v.x <= 1.0 && v.y >= 0.0 && v.y <= 1.0; }

// 墙纸层：网格 5.12x5.12，整体缩放 4.5，绕原点旋转；UV = 缩放*(u,v) + 位移（位移最后加）；混合 SrcAlpha, InvSrcAlpha
float3 wallLayer(float3 dst, float2 w, float cs, float sn, float tile, float tr, float alphaK, float3 col)
{
    float2 loc = float2(cs * w.x + sn * w.y, -sn * w.x + cs * w.y) / 4.5;
    float2 v = (loc + 2.56) / 5.12;
    if (!inside(v)) return dst;
    float2 uv = float2(v.x, 1.0 - v.y) * tile + tr;
    float4 t = tWall.SampleLevel(sWall, uv, 0);
    float a = saturate(t.a) * alphaK;
    return q8(dst * (1.0 - a) + t.rgb * col * a);
}

float4 main(float4 pos : SV_Position) : SV_Target
{
    float2 ndc = float2(pos.x / uRes.x * 2.0 - 1.0, 1.0 - pos.y / uRes.y * 2.0);
    float2 w = ndc * uExt;                         // 世界坐标 (z=0)
    float cs = cos(uAng), sn = sin(uAng);
    float3 c = 0.0;

    if (uTone.x > 0.5)
    {
        // 浅色色调：几何、旋转、滚动、图案与游戏一致；底色换成近白的亮灰，花卉改为淡冷色（灰蓝），
        // 花卉颜色随游戏的 20 秒颜色动画做很小幅度的偏移；聚光暗角与底部渐变改为"白色"版本。
        float3 hA = uCFar.rgb  / max(max(uCFar.r,  max(uCFar.g,  uCFar.b)),  1e-4);
        float3 hN = uCNear.rgb / max(max(uCNear.r, max(uCNear.g, uCNear.b)), 1e-4);
        float3 dA = hA - dot(hA, 1.0 / 3.0), dN = hN - dot(hN, 1.0 / 3.0);
        float3 base = float3(0.968, 0.970, 0.976);
        c = q8(base);
        c = wallLayer(c, w, cs, sn, 6.0, 0.0, 0.09, 0.0);                                     // 花卉的淡灰阴影
        c = wallLayer(c, w, cs, sn, uGeo.y, uFarT, 0.55, float3(0.70, 0.77, 0.88) + dA * 0.07);   // 远景：浅灰蓝
        c = wallLayer(c, w, cs, sn, 2.0, uNearT, 0.65, float3(0.52, 0.62, 0.79) + dN * 0.07);     // 近景：稍深的灰蓝
        // 聚光：游戏里是四周压暗的暗角；这里改为四周泛白
        float2 sv2 = ((w - uSpot.xy) / uGeo.z + 10.24) / 20.48;
        if (inside(sv2))
        {
            float2 d2 = (sv2 - 0.5) * 2.0;
            float3 s2 = tSpot.SampleLevel(sSpot, (1.0 - abs(d2)) * 0.991285 + 0.0043573, 0).rgb;
            float3 k2 = saturate(s2 * 1.3);     // 系数 >1：泛白范围收窄，中心保留更多原色
            c = q8(lerp(1.0, c, k2 * k2));      // 边缘贴近纯白，向中心渐隐
        }
        // 渐变：游戏里是自下而上 1→0 的加色光；这里是同样位置、同样衰减的白色柔光
        float2 gv2 = (float2(w.x / uSpot.z, (w.y - uGeo.w) / uSpot.w) + 10.0) / 20.0;
        if (gv2.y <= 1.0) c = q8(lerp(c, 1.0, 0.62 * saturate(1.0 - gv2.y)));   // 同上：向下、左右延续
        return float4(c, 1.0);
    }

    // 层 0 fill：纯色 × 亮度倍率。游戏里它是 19.2x10.8（乘缩放 uGeo.x：标题 2 / 主菜单 1）的矩形，正好盖住 16:9 的屏幕；
    // 竖屏 / 超宽屏会超出矩形，这里让它铺满整个屏幕（16:9 下结果相同）
    c = q8(uCFill.rgb * uBr.x);
    // 层 1 back：黑色，常量透明度 0.5，UV 缩放 6
    c = wallLayer(c, w, cs, sn, 6.0, 0.0, 0.5, 0.0);
    // 层 2 far：UV 缩放 uGeo.y（标题 3 / 主菜单 3.5）
    c = wallLayer(c, w, cs, sn, uGeo.y, uFarT, 1.0, uCFar.rgb * uBr.y);
    // 层 3 near：UV 缩放 2
    c = wallLayer(c, w, cs, sn, 2.0, uNearT, 1.0, uCNear.rgb * uBr.z);

    // 层 4 spotlight：20.48 方形，缩放 uGeo.z，位于 uSpot.xy，混合 DstCol*Zero；
    //   网格为中心扇形，UV = (1-|dx|, 1-|dy|)，再经 (0.0043573, 0.991285) 的 UV 变换
    float2 sv = ((w - uSpot.xy) / uGeo.z + 10.24) / 20.48;
    if (inside(sv))
    {
        float2 d = (sv - 0.5) * 2.0;
        float2 suv = (1.0 - abs(d)) * 0.991285 + 0.0043573;
        c = q8(c * tSpot.SampleLevel(sSpot, suv, 0).rgb);
    }

    // 层 5 gradient：20x20 网格，缩放 uSpot.zw（标题 (2,1) / 主菜单 (1,0.4)），位置 y=uGeo.w；
    //   顶点 alpha 自下而上 1→0；混合 SrcAlpha, One
    //   游戏里网格的下缘和左右缘在 16:9 屏幕之外；竖屏 / 超宽屏放大后会露出来，所以向下和左右延续（alpha 保持 1），16:9 下结果相同
    float2 gv = (float2(w.x / uSpot.z, (w.y - uGeo.w) / uSpot.w) + 10.0) / 20.0;
    if (gv.y <= 1.0) c = q8(c + uCGrad.rgb * uBr.w * saturate(1.0 - gv.y));

    return float4(c, 1.0);
}

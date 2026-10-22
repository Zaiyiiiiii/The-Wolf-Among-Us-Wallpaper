"""按 Ghidra 反编译读到的引擎代码逐字实现 KeyframedValue 求值（Color / float）。
切线模式: 1=Stepped 2=Knot 3=Smooth 4=Flat。样条为 Catmull-Rom 基 (0.5, 1.5, 2.5, -0.5)。
"""
import numpy as np

STEPPED, KNOT, SMOOTH, FLAT = 1, 2, 3, 4


def eval_kfv(samples, t):
    """samples: [(time, mode, value(np.array))]，按时间升序；t 为曲线内时间。返回 np.array。"""
    n = len(samples)
    if n == 0:
        return None
    if t < samples[0][0] or n == 1:
        return np.array(samples[0][2], float)
    if not t < samples[-1][0]:
        return np.array(samples[-1][2], float)
    lo, hi = 0, n - 1                       # 二分：samples[lo].time <= t < samples[hi].time
    while hi - lo > 1:
        mid = (lo + hi) >> 1
        if t < samples[mid][0]:
            hi = mid
        else:
            lo = mid
    t0, m0, b = samples[lo]
    t1, m1, c = samples[hi]
    b = np.array(b, float); c = np.array(c, float)
    if m1 in (STEPPED, KNOT) and m0 == KNOT:      # 线性
        x = (t - t0) / (t1 - t0)
        return (c - b) * x + b
    if m0 == STEPPED:
        return b
    x = (t - t0) / (t1 - t0)
    # a：起点一侧邻居
    if m0 == FLAT:
        a = c
    elif m0 == SMOOTH:
        a = np.array(samples[lo - 1][2], float) if lo - 1 >= 0 else b
    else:
        a = 2 * b - c
    # d：终点一侧邻居
    if m1 == FLAT:
        d = b
    elif m1 == SMOOTH:
        d = np.array(samples[hi + 1][2], float) if hi + 1 < n else c
    else:
        d = 2 * c - b
    x = min(max(x, 0.0), 1.0)
    c3 = 1.5 * b - 0.5 * a - 1.5 * c + 0.5 * d
    c2 = a - 2.5 * b + 2 * c - 0.5 * d
    c1 = -0.5 * a + 0.5 * c
    return ((c3 * x + c2) * x + c1) * x + b


def slerp(q1, q2, t):
    q1 = np.array(q1, float); q2 = np.array(q2, float)
    dot = float(q1 @ q2)
    if dot < 0:
        q2 = -q2; dot = -dot
    s1 = 1.0 - t; s2 = t
    if dot < 0.999999:
        th = np.arccos(dot); st = np.sin(th)
        s1 = np.sin((1.0 - t) * th) / st
        s2 = np.sin(t * th) / st
    return q1 * s1 + q2 * s2

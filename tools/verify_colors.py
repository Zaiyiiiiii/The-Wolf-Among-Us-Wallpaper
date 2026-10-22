import json, sys, os
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from engine_kfv import eval_kfv, FLAT, SMOOTH

import wolf_env as env
rows = json.load(open(env.path('WOLF_DENSE_ROWS')))
NS = int(sys.argv[1]) if len(sys.argv) > 1 else 9
rows = rows[:NS]
w = np.array([r['wall'] for r in rows]); w -= w[0]

# 来自 Boot 归档 ui_menu_background_color.chore 的原始关键帧（时间, 切线模式, RGB）
KEYS = {
    'far':  [(0, FLAT, (0.2, 0.051, 0.098)), (5, FLAT, (0.2, 0.078, 0.057)), (15, FLAT, (0.2, 0.051, 0.188)), (20, FLAT, (0.2, 0.051, 0.098))],
    'near': [(0, FLAT, (0.2, 0.051, 0.2)), (5, FLAT, (0.2, 0.051, 0.125)), (15, FLAT, (0.11, 0.051, 0.2)), (20, FLAT, (0.2, 0.051, 0.2))],
    'fill': [(0, SMOOTH, (0.102, 0.024, 0.051)), (5, SMOOTH, (0.102, 0.035, 0.024)), (15, SMOOTH, (0.102, 0.024, 0.098)), (20, SMOOTH, (0.102, 0.024, 0.051))],
    'grad': [(0, SMOOTH, (0.024, 0.0, 0.149)), (5, SMOOTH, (0.098, 0.0, 0.149)), (15, SMOOTH, (0.0, 0.067, 0.149)), (19.980469, SMOOTH, (0.024, 0.0, 0.149))],
}
obs = {n: np.array([r['col'][n] for r in rows]) for n in KEYS}
LEN = 20.0


def err(k, ph, names=KEYS):
    e = 0.0
    for n in names:
        for i, t in enumerate(w):
            tc = (ph + k * t) % LEN
            e += float(((eval_kfv(KEYS[n], tc) - obs[n][i]) ** 2).sum())
    return e


best = (1e9, None, None)
for k in np.arange(0.5, 1.5, 0.01):
    for ph in np.arange(0, LEN, 0.1):
        e = err(k, ph)
        if e < best[0]:
            best = (e, k, ph)
print('粗搜 最佳', best)
e0, k0, p0 = best
best = (e0, k0, p0)
for k in np.arange(k0 - 0.02, k0 + 0.02, 0.0005):
    for ph in np.arange(p0 - 0.2, p0 + 0.2, 0.005):
        e = err(k, ph % LEN)
        if e < best[0]:
            best = (e, k, ph % LEN)
print('精搜 最佳 误差平方和=%.3e  速度系数=%.5f  相位=%.4f' % best)
e, k, ph = best
for n in KEYS:
    errs = []
    for i, t in enumerate(w):
        pred = eval_kfv(KEYS[n], (ph + k * t) % LEN)
        errs.append(np.abs(pred - obs[n][i]).max())
    print('%-5s 逐帧最大绝对误差: max=%.2e  平均=%.2e' % (n, max(errs), np.mean(errs)))

import json, math, sys
import wolf_env as env
data = json.load(open(env.path('WOLF_MENU_JSON'), encoding='utf-8'))
which = int(sys.argv[1]) if len(sys.argv) > 1 else 0
r = data[which]
print(r['file'], 'ndraws', r['ndraws'], 'event', r.get('event') and r['event'].get('wall'))
for i, d in enumerate(r['draws']):
    g = lambda blk, key: d.get(blk, {}).get(key)
    rows = [g('VS2', 'cb2_v0'), g('VS2', 'cb2_v1')]
    ang = math.degrees(math.atan2(rows[1][0], rows[0][0])) if rows[0] and rows[1] else None
    print('[%d] idx=%s blend=%s srv=%s' % (i, d.get('indices'), d.get('blend'), d.get('srv')))
    print('     世界矩阵 v0=%s v1=%s v6(scale)=%s  角度=%s' % (rows[0], rows[1], g('VS2', 'cb2_v6'), None if ang is None else round(ang, 3)))
    print('     uv网格 cb3_v1=%s size cb3_v5=%s org cb3_v6=%s | UV矩阵 cb4_v0=%s 位移 cb4_v1=%s' % (g('VS3', 'cb3_v1'), g('VS3', 'cb3_v5'), g('VS3', 'cb3_v6'), g('VS4', 'cb4_v0'), g('VS4', 'cb4_v1')))
    print('     PS: alpha cb2_v6=%s 漫反射 cb2_v7=%s cb3_v0=%s cb3_v1=%s | cb0_v7=%s' % (g('PS2', 'cb2_v6'), g('PS2', 'cb2_v7'), g('PS3', 'cb3_v0'), g('PS3', 'cb3_v1'), g('PS0', 'cb0_v7')))
    print('     环境光 VS cb5_v5=%s  fog/tint PS cb1_v0=%s cb1_v1=%s cb1_v29=%s' % (g('VS5', 'cb5_v5'), g('PS1', 'cb1_v0'), g('PS1', 'cb1_v1'), g('PS1', 'cb1_v29')))

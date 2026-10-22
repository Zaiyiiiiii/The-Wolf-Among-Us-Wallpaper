import re, sys, glob, os
import wolf_env as env

OUT = str(env.path('WOLF_CAPTURE_DIR'))
NAMES = ['fill', 'back', 'far', 'near', 'spotlight', 'gradient']


def parse(path):
    t = open(path, encoding='utf-8').read()
    draws = re.split(r'={50,}\nDRAW #', t)[1:]
    res = []
    for d in draws[:6]:
        info = {}
        info['hdr'] = d.splitlines()[0]
        parts = re.split(r'--- (VS|PS) shader=', d)
        # parts: [pre, 'VS', body, 'PS', body]
        for i in range(1, len(parts), 2):
            stage, body = parts[i], parts[i + 1]
            for m in re.finditer(r'(cb\d+_v\d+) VarType\.Float 1x4 = \[([^\]]*)\]', body):
                info[stage + ':' + m.group(1)] = [float(x) for x in m.group(2).split(',')]
        info['blend'] = re.search(r'--- BLEND enabled=(\w+) color\(src=BlendMultiplier\.(\w+) dst=BlendMultiplier\.(\w+)', d).groups()
        info['srv'] = re.findall(r'PS_SRV\[\d+\] id=\S+ (\d+x\d+) mips=\d+ arr=\d+ fmt=(\w+) viewFmt=(\w+)', d)
        info['smp'] = re.findall(r'PS_SMP\[\d+\] filter=\S+ \S+ \S+ \S+ addrU=AddressMode\.(\w+) addrV=AddressMode\.(\w+)', d)
        res.append(info)
    return res


def fmt(v):
    return '(' + ', '.join('%.6g' % x for x in v) + ')' if v else '-'


files = sorted(glob.glob(os.path.join(OUT, 'auto_frame*_draws.txt')), key=lambda p: int(re.search(r'frame(\d+)', p).group(1)))
for p in files:
    print('#' * 30, os.path.basename(p))
    for i, info in enumerate(parse(p)):
        print('[%d %s] %s blend=%s srv=%s smp=%s' % (i, NAMES[i], info['hdr'].split(' ', 2)[2][:50], info['blend'], info['srv'], info['smp']))
        print('    VS 世界矩阵 cb2 v0..v2:', fmt(info.get('VS:cb2_v0')), fmt(info.get('VS:cb2_v1')), fmt(info.get('VS:cb2_v2')), '| v3..v5:', fmt(info.get('VS:cb2_v3')), fmt(info.get('VS:cb2_v4')), fmt(info.get('VS:cb2_v5')), '| v6(scale)', fmt(info.get('VS:cb2_v6')))
        print('    VS 网格 cb3 v1(uv 偏移xy/缩放zw)', fmt(info.get('VS:cb3_v1')), ' v5', fmt(info.get('VS:cb3_v5')), ' v6', fmt(info.get('VS:cb3_v6')), ' v7', fmt(info.get('VS:cb3_v7')))
        print('    VS UV 变换 cb4 v0(矩阵)', fmt(info.get('VS:cb4_v0')), ' v1(位移)', fmt(info.get('VS:cb4_v1')))
        print('    PS 材质 cb3 v0', fmt(info.get('PS:cb3_v0')), ' v1', fmt(info.get('PS:cb3_v1')), '| cb2 v6(alpha)', fmt(info.get('PS:cb2_v6')), ' v7(漫反射色)', fmt(info.get('PS:cb2_v7')), '| cb0 v7(亮度)', fmt(info.get('PS:cb0_v7')))

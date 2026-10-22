import re, sys

# 用法: chore_summary.py <chore 转储文本> ：列出每个资源的动画曲线关键帧
t = open(sys.argv[1], encoding='utf-8').read()
print('Length =', re.search(r'\n  Length: (\S+)', t).group(1))
for ag in re.finditer(r'AgentName: (.*)\n      Resources: List`1 count=\d+\n((?:        \[\d+\] = \d+\n)*)', t):
    print('Agent', ag.group(1), 'resources', re.findall(r'= (\d+)', ag.group(2)))
res = re.split(r'\n    \[(\d+)\] = ChoreResource\n', t)
for i in range(1, len(res), 2):
    idx, body = res[i], res[i + 1]
    rn = re.search(r'ResNameSymbol: (.*)', body).group(1)
    obh = re.search(r'ObjectHandle: HandleBase\(Handle: Symbol: (.*)\)', body)
    print('\n== Resource', idx, rn, '| IsAgentResource', re.search(r'IsAgentResource: (\S+)', body).group(1), '| handle', obh.group(1) if obh else '')
    vals = re.split(r'\n          \[(\d+)\] = KeyframedValue`1\n', body)
    for j in range(1, len(vals), 2):
        vb = vals[j + 1]
        name = re.search(r'NameS: (.*)', vb)
        mn = re.search(r'MinVal: (.*)', vb); mx = re.search(r'MaxVal: (.*)', vb)
        samples = re.split(r'\n              \[(\d+)\] = Sample\n', vb)
        line = []
        for k in range(1, len(samples), 2):
            sb = samples[k + 1]
            time = re.search(r'Time: (\S+)', sb).group(1)
            vm = re.search(r'\n                Value: ([^\n]*)\n((?:                  [^\n]*\n)*)', sb)
            v = vm.group(1)
            if vm.group(2):
                flat = re.findall(r'([A-Za-z]+): ([-0-9.E+]+)', vm.group(2))
                v = ' '.join('%s=%s' % kv for kv in flat if kv[0] != 'Version')
            tm = re.search(r'TangentMode: (\S+)', sb)
            line.append('t=%s -> %s [%s]' % (time, v, tm.group(1) if tm else ''))
        print('  value[%s] %s min=%s max=%s samples=%d' % (vals[j], name.group(1) if name else '', mn.group(1) if mn else '', mx.group(1) if mx else '', len(line)))
        for l in line:
            print('       ', l)

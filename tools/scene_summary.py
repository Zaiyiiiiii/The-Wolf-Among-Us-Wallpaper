import re, sys

# 用法: scene_summary.py <scene 转储文本> ：每个 Agent 一行，列出渲染相关属性
t = open(sys.argv[1], encoding='utf-8').read()
chunks = re.split(r'\n    \[\d+\] = AgentInfo\n', t)[1:]
for c in chunks:
    name = re.search(r'AgentName: (.*)', c).group(1)
    out = []
    for m in re.finditer(r'\n          \[([^\]]+)\] = PropertyEntry\n            Value: ([^\n]*)\n((?:(?!            MetaType)[^\n]*\n)*)', c):
        key, val, rest = m.group(1), m.group(2), m.group(3)
        if key in ('Scene: Start Visibility', 'Scene: Transient'):
            continue
        if val.startswith('Color'):
            v = dict(re.findall(r'([RGBA]): (\S+)', rest))
            val = 'rgba(%s, %s, %s, %s)' % (v['R'], v['G'], v['B'], v['A'])
        elif val.startswith('Vector3'):
            v = dict(re.findall(r'([XYZ]): (\S+)', rest))
            val = 'xyz(%s, %s, %s)' % (v['X'], v['Y'], v['Z'])
        elif val.startswith('LocationInfo'):
            att = re.search(r'AttachmentAgent: (.*)', rest).group(1)
            node = re.search(r'AttachmentNode: (.*)', rest).group(1)
            tr = re.search(r'Translation: Vector3\n\s+X: (\S+)\n\s+Y: (\S+)\n\s+Z: (\S+)', rest)
            q = re.search(r'Rotation: Quaternion\n\s+IsIdentity: (\S+)\n\s+X: (\S+)\n\s+Y: (\S+)\n\s+Z: (\S+)\n\s+W: (\S+)', rest)
            val = 'attach=%r node=%s pos=(%s,%s,%s) quat(ident=%s)=(%s,%s,%s,%s)' % ((att, node) + tr.groups() + q.groups())
        elif val.startswith('BlendMode'):
            val = 'BlendMode ' + re.search(r'Val: (\S+)', rest).group(1)
        elif val.startswith('AnimOrChore'):
            val = 'chore ' + re.search(r'Chore: Handle`1\(Handle: Symbol: ([^)]*)\)', rest).group(1)
        elif val.startswith('Dictionary'):
            val = val
        out.append('%s=%s' % (key, val))
    print('## ' + name)
    for o in out:
        print('   ' + o)

"""把 template.html 和两张 PNG 贴图内嵌成单文件网页。

路径来自 .env：WOLF_WEB_WALL_PNG、WOLF_WEB_SPOT_PNG、WOLF_WEB_OUT。
两张 PNG 由 tools/d3dtx2png.py 从游戏贴图转出（不随仓库提供，见 README.md）。
"""
import base64
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / 'tools'))
import wolf_env as env

wall = env.path('WOLF_WEB_WALL_PNG')
spot = env.path('WOLF_WEB_SPOT_PNG')
out = env.path('WOLF_WEB_OUT')
b64 = lambda p: base64.b64encode(p.read_bytes()).decode()
html = (pathlib.Path(__file__).parent / 'template.html').read_text(encoding='utf-8')
html = html.replace('{{WALL}}', b64(wall)).replace('{{SPOT}}', b64(spot))
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(html, encoding='utf-8', newline='\n')
print(out, len(html) // 1024, 'KB')

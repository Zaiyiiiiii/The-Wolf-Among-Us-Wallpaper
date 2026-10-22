"""读取项目根目录的 .env（KEY=VALUE，# 开头为注释），供 tools/ 和 web/ 下的脚本共用。

已存在的系统环境变量优先于 .env；相对路径按项目根目录解析。没有第三方依赖。
"""
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def load():
    env = ROOT / '.env'
    if not env.is_file():
        return
    for line in env.read_text(encoding='utf-8-sig').splitlines():
        line = line.strip()
        if not line or line.startswith('#') or '=' not in line:
            continue
        key, _, val = line.partition('=')
        val = val.strip().strip('"').strip("'")
        os.environ.setdefault(key.strip(), val)


def get(name, default=None):
    """取变量；未设置且没有默认值时给出明确提示并退出。"""
    load()
    val = os.environ.get(name, default)
    if val is None or val == '':
        sys.exit(f'缺少配置 {name}：请在 {ROOT / ".env"} 中设置（参考 .env.example）')
    return val


def path(name, default=None):
    """取路径变量；相对路径按项目根目录解析。"""
    p = pathlib.Path(get(name, default))
    return p if p.is_absolute() else ROOT / p

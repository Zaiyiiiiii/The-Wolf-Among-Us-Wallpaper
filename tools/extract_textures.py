"""从游戏贴图 (.d3dtx) 取出原始块压缩数据，生成原生 exe 构建所需的 assets 文件。

输入输出路径来自 .env：WOLF_WALLPAPER_D3DTX、WOLF_SPOTLIGHT_D3DTX、WOLF_ASSETS_DIR。
输出: wall.bc3 (2048x2048 BC3, 4194304 字节)、spot.bc1 (512x512 BC1, 131072 字节)

d3dtx 的像素数据位于文件末尾（仅一级 mip），所以直接取末尾对应字节数。
d3dtx 本身需要先从游戏的 .ttarch2 归档里解出（例如用 TelltaleToolKit）。
"""
import pathlib
import sys

import wolf_env as env

WALL_SIZE = 2048 * 2048        # BC3：每像素 1 字节
SPOT_SIZE = 512 * 512 // 2     # BC1：每像素 0.5 字节

out = env.path('WOLF_ASSETS_DIR', 'assets')
out.mkdir(parents=True, exist_ok=True)
for var, name, size in (('WOLF_WALLPAPER_D3DTX', 'wall.bc3', WALL_SIZE), ('WOLF_SPOTLIGHT_D3DTX', 'spot.bc1', SPOT_SIZE)):
    src = env.path(var)
    data = src.read_bytes()
    if len(data) < size:
        sys.exit(f'{src}: 文件太小，期望至少 {size} 字节')
    (out / name).write_bytes(data[-size:])
    print(out / name, size)

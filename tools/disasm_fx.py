import ctypes, struct, sys

# 用法: disasm_fx.py <文件.t3fxb> -> 输出该文件中全部 D3D9 着色器的反汇编
d3dc = ctypes.WinDLL('d3dcompiler_47.dll')


class ID3DBlob(ctypes.Structure):
    pass


# ID3DBlob 虚表: QueryInterface, AddRef, Release, GetBufferPointer, GetBufferSize
vt = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)
ID3DBlob._fields_ = [('vtbl', ctypes.POINTER(ctypes.c_void_p))]


def blob_text(p):
    blob = ctypes.cast(p, ctypes.POINTER(ID3DBlob))
    get_ptr = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(blob.contents.vtbl[3])
    get_size = ctypes.CFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(blob.contents.vtbl[4])
    return ctypes.string_at(get_ptr(p), get_size(p)).decode('latin1')


def shader_len(d, off):
    """从版本令牌起沿指令流走到 END 令牌，返回字节数；失败返回 0。"""
    i = off + 4
    n = len(d)
    while i + 4 <= n:
        tok = struct.unpack_from('<I', d, i)[0]
        if tok == 0x0000FFFF:
            return i + 4 - off
        op = tok & 0xFFFF
        if op == 0xFFFE:  # 注释
            i += 4 + 4 * ((tok >> 16) & 0x7FFF)
        else:
            ln = (tok >> 24) & 0xF
            i += 4 + 4 * ln
    return 0


def main():
    d = open(sys.argv[1], 'rb').read()
    found = []
    for tag, name in ((b'\x00\x03\xfe\xff', 'vs_3_0'), (b'\x00\x03\xff\xff', 'ps_3_0')):
        p = 0
        while True:
            p = d.find(tag, p)
            if p < 0:
                break
            ln = shader_len(d, p)
            if ln and p + ln <= len(d):
                found.append((p, name, ln))
            p += 4
    found.sort()
    # 去掉被包含在更早着色器内部的伪命中
    out, end = [], 0
    for p, name, ln in found:
        if p >= end:
            out.append((p, name, ln))
            end = p + ln
    for p, name, ln in out:
        buf = d[p:p + ln]
        res = ctypes.c_void_p()
        hr = d3dc.D3DDisassemble(buf, len(buf), 0, None, ctypes.byref(res))
        print('=' * 20, name, 'offset', p, 'bytes', ln, 'hr', hex(hr & 0xFFFFFFFF))
        if hr == 0:
            print(blob_text(res.value))


main()

package com.zaiyii.wolves;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * 在设备上解码游戏的原始块压缩数据（BC3 / BC1），得到 RGBA8 像素供 OpenGL ES 上传。
 * 不依赖 S3TC 扩展（很多移动 GPU 不支持）。像素按行从上到下排列，与 Direct3D 的纹理坐标约定一致。
 */
final class TextureDecoder {
    private TextureDecoder() {}

    private static void rgb565(int c, int[] out, int o) {
        int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
        out[o] = (r << 3) | (r >> 2);
        out[o + 1] = (g << 2) | (g >> 4);
        out[o + 2] = (b << 3) | (b >> 2);
    }

    private static int le16(byte[] d, int o) {
        return (d[o] & 255) | ((d[o + 1] & 255) << 8);
    }

    private static int le32(byte[] d, int o) {
        return le16(d, o) | (le16(d, o + 2) << 16);
    }

    /** BC3（DXT5）：每 4x4 块 16 字节，前 8 字节为 alpha，后 8 字节为颜色（始终按 4 色模式）。 */
    static ByteBuffer decodeBc3(byte[] d, int w, int h) {
        byte[] px = new byte[w * h * 4];
        int bw = w / 4, bh = h / 4;
        int[] col = new int[12];
        int[] alpha = new int[8];
        for (int by = 0; by < bh; by++) {
            for (int bx = 0; bx < bw; bx++) {
                int off = (by * bw + bx) * 16;
                int a0 = d[off] & 255, a1 = d[off + 1] & 255;
                long abits = 0;
                for (int i = 5; i >= 0; i--) abits = (abits << 8) | (d[off + 2 + i] & 255);
                alpha[0] = a0;
                alpha[1] = a1;
                if (a0 > a1) {
                    for (int i = 1; i <= 6; i++) alpha[1 + i] = ((7 - i) * a0 + i * a1) / 7;
                } else {
                    for (int i = 1; i <= 4; i++) alpha[1 + i] = ((5 - i) * a0 + i * a1) / 5;
                    alpha[6] = 0;
                    alpha[7] = 255;
                }
                rgb565(le16(d, off + 8), col, 0);
                rgb565(le16(d, off + 10), col, 3);
                for (int k = 0; k < 3; k++) {
                    col[6 + k] = (2 * col[k] + col[3 + k]) / 3;
                    col[9 + k] = (col[k] + 2 * col[3 + k]) / 3;
                }
                int cbits = le32(d, off + 12);
                for (int py = 0; py < 4; py++) {
                    for (int pxl = 0; pxl < 4; pxl++) {
                        int n = py * 4 + pxl;
                        int ci = (cbits >>> (2 * n)) & 3;
                        int ai = (int) ((abits >>> (3 * n)) & 7);
                        int o = ((by * 4 + py) * w + bx * 4 + pxl) * 4;
                        px[o] = (byte) col[ci * 3];
                        px[o + 1] = (byte) col[ci * 3 + 1];
                        px[o + 2] = (byte) col[ci * 3 + 2];
                        px[o + 3] = (byte) alpha[ai];
                    }
                }
            }
        }
        return toDirect(px);
    }

    /** BC1（DXT1）：每 4x4 块 8 字节；只用到 RGB，alpha 一律填 255。 */
    static ByteBuffer decodeBc1(byte[] d, int w, int h) {
        byte[] px = new byte[w * h * 4];
        int bw = w / 4, bh = h / 4;
        int[] col = new int[12];
        for (int by = 0; by < bh; by++) {
            for (int bx = 0; bx < bw; bx++) {
                int off = (by * bw + bx) * 8;
                int c0 = le16(d, off), c1 = le16(d, off + 2);
                rgb565(c0, col, 0);
                rgb565(c1, col, 3);
                for (int k = 0; k < 3; k++) {
                    if (c0 > c1) {
                        col[6 + k] = (2 * col[k] + col[3 + k]) / 3;
                        col[9 + k] = (col[k] + 2 * col[3 + k]) / 3;
                    } else {
                        col[6 + k] = (col[k] + col[3 + k]) / 2;
                        col[9 + k] = 0;
                    }
                }
                int bits = le32(d, off + 4);
                for (int py = 0; py < 4; py++) {
                    for (int pxl = 0; pxl < 4; pxl++) {
                        int ci = (bits >>> (2 * (py * 4 + pxl))) & 3;
                        int o = ((by * 4 + py) * w + bx * 4 + pxl) * 4;
                        px[o] = (byte) col[ci * 3];
                        px[o + 1] = (byte) col[ci * 3 + 1];
                        px[o + 2] = (byte) col[ci * 3 + 2];
                        px[o + 3] = (byte) 255;
                    }
                }
            }
        }
        return toDirect(px);
    }

    private static ByteBuffer toDirect(byte[] px) {
        ByteBuffer b = ByteBuffer.allocateDirect(px.length).order(ByteOrder.nativeOrder());
        b.put(px);
        b.position(0);
        return b;
    }
}

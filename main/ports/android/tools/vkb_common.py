"""Helpers shared by the on-screen keyboard generators (tools/gen_*_vkb.py).

The pictures are drawn from scratch with Pillow at SS x the final size and
scaled down, then written as a C++ include: RGBA rows stored as byte
deltas of the row above (gradients and repeated caps compress far better),
zlib, plus the key table (code, flags, cap rectangle) that
src/machine/virtual_keyboard.cpp reads.
"""
import zlib

from PIL import Image, ImageDraw, ImageFilter, ImageFont

SS = 3
FONT_BOLD = "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf"
FONT = "/usr/share/fonts/truetype/freefont/FreeSans.ttf"


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(len(a)))


class Canvas:
    """RGBA drawing surface in final-picture coordinates (floats), supersampled."""

    def __init__(self, w, h, background=(0, 0, 0, 0)):
        self.w, self.h = w, h
        self.img = Image.new("RGBA", (w * SS, h * SS), background)
        self.draw = ImageDraw.Draw(self.img)

    @staticmethod
    def s(v):
        return int(round(v * SS))

    def box(self, x1, y1, x2, y2):
        return (self.s(x1), self.s(y1), self.s(x2), self.s(y2))

    def rrect(self, x1, y1, x2, y2, r, fill):
        self.draw.rounded_rectangle(self.box(x1, y1, x2, y2), radius=self.s(r), fill=fill)

    def vgrad_rrect(self, x1, y1, x2, y2, r, c1, c2, stops=None):
        """Rounded rectangle filled with a vertical gradient c1 -> c2."""
        w, h = self.s(x2) - self.s(x1), self.s(y2) - self.s(y1)
        if w <= 0 or h <= 0:
            return
        layer = Image.new("RGBA", (w, h))
        d = ImageDraw.Draw(layer)
        for y in range(h):
            t = y / max(1, h - 1)
            if stops:
                for (t0, a), (t1, b) in zip(stops, stops[1:]):
                    if t0 <= t <= t1:
                        c = lerp(a, b, (t - t0) / max(1e-6, t1 - t0))
                        break
            else:
                c = lerp(c1, c2, t)
            if len(c) == 3:
                c = c + (255,)
            d.line([(0, y), (w, y)], fill=c)
        mask = Image.new("L", (w, h), 0)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, w - 1, h - 1), radius=self.s(r), fill=255)
        self.img.paste(layer, (self.s(x1), self.s(y1)), mask)

    def shadow(self, shapes, blur, color=(0, 0, 0), alpha=200):
        """Soft shadow under rounded rects: shapes = [(x1, y1, x2, y2, r)]."""
        m = Image.new("L", self.img.size, 0)
        d = ImageDraw.Draw(m)
        for x1, y1, x2, y2, r in shapes:
            d.rounded_rectangle(self.box(x1, y1, x2, y2), radius=self.s(r), fill=alpha)
        m = m.filter(ImageFilter.GaussianBlur(self.s(blur)))
        self.img.paste(Image.new("RGBA", self.img.size, color + (255,)), (0, 0), m)

    def text(self, x, y, s, size, fill, anchor="la", bold=True, max_w=None, font=None):
        """Text whose width is squeezed to max_w (condensed, like keyboard print)."""
        if not s:
            return
        f = ImageFont.truetype(font or (FONT_BOLD if bold else FONT), self.s(size))
        l, t, r, b = f.getbbox(s, anchor="ls")
        tw, th = r - l, b - t
        pad = self.s(2)
        tmp = Image.new("L", (tw + 2 * pad, th + 2 * pad), 0)
        ImageDraw.Draw(tmp).text((pad - l, pad - t), s, font=f, fill=255, anchor="ls")
        target_w = tw
        if max_w is not None and tw > self.s(max_w):
            target_w = self.s(max_w)
            tmp = tmp.resize((target_w + 2 * pad, tmp.height), Image.LANCZOS)
        # Anchor: horizontal l/m/r, vertical a (top), m (middle), s (baseline ~ bottom).
        ax = {"l": 0, "m": target_w / 2, "r": target_w}[anchor[0]]
        ay = {"a": 0, "m": th / 2, "s": th, "d": th}[anchor[1]]
        px = int(self.s(x) - ax - pad)
        py = int(self.s(y) - ay - pad)
        self.img.paste(Image.new("RGBA", tmp.size, fill + (255,) if len(fill) == 3 else fill), (px, py), tmp)

    def final(self):
        return self.img.resize((self.w, self.h), Image.LANCZOS)


def write_inc(path, img, keys, prefix, header):
    """keys: [(code, flags, (x1, y1, x2, y2), comment)] in final pixels."""
    w, h = img.size
    raw = bytearray(img.convert("RGBA").tobytes())
    stride = w * 4
    out = bytearray(raw)
    for p in range(len(raw) - 1, stride - 1, -1):
        out[p] = (raw[p] - raw[p - stride]) & 0xFF
    data = zlib.compress(bytes(out), 9)
    with open(path, "w") as f:
        f.write(header)
        f.write(f"// Picture: {w}x{h} RGBA, each row a byte delta of the row above, zlib.\n")
        f.write(f"constexpr int k{prefix}Width = {w};\nconstexpr int k{prefix}Height = {h};\n")
        f.write(f"const unsigned char k{prefix}Picture[] = {{\n")
        for i in range(0, len(data), 24):
            f.write("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",\n")
        f.write("};\n")
        f.write("// code, flags (1 modifier, 2 lock), cap rect.\n")
        f.write(f"const VkbKeyDef k{prefix}Keys[] = {{\n")
        for code, flags, (x1, y1, x2, y2), comment in keys:
            comment = comment.replace("\\", "backslash")  # a trailing \ would continue the comment
            f.write(f"    {{{code}, {flags}, {round(x1)}, {round(y1)}, {round(x2)}, {round(y2)}}},  // {comment}\n")
        f.write("};\n")
    return len(data)

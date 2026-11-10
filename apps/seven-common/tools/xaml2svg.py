#!/usr/bin/env python3
"""Convert vector icons drawn in WPF XAML (as exported by Expression Blend: Viewbox > Grid/Canvas > Path with
Stretch="Fill", gradient brushes, opacity masks, clips) into SVG.

Used for the AMD Catalyst Control Center icons (resourcedictionarygraphics.xaml, decompiled from the user's own
Catalyst package). Usage: xaml2svg.py file.xaml OUTDIR [key ...]   (no keys: every DataTemplate with a Viewbox)
"""
import math, re, sys, os
import xml.etree.ElementTree as ET

NS_X = '{http://schemas.microsoft.com/winfx/2006/xaml}'


def local(tag):
    return tag.split('}', 1)[-1]


# ---------------------------------------------------------------- colours
def lin2srgb(c):
    c = max(0.0, min(1.0, c))
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def color(s):
    """-> (r, g, b, a) 0..255 / 0..1"""
    s = s.strip()
    if s.startswith('sc#'):
        v = [float(t) for t in s[3:].split(',')]
        a, r, g, b = v if len(v) == 4 else (1.0, *v)
        return tuple(round(lin2srgb(c) * 255) for c in (r, g, b)) + (max(0.0, min(1.0, a)),)
    if s.startswith('#'):
        h = s[1:]
        if len(h) == 3:
            h = 'F' + h
        if len(h) == 4:
            h = ''.join(c * 2 for c in h)
        if len(h) == 6:
            h = 'FF' + h
        a, r, g, b = (int(h[i:i + 2], 16) for i in (0, 2, 4, 6))
        return (r, g, b, a / 255)
    named = {'white': (255, 255, 255, 1), 'black': (0, 0, 0, 1), 'transparent': (0, 0, 0, 0)}
    return named.get(s.lower(), (0, 0, 0, 1))


def rgb(c):
    return '#%02x%02x%02x' % c[:3]


# ---------------------------------------------------------------- geometry
TOK = re.compile(r'[A-Za-z]|[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?')


def path_bbox(d):
    """tight-ish bounds of a path mini-language string (curves sampled)"""
    toks = TOK.findall(d)
    pts = []
    cur = (0.0, 0.0); start = cur; cmd = None; i = 0
    prev_ctrl = None

    def num():
        nonlocal i
        v = float(toks[i]); i += 1
        return v

    def cubic(p0, p1, p2, p3):
        for k in range(1, 17):
            t = k / 16; u = 1 - t
            pts.append((u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
                        u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]))

    def quad(p0, p1, p2):
        for k in range(1, 17):
            t = k / 16; u = 1 - t
            pts.append((u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0], u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1]))

    while i < len(toks):
        t = toks[i]
        if t.isalpha():
            cmd = t; i += 1
            if cmd in 'Ff':
                i += 1  # fill rule number
                continue
            if cmd in 'Zz':
                cur = start; continue
        c = cmd
        rel = c.islower(); C = c.upper()
        ox, oy = cur if rel else (0.0, 0.0)
        if C == 'M':
            cur = (ox + num(), oy + num()); start = cur; pts.append(cur); cmd = 'l' if rel else 'L'
        elif C == 'L':
            cur = (ox + num(), oy + num()); pts.append(cur)
        elif C == 'H':
            cur = ((cur[0] if rel else 0) + num(), cur[1]); pts.append(cur)
        elif C == 'V':
            cur = (cur[0], (cur[1] if rel else 0) + num()); pts.append(cur)
        elif C == 'C':
            p1 = (ox + num(), oy + num()); p2 = (ox + num(), oy + num()); p3 = (ox + num(), oy + num())
            cubic(cur, p1, p2, p3); prev_ctrl = p2; cur = p3
        elif C == 'S':
            p1 = (2 * cur[0] - prev_ctrl[0], 2 * cur[1] - prev_ctrl[1]) if prev_ctrl else cur
            p2 = (ox + num(), oy + num()); p3 = (ox + num(), oy + num())
            cubic(cur, p1, p2, p3); prev_ctrl = p2; cur = p3
        elif C == 'Q':
            p1 = (ox + num(), oy + num()); p2 = (ox + num(), oy + num()); quad(cur, p1, p2); cur = p2
        elif C == 'A':
            rx, ry, rot, la, sw = num(), num(), num(), num(), num()
            end = (ox + num(), oy + num())
            pts += [cur, end, (cur[0] - rx, cur[1] - ry), (cur[0] + rx, cur[1] + ry)]  # loose
            cur = end
        else:
            i += 1
        if C not in 'CS':
            prev_ctrl = None
    if not pts:
        return (0, 0, 0, 0)
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    return (min(xs), min(ys), max(xs), max(ys))


def svg_path(d):
    """WPF mini-language -> SVG d, fill-rule"""
    rule = 'evenodd'
    m = re.match(r'\s*F\s*([01])', d)
    if m:
        rule = 'nonzero' if m.group(1) == '1' else 'evenodd'
        d = d[m.end():]
    return d.replace(',', ' '), rule


def fnum(s, default=None):
    try:
        return float(s)
    except (TypeError, ValueError):
        return default


def thickness(s, default=(0, 0, 0, 0)):
    if not s:
        return default
    v = [float(t) for t in s.replace(' ', ',').split(',') if t]
    if len(v) == 1:
        return (v[0],) * 4
    if len(v) == 2:
        return (v[0], v[1], v[0], v[1])
    return tuple(v[:4])


def matrix_str(s):
    v = [float(t) for t in s.replace(' ', ',').split(',') if t]
    return 'matrix(%s)' % ' '.join('%g' % x for x in v)


# ---------------------------------------------------------------- converter
class Conv:
    def __init__(self):
        self.defs = []
        self.n = 0

    def uid(self, p):
        self.n += 1
        return '%s%d' % (p, self.n)

    def brush(self, el):
        """Brush element -> svg paint string"""
        tag = local(el.tag)
        op = float(el.get('Opacity', '1'))
        if tag == 'SolidColorBrush':
            c = color(el.get('Color', '#000'))
            return rgb(c), c[3] * op
        stops = []
        for gs in el.iter():
            if local(gs.tag) == 'GradientStop':
                c = color(gs.get('Color', '#000'))
                stops.append('<stop offset="%s" stop-color="%s" stop-opacity="%.4f"/>' % (gs.get('Offset', '0'), rgb(c), c[3]))
        rel = el.get('MappingMode', 'RelativeToBoundingBox') != 'Absolute'
        units = 'objectBoundingBox' if rel else 'userSpaceOnUse'
        spread = {'Reflect': 'reflect', 'Repeat': 'repeat'}.get(el.get('SpreadMethod', ''), 'pad')
        gid = self.uid('g')
        tr = ''
        for ch in el:
            if local(ch.tag).endswith('.Transform') or local(ch.tag).endswith('.RelativeTransform'):
                for m in ch.iter():
                    if local(m.tag) == 'MatrixTransform' and m.get('Matrix'):
                        tr = ' gradientTransform="%s"' % matrix_str(m.get('Matrix'))
        if tag == 'LinearGradientBrush':
            sx, sy = (float(t) for t in el.get('StartPoint', '0,0').split(','))
            ex, ey = (float(t) for t in el.get('EndPoint', '1,1').split(','))
            self.defs.append('<linearGradient id="%s" gradientUnits="%s" spreadMethod="%s" x1="%g" y1="%g" x2="%g" y2="%g"%s>%s</linearGradient>'
                             % (gid, units, spread, sx, sy, ex, ey, tr, ''.join(stops)))
        else:
            cx, cy = (float(t) for t in el.get('Center', '0.5,0.5').split(','))
            fx, fy = (float(t) for t in el.get('GradientOrigin', el.get('Center', '0.5,0.5')).split(','))
            rx = float(el.get('RadiusX', '0.5')); ry = float(el.get('RadiusY', '0.5'))
            if abs(rx - ry) > 1e-6 and not tr:
                # ellipse: scale around the centre
                k = ry / rx if rx else 1
                tr = ' gradientTransform="translate(%g %g) scale(1 %g) translate(%g %g)"' % (cx, cy, k, -cx, -cy)
            self.defs.append('<radialGradient id="%s" gradientUnits="%s" spreadMethod="%s" cx="%g" cy="%g" r="%g" fx="%g" fy="%g"%s>%s</radialGradient>'
                             % (gid, units, spread, cx, cy, rx, fx, fy, tr, ''.join(stops)))
        return 'url(#%s)' % gid, op

    def prop(self, el, name):
        """attribute or property element (Path.Fill etc.) -> ('attr', str) | ('el', element) | None"""
        if el.get(name) is not None:
            return ('attr', el.get(name))
        for ch in el:
            if local(ch.tag) == '%s.%s' % (local(el.tag), name):
                kids = list(ch)
                return ('el', kids[0]) if kids else None
        return None

    def paint(self, el, name):
        p = self.prop(el, name)
        if p is None:
            return None, 1
        if p[0] == 'attr':
            v = p[1]
            if v.startswith('{'):
                return None, 1
            c = color(v)
            return rgb(c), c[3]
        return self.brush(p[1])

    def geometry(self, el):
        p = self.prop(el, 'Data')
        if p is None:
            return None, None, None
        if p[0] == 'attr':
            d, rule = svg_path(p[1])
            return d, rule, None
        g = p[1]
        tr = None
        for ch in g:
            if local(ch.tag).endswith('.Transform'):
                for m in ch.iter():
                    if local(m.tag) == 'MatrixTransform':
                        tr = [float(t) for t in m.get('Matrix').replace(' ', ',').split(',') if t]
        d, rule = svg_path(g.get('Figures', ''))
        if g.get('FillRule') == 'Nonzero':
            rule = 'nonzero'
        return d, rule, tr

    def slot(self, el, rect, natural):
        """place el inside rect (x, y, w, h) honouring Margin, Width/Height and alignment"""
        x, y, w, h = rect
        ml, mt, mr, mb = thickness(el.get('Margin'))
        x += ml; y += mt; w -= ml + mr; h -= mt + mb
        ew = fnum(el.get('Width'))
        eh = fnum(el.get('Height'))
        ha = el.get('HorizontalAlignment', 'Stretch'); va = el.get('VerticalAlignment', 'Stretch')
        if ew is None and ha != 'Stretch':
            ew = natural[0]
        if eh is None and va != 'Stretch':
            eh = natural[1]
        if ew is not None:
            x += {'Left': 0, 'Right': w - ew}.get(ha, (w - ew) / 2); w = ew
        if eh is not None:
            y += {'Top': 0, 'Bottom': h - eh}.get(va, (h - eh) / 2); h = eh
        return (x, y, w, h)

    def wrap(self, el, inner, rect):
        """opacity / opacity mask / clip / render transform around an element's output"""
        attrs = ''
        op = float(el.get('Opacity', '1'))
        if op < 1:
            attrs += ' opacity="%g"' % op
        om = self.prop(el, 'OpacityMask')
        if om and om[0] == 'el':
            paint, pop = self.brush(om[1])
            mid = self.uid('m')
            bx, by, bw, bh = self.last_bbox if hasattr(self, 'last_bbox') and self.last_bbox else rect
            # WPF opacity masks use the brush alpha: SVG mask-type alpha over the masked element's box
            self.defs.append('<mask id="%s" mask-type="alpha" style="mask-type:alpha" maskUnits="userSpaceOnUse" x="%g" y="%g" width="%g" height="%g"><rect x="%g" y="%g" width="%g" height="%g" fill="%s" fill-opacity="%g"/></mask>'
                             % (mid, bx - 1000, by - 1000, bw + 2000, bh + 2000, bx, by, bw, bh, paint or '#fff', pop))
            attrs += ' mask="url(#%s)"' % mid
        clip = self.prop(el, 'Clip')
        if clip:
            if clip[0] == 'attr':
                d, rule = svg_path(clip[1]); tr = None
            else:
                g = clip[1]
                if local(g.tag) == 'RectangleGeometry':
                    rx, ry, rw, rh = (float(t) for t in g.get('Rect').split(','))
                    d, rule, tr = 'M%g %gh%gv%gh%gz' % (rx, ry, rw, rh, -rw), 'nonzero', None
                else:
                    d, rule = svg_path(g.get('Figures', '')); tr = None
            cid = self.uid('c')
            self.defs.append('<clipPath id="%s"><path d="%s" clip-rule="%s" transform="translate(%g %g)"/></clipPath>' % (cid, d, rule, rect[0], rect[1]))
            attrs += ' clip-path="url(#%s)"' % cid
        rt = self.prop(el, 'RenderTransform')
        if rt and rt[0] == 'el':
            t = self.transform(rt[1], rect)
            if t:
                attrs += ' transform="%s"' % t
        return '<g%s>%s</g>' % (attrs, inner) if attrs else inner

    def transform(self, el, rect):
        tag = local(el.tag)
        ox = rect[0] + rect[2] * float((el.get('CenterX') or 0))  # absolute centres are rare
        if tag == 'TransformGroup':
            parts = [self.transform(ch, rect) for ch in el]
            return ' '.join(p for p in reversed(parts) if p)
        if tag == 'MatrixTransform':
            return matrix_str(el.get('Matrix'))
        if tag == 'TranslateTransform':
            return 'translate(%s %s)' % (el.get('X', '0'), el.get('Y', '0'))
        if tag == 'ScaleTransform':
            sx, sy = float(el.get('ScaleX', '1')), float(el.get('ScaleY', '1'))
            if sx == 1 and sy == 1:
                return ''
            cx, cy = float(el.get('CenterX', '0')), float(el.get('CenterY', '0'))
            return 'translate(%g %g) scale(%g %g) translate(%g %g)' % (rect[0] + cx, rect[1] + cy, sx, sy, -rect[0] - cx, -rect[1] - cy)
        if tag == 'RotateTransform':
            a = float(el.get('Angle', '0'))
            if not a:
                return ''
            return 'rotate(%g %g %g)' % (a, rect[0] + float(el.get('CenterX', '0')), rect[1] + float(el.get('CenterY', '0')))
        return ''

    def node(self, el, rect):
        tag = local(el.tag)
        if el.get('Visibility') in ('Collapsed', 'Hidden'):
            return ''
        if tag in ('Grid', 'Canvas', 'Border', 'Viewbox'):
            r = self.slot(el, rect, (rect[2], rect[3]))
            out = []
            for ch in el:
                ct = local(ch.tag)
                if '.' in ct:
                    continue
                if tag == 'Canvas':
                    cl = float(ch.get('Canvas.Left', '0')); ctp = float(ch.get('Canvas.Top', '0'))
                    cw = fnum(ch.get('Width'))
                    chh = fnum(ch.get('Height'))
                    if cw is None or chh is None:
                        nat = self.natural(ch)
                        cw = cw if cw is not None else nat[0]; chh = chh if chh is not None else nat[1]
                    cr = (r[0] + cl, r[1] + ctp, cw, chh)
                    ch2 = ch
                    out.append(self.node_at(ch2, cr))
                else:
                    out.append(self.node(ch, r))
            return self.wrap(el, ''.join(out), r)
        if tag in ('Path', 'Rectangle', 'Ellipse'):
            return self.shape(el, self.slot(el, rect, self.natural(el)))
        return ''

    def node_at(self, el, rect):
        """element already placed (Canvas child): ignore its Margin/Width/Height"""
        tag = local(el.tag)
        if tag in ('Path', 'Rectangle', 'Ellipse'):
            return self.shape(el, rect)
        saved = {k: el.attrib.pop(k) for k in ('Margin', 'Width', 'Height') if k in el.attrib}
        try:
            return self.node(el, rect)
        finally:
            el.attrib.update(saved)

    def natural(self, el):
        tag = local(el.tag)
        if tag == 'Path':
            d, rule, tr = self.geometry(el)
            if not d:
                return (0, 0)
            x0, y0, x1, y1 = path_bbox(d)
            return (x1 - x0, y1 - y0)
        return (fnum(el.get('Width'), 0), fnum(el.get('Height'), 0))

    def shape(self, el, rect):
        tag = local(el.tag)
        x, y, w, h = rect
        fill, fop = self.paint(el, 'Fill')
        stroke, sop = self.paint(el, 'Stroke')
        sw = float(el.get('StrokeThickness', '1')) if stroke else 0
        style = 'fill="%s"' % (fill or 'none')
        if fill and fop < 1:
            style += ' fill-opacity="%.4f"' % fop
        if stroke:
            style += ' stroke="%s" stroke-width="%g"' % (stroke, sw)
            if sop < 1:
                style += ' stroke-opacity="%.4f"' % sop
            if el.get('StrokeLineJoin'):
                style += ' stroke-linejoin="%s"' % el.get('StrokeLineJoin').lower()
        if tag == 'Rectangle':
            rx = el.get('RadiusX', '0'); ry = el.get('RadiusY', '0')
            body = '<rect x="%g" y="%g" width="%g" height="%g" rx="%s" ry="%s" %s/>' % (x + sw / 2, y + sw / 2, max(0, w - sw), max(0, h - sw), rx, ry, style)
            self.last_bbox = rect
            return self.wrap(el, body, rect)
        if tag == 'Ellipse':
            body = '<ellipse cx="%g" cy="%g" rx="%g" ry="%g" %s/>' % (x + w / 2, y + h / 2, max(0, w / 2 - sw / 2), max(0, h / 2 - sw / 2), style)
            self.last_bbox = rect
            return self.wrap(el, body, rect)
        d, rule, gtr = self.geometry(el)
        if not d:
            return ''
        bx0, by0, bx1, by1 = path_bbox(d)
        bw, bh = bx1 - bx0, by1 - by0
        stretch = el.get('Stretch', 'None')
        if stretch == 'Fill' and bw > 0 and bh > 0:
            sx = (w - sw) / bw if bw else 1; sy = (h - sw) / bh if bh else 1
            tr = 'translate(%g %g) scale(%g %g) translate(%g %g)' % (x + sw / 2, y + sw / 2, sx, sy, -bx0, -by0)
            self.last_bbox = (x, y, w, h)
        elif stretch == 'Uniform' and bw > 0 and bh > 0:
            s = min((w - sw) / bw, (h - sw) / bh)
            tr = 'translate(%g %g) scale(%g) translate(%g %g)' % (x + (w - bw * s) / 2, y + (h - bh * s) / 2, s, -bx0, -by0)
            self.last_bbox = (x, y, w, h)
        else:
            tr = 'translate(%g %g)' % (x, y)
            if gtr:
                tr += ' matrix(%s)' % ' '.join('%g' % v for v in gtr)
            self.last_bbox = (x + bx0, y + by0, bw, bh)
        # strokes in WPF stay the given width in device units for Stretch=Fill paths
        if stroke and stretch == 'Fill':
            style += ' vector-effect="non-scaling-stroke"'
        body = '<path d="%s" fill-rule="%s" transform="%s" %s/>' % (d, rule, tr, style)
        return self.wrap(el, body, rect)


def convert(template):
    """DataTemplate (or any element) holding a Viewbox -> svg text, (w, h)"""
    vb = None
    for e in template.iter():
        if local(e.tag) == 'Viewbox':
            vb = e
            break
    root = vb if vb is not None else template
    kids = [k for k in root if '.' not in local(k.tag)]
    if not kids:
        return None
    top = kids[0]
    w = fnum(top.get('Width'), 0); h = fnum(top.get('Height'), 0)
    c = Conv()
    if not w or not h:
        w, h = 48, 48
    body = c.node(top, (0, 0, w, h)) if not top.get('Margin') else c.node(top, (0, 0, w, h))
    svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %g %g" width="%g" height="%g"><defs>%s</defs>%s</svg>' % (w, h, w, h, ''.join(c.defs), body)
    return svg, (w, h)


def main():
    src, out = sys.argv[1], sys.argv[2]
    keys = set(sys.argv[3:])
    os.makedirs(out, exist_ok=True)
    tree = ET.parse(src)
    n = 0
    for el in tree.getroot().iter():
        k = el.get(NS_X + 'Key')
        if not k or (keys and k not in keys):
            continue
        if not any(local(e.tag) == 'Viewbox' for e in el.iter()):
            continue
        try:
            r = convert(el)
        except Exception as e:
            print('skip', k, e)
            continue
        if r:
            open(os.path.join(out, k + '.svg'), 'w').write(r[0])
            n += 1
    print(n, 'icons')


if __name__ == '__main__':
    main()

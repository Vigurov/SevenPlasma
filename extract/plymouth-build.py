#!/usr/bin/env python3
"""Pictures of the SevenPlasma boot / shutdown screen (Plymouth), made on the user's machine.

    plymouth-build.py WINDOWS_ROOT ATP_DIR FONT_DIR OUT_DIR

- frame000..104.png: Windows 7's "Starting Windows" animation, bootres.dll -> embedded WIM -> activity.bmp,
  105 frames of 200 px, scaled 1.4 (Lanczos) like the logo of a 1080p screen
- logon_bg.png: imageres.dll IMAGE 5036 (1920x1200), centre 1920x1080
- spin00..17.png, watermark.png: AeroThemePlasma's authui7 pictures
- starting.png, copyright.png, shutdown_text.png: the texts, drawn with Segoe UI from the user's Windows fonts
"""
import io, os, shutil, subprocess, sys, tempfile
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
from win7index import PE
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location('sx', os.path.join(HERE, 'seven-extract.py'))
sx = module_from_spec(spec)
spec.loader.exec_module(sx)


def resource(root, rel, pred):
    pe = PE(open(sx.find_ci(root, rel), 'rb').read())
    for t, i, b in pe.all():
        if pred(t, i, b):
            return b
    raise KeyError(rel)


def text(s, size, colour, shadow=False, pad=5):
    font = ImageFont.truetype(FONT, size)
    x0, y0, x1, y1 = font.getbbox(s)
    im = Image.new('RGBA', (x1 - x0 + 2 * pad, y1 - y0 + 2 * pad), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    if shadow:
        sh = Image.new('RGBA', im.size, (0, 0, 0, 0))
        ImageDraw.Draw(sh).text((pad - x0 + 1, pad - y0 + 1), s, font=font, fill=(0, 0, 0, 160))
        im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(1.5)))
    d.text((pad - x0, pad - y0), s, font=font, fill=colour)
    return im


def main():
    global FONT
    root, atp, fonts, out = sys.argv[1:5]
    FONT = os.path.join(fonts, 'segoeui.ttf')
    os.makedirs(out, exist_ok=True)
    wim = resource(root, 'Windows/System32/bootres.dll', lambda t, i, b: b[:5] == b'MSWIM')
    with tempfile.TemporaryDirectory() as tmp:
        open(os.path.join(tmp, 'b.wim'), 'wb').write(wim)
        subprocess.run(['7z', 'x', '-y', '-o' + tmp, os.path.join(tmp, 'b.wim'), 'activity.bmp'], check=True, stdout=subprocess.DEVNULL)
        act = Image.open(os.path.join(tmp, 'activity.bmp')).convert('RGB')
    for k in range(act.size[1] // 200):
        act.crop((0, 200 * k, 200, 200 * k + 200)).resize((280, 280), Image.LANCZOS).save(os.path.join(out, 'frame%03d.png' % k))
    bg = Image.open(io.BytesIO(resource(root, 'Windows/System32/imageres.dll', lambda t, i, b: i == 5036 and str(t) == 'IMAGE'))).convert('RGB')
    bg.crop((0, (bg.size[1] - 1080) // 2, 1920, (bg.size[1] - 1080) // 2 + 1080)).save(os.path.join(out, 'logon_bg.png'))
    a7 = os.path.join(atp, 'plasma/look-and-feel/authui7/contents/images')
    for k in range(18):
        shutil.copy(os.path.join(a7, '100/spin%d.png' % k), os.path.join(out, 'spin%02d.png' % k))
    shutil.copy(os.path.join(a7, 'watermark.png'), os.path.join(out, 'watermark.png'))
    # the texts in the system language, as Windows 7 shows them (Ukrainian or English)
    uk = (os.environ.get('LANGUAGE') or os.environ.get('LC_ALL') or os.environ.get('LANG') or '').startswith('uk')
    t = ('Запуск Windows', '© Корпорація Майкрософт', 'Завершення роботи...') if uk else \
        ('Starting Windows', '© Microsoft Corporation', 'Shutting down...')
    text(t[0], 25, (255, 255, 255, 255)).save(os.path.join(out, 'starting.png'))
    text(t[1], 15, (135, 135, 135, 255)).save(os.path.join(out, 'copyright.png'))
    text(t[2], 24, (255, 255, 255, 255), shadow=True, pad=2).save(os.path.join(out, 'shutdown_text.png'))
    print('plymouth pictures: %d frames' % (act.size[1] // 200))


if __name__ == '__main__':
    main()

/*
 * Fonts (Control Panel): what fontconfig knows about the installed fonts, the bits of the font files
 * Windows 7 shows (name / OS/2 tables), hiding fonts and drawing the samples of one exact font file.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QLocale>
#include <QPainter>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QtEndian>

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_LCD_FILTER_H

namespace Win7Fonts
{

enum {
    FontsView = 36, // Control Panel\Fonts
    FamilyView = 37, // one font family, opened from the folder
    SettingsView = 38, // Font settings
};

struct Face {
    QString family;
    QString style;
    QString file;
    int index = 0;
    bool hidden = false; // rejected by our fontconfig snippet
    bool symbol = false;
    bool rtl = false; // the sample is Hebrew, Arabic, Syriac or Thaana
    QStringList scripts; // "Designed for", in Windows 7 words
    QString sample; // the three letters on the tile ("Абф", "Abg", "אגץ"...)
    int weight = 80;
    int slant = 0;
};

struct Family {
    QString name;
    QList<Face> faces;
    bool hidden() const
    {
        for (const Face &f : faces) {
            if (!f.hidden) {
                return false;
            }
        }
        return !faces.isEmpty();
    }
    bool mixed() const
    {
        bool h = false, s = false;
        for (const Face &f : faces) {
            (f.hidden ? h : s) = true;
        }
        return h && s;
    }
};

inline QString userFontDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/fonts");
}

/** Fonts we may delete: the user's own; the packaged ones under /usr belong to the system */
inline bool isUserFont(const QString &file)
{
    const QString home = QDir::homePath() + QLatin1Char('/');
    return file.startsWith(home);
}

inline QSettings &fontSettings()
{
    static QSettings s(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/seven-fontsrc"), QSettings::IniFormat);
    return s;
}

inline QStringList hiddenFiles()
{
    return fontSettings().value(QStringLiteral("Hidden/Files")).toStringList();
}

inline QString hiddenConf()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/fontconfig/conf.d/90-seven-hidden-fonts.conf");
}

/** "Hide": fontconfig stops offering the file to programs, like the hidden fonts of Windows 7 */
inline void setHiddenFiles(QStringList files)
{
    files.removeDuplicates();
    files.sort();
    fontSettings().setValue(QStringLiteral("Hidden/Files"), files);
    fontSettings().sync();
    QDir().mkpath(QFileInfo(hiddenConf()).path());
    if (files.isEmpty()) {
        QFile::remove(hiddenConf());
    } else {
        QFile f(hiddenConf());
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QString xml = QStringLiteral("<?xml version=\"1.0\"?>\n<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n"
                                         "<!-- written by the Fonts folder of seven-control: fonts hidden from programs -->\n"
                                         "<fontconfig>\n <selectfont>\n  <rejectfont>\n");
            for (const QString &file : std::as_const(files)) {
                xml += QStringLiteral("   <glob>%1</glob>\n").arg(file.toHtmlEscaped());
            }
            xml += QStringLiteral("  </rejectfont>\n </selectfont>\n</fontconfig>\n");
            f.write(xml.toUtf8());
        }
    }
    FcInitReinitialize();
}

/* ---------------- what the font is for ---------------- */

struct ScriptInfo {
    const char *lang;
    const char *name;
    const char *sample; // fontext.dll string table
};

inline const QList<ScriptInfo> &scriptTable()
{
    static const QList<ScriptInfo> t{
        {"uk", "Кирилиця", "Абф"},
        {"en", "Латиниця", "Abg"},
        {"el", "Грецька", "Αβγ"},
        {"hy", "Вірменська", "Աբգ"},
        {"he", "Іврит", "אגץ"},
        {"ar", "Арабська", "أ​ب​ج"},
        {"syr", "Сирійська", "ܐ​ܒ​ܓ"},
        {"dv", "Тана", "ހބޗ"},
        {"hi", "Деванагарі", "अबक"},
        {"bn", "Бенгальська", "অবক"},
        {"pa", "Гурмукхі", "ਅਬਕ"},
        {"gu", "Гуджараті", "અબક"},
        {"or", "Орія", "ଅବକ"},
        {"ta", "Тамільська", "அஇஉ"},
        {"te", "Телугу", "అబక"},
        {"kn", "Каннада", "ಅಬಕ"},
        {"ml", "Малаялам", "അബക"},
        {"si", "Сингальська", "අබක"},
        {"th", "Тайська", "กคฎ"},
        {"lo", "Лаоська", "ກຂຄ"},
        {"bo", "Тибетська", "ཀཁག"},
        {"my", "М'янма", "အဗက"},
        {"ka", "Грузинська", "აბგ"},
        {"am", "Ефіопська", "ግዕዝ"},
        {"chr", "Черокі", "ᏣᎳᎩ"},
        {"iu", "Канадське складове письмо", "ᐊᕕᒋ"},
        {"km", "Кхмерська", "កខគ"},
        {"mn-cn", "Монгольська", "ᠠ​ᠡ​ᠢ"},
        {"ja", "Японська", "あいう"},
        {"ko", "Корейська", "한글"},
        {"zh-cn", "Китайська (спрощене письмо)", "简体字"},
        {"zh-tw", "Китайська (традиційне письмо)", "繁體字"},
    };
    return t;
}

/* ---------------- the font file itself ---------------- */

/** Tables of one face of a .ttf / .otf / .ttc, read on demand (CJK fonts are tens of megabytes) */
class SfntFile
{
public:
    explicit SfntFile(const QString &file, int index = 0)
        : m_file(file)
    {
        if (!m_file.open(QIODevice::ReadOnly)) {
            return;
        }
        quint32 dir = 0;
        const QByteArray head = read(0, 16);
        if (head.startsWith("ttcf")) {
            if (quint32(index) >= be32(head, 8)) {
                return;
            }
            dir = be32(read(12 + 4 * index, 4), 0);
        }
        const int tables = be16(read(dir + 4, 2), 0);
        const QByteArray recs = read(dir + 12, 16 * tables);
        for (int i = 0; i + 16 <= recs.size(); i += 16) {
            m_tables.insert(recs.mid(i, 4), {be32(recs, i + 8), be32(recs, i + 12)});
        }
    }

    bool has(const char *tag) const
    {
        return m_tables.contains(QByteArray(tag));
    }

    /** name table record (prefers English or Ukrainian, Windows platform) */
    QString name(int id)
    {
        const auto t = m_tables.value("name");
        if (!t.second || t.second > 4 * 1024 * 1024) {
            return {};
        }
        const QByteArray d = read(t.first, t.second);
        const int count = be16(d, 2);
        const int strings = be16(d, 4);
        QString best;
        int bestScore = -1;
        for (int i = 0; i < count; ++i) {
            const int r = 6 + 12 * i;
            const int platform = be16(d, r), lang = be16(d, r + 4), nameId = be16(d, r + 6), len = be16(d, r + 8), off = be16(d, r + 10);
            if (nameId != id || strings + off + len > d.size()) {
                continue;
            }
            QString s;
            int score = 0;
            if (platform == 3 || platform == 0) {
                for (int k = 0; k + 1 < len; k += 2) {
                    s += QChar(char16_t(be16(d, strings + off + k)));
                }
                score = lang == 0x422 ? 4 : (platform == 3 && lang == 0x409 ? 3 : 2);
            } else if (platform == 1) {
                s = QString::fromLatin1(d.mid(strings + off, len));
                score = 1;
            }
            if (score > bestScore && !s.trimmed().isEmpty()) {
                best = s.trimmed();
                bestScore = score;
            }
        }
        return best;
    }

    /** OS/2 fsType: embedding permissions */
    int fsType()
    {
        const auto t = m_tables.value("OS/2");
        return t.second >= 10 ? be16(read(t.first + 8, 2), 0) : -1;
    }

    bool valid() const
    {
        return !m_tables.isEmpty();
    }

private:
    QByteArray read(qint64 at, qint64 len)
    {
        if (!m_file.isOpen() || len <= 0 || !m_file.seek(at)) {
            return {};
        }
        return m_file.read(len);
    }
    static quint16 be16(const QByteArray &d, int at)
    {
        return at >= 0 && at + 2 <= d.size() ? qFromBigEndian<quint16>(d.constData() + at) : 0;
    }
    static quint32 be32(const QByteArray &d, int at)
    {
        return at >= 0 && at + 4 <= d.size() ? qFromBigEndian<quint32>(d.constData() + at) : 0;
    }

    QFile m_file;
    QHash<QByteArray, QPair<quint32, quint32>> m_tables;
};

/** "OpenType" / "TrueType": the word Windows 7 puts after the name */
inline QString fontType(const QString &file, int index)
{
    const QString suffix = QFileInfo(file).suffix().toLower();
    if (suffix == QLatin1String("pfb") || suffix == QLatin1String("pfa")) {
        return QStringLiteral("Type 1");
    }
    SfntFile s(file, index);
    if (s.has("CFF ") || s.has("CFF2") || s.has("DSIG")) {
        return QStringLiteral("OpenType");
    }
    return QStringLiteral("TrueType");
}

inline QString embeddability(int fsType)
{
    if (fsType < 0) {
        return {};
    }
    if (fsType & 0x0002) {
        return QStringLiteral("Обмежена ліцензія");
    }
    if (fsType & 0x0004) {
        return QStringLiteral("Друк і попередній перегляд");
    }
    if (fsType & 0x0008) {
        return QStringLiteral("Можна редагувати");
    }
    return QStringLiteral("Можна встановлювати");
}

/** "Regular" -> "звичайний", like the captions of the Ukrainian Fonts folder */
inline QString styleName(const QString &style)
{
    static const QList<QPair<QString, QString>> words{
        {QStringLiteral("Bold Italic"), QStringLiteral("напівжирний курсив")}, {QStringLiteral("Bold Oblique"), QStringLiteral("напівжирний похилий")},
        {QStringLiteral("SemiBold"), QStringLiteral("напівжирний (середній)")}, {QStringLiteral("Semibold"), QStringLiteral("напівжирний (середній)")},
        {QStringLiteral("ExtraBold"), QStringLiteral("наджирний")}, {QStringLiteral("Black"), QStringLiteral("чорний")},
        {QStringLiteral("Bold"), QStringLiteral("напівжирний")}, {QStringLiteral("Italic"), QStringLiteral("курсив")},
        {QStringLiteral("Oblique"), QStringLiteral("похилий")}, {QStringLiteral("Regular"), QStringLiteral("звичайний")},
        {QStringLiteral("Normal"), QStringLiteral("звичайний")}, {QStringLiteral("Book"), QStringLiteral("звичайний")},
        {QStringLiteral("ExtraLight"), QStringLiteral("надсвітлий")}, {QStringLiteral("Light"), QStringLiteral("світлий")},
        {QStringLiteral("Thin"), QStringLiteral("тонкий")}, {QStringLiteral("Medium"), QStringLiteral("середній")},
        {QStringLiteral("Condensed"), QStringLiteral("вузький")}, {QStringLiteral("Narrow"), QStringLiteral("вузький")},
    };
    QString s = style;
    for (const auto &w : words) {
        s.replace(w.first, w.second, Qt::CaseSensitive);
    }
    return s;
}

/* ---------------- the list ---------------- */

inline void fillFace(Face &f, FcPattern *p)
{
    FcChar8 *s = nullptr;
    if (FcPatternGetString(p, FC_FAMILY, 0, &s) == FcResultMatch) {
        f.family = QString::fromUtf8(reinterpret_cast<const char *>(s));
    }
    if (FcPatternGetString(p, FC_STYLE, 0, &s) == FcResultMatch) {
        f.style = QString::fromUtf8(reinterpret_cast<const char *>(s));
    }
    if (FcPatternGetString(p, FC_FILE, 0, &s) == FcResultMatch) {
        f.file = QString::fromUtf8(reinterpret_cast<const char *>(s));
    }
    int i = 0;
    if (FcPatternGetInteger(p, FC_INDEX, 0, &i) == FcResultMatch) {
        f.index = i & 0xffff;
    }
    FcPatternGetInteger(p, FC_WEIGHT, 0, &f.weight);
    FcPatternGetInteger(p, FC_SLANT, 0, &f.slant);
    FcLangSet *ls = nullptr;
    if (FcPatternGetLangSet(p, FC_LANG, 0, &ls) == FcResultMatch) {
        // the sample is the font's own script, like fontext.dll: East Asian first, then ours (Cyrillic),
        // then any other non-Latin one, and Latin only for fonts that have nothing else
        QString cjk, cyrillic, other, latin;
        for (const ScriptInfo &si : scriptTable()) {
            if (FcLangSetHasLang(ls, reinterpret_cast<const FcChar8 *>(si.lang)) != FcLangEqual) {
                continue;
            }
            f.scripts << QString::fromUtf8(si.name);
            const QString lang = QString::fromLatin1(si.lang);
            QString &slot = (lang == QLatin1String("ja") || lang == QLatin1String("ko") || lang.startsWith(QLatin1String("zh"))) ? cjk
                : lang == QLatin1String("uk")                                                                                 ? cyrillic
                : lang == QLatin1String("en")                                                                                 ? latin
                                                                                                                              : other;
            if (slot.isEmpty()) {
                slot = lang;
            }
        }
        // Greek and Armenian fonts nearly always carry Latin too; they are not "the font's script" unless alone
        const QString pick = !cjk.isEmpty() ? cjk : !cyrillic.isEmpty() ? cyrillic : (!other.isEmpty() && (latin.isEmpty() || (other != QLatin1String("el") && other != QLatin1String("hy")))) ? other : latin;
        for (const ScriptInfo &si : scriptTable()) {
            if (pick == QLatin1String(si.lang)) {
                f.sample = QString::fromUtf8(si.sample);
                f.rtl = pick == QLatin1String("he") || pick == QLatin1String("ar") || pick == QLatin1String("syr") || pick == QLatin1String("dv");
            }
        }
    }
    if (f.sample.isEmpty()) {
        // symbol fonts: the letters themselves, drawn with whatever the font has there
        f.symbol = true;
        f.sample = QStringLiteral("Abg");
    }
}

/** Every font face fontconfig lists, plus the ones we hid from it */
inline QList<Face> installedFaces()
{
    FcInitBringUptoDate();
    QList<Face> faces;
    QSet<QString> seen;
    FcPattern *pat = FcPatternCreate();
    FcObjectSet *os = FcObjectSetBuild(FC_FAMILY, FC_STYLE, FC_FILE, FC_INDEX, FC_LANG, FC_WEIGHT, FC_SLANT, FC_VARIABLE, FC_FONTFORMAT, nullptr);
    FcFontSet *fs = FcFontList(nullptr, pat, os);
    for (int i = 0; fs && i < fs->nfont; ++i) {
        FcPattern *p = fs->fonts[i];
        FcBool variable = FcFalse;
        // a variable font is listed once as itself and once per named instance; keep the instances
        if (FcPatternGetBool(p, FC_VARIABLE, 0, &variable) == FcResultMatch && variable) {
            continue;
        }
        FcChar8 *fmt = nullptr;
        if (FcPatternGetString(p, FC_FONTFORMAT, 0, &fmt) == FcResultMatch && qstrcmp(reinterpret_cast<const char *>(fmt), "PCF") == 0) {
            continue; // X11 bitmap fonts: Windows 7 lists none of these
        }
        Face f;
        fillFace(f, p);
        const QString key = f.file + QLatin1Char('#') + QString::number(f.index) + QLatin1Char('#') + f.style;
        if (f.family.isEmpty() || seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        faces << f;
    }
    if (fs) {
        FcFontSetDestroy(fs);
    }
    FcObjectSetDestroy(os);
    FcPatternDestroy(pat);

    // the hidden ones are rejected by fontconfig, so ask FreeType about them directly
    for (const QString &file : hiddenFiles()) {
        if (!QFileInfo::exists(file)) {
            continue;
        }
        FcFontSet *set = FcFontSetCreate();
        FcFreeTypeQueryAll(reinterpret_cast<const FcChar8 *>(QFile::encodeName(file).constData()), -1, nullptr, nullptr, set);
        for (int i = 0; i < set->nfont; ++i) {
            FcBool variable = FcFalse;
            if (FcPatternGetBool(set->fonts[i], FC_VARIABLE, 0, &variable) == FcResultMatch && variable) {
                continue;
            }
            Face f;
            fillFace(f, set->fonts[i]);
            f.hidden = true;
            if (!f.family.isEmpty()) {
                faces << f;
            }
        }
        FcFontSetDestroy(set);
    }
    return faces;
}

inline QList<Family> families(const QList<Face> &faces)
{
    QHash<QString, int> at;
    QList<Family> out;
    for (const Face &f : faces) {
        auto it = at.find(f.family);
        if (it == at.end()) {
            it = at.insert(f.family, int(out.size()));
            out.append({f.family, {}});
        }
        out[*it].faces << f;
    }
    QCollator c(QLocale(QLocale::Ukrainian));
    c.setNumericMode(true);
    std::sort(out.begin(), out.end(), [&c](const Family &a, const Family &b) {
        return c.compare(a.name, b.name) < 0;
    });
    for (Family &fam : out) {
        // regular first, then by weight and slant
        std::sort(fam.faces.begin(), fam.faces.end(), [](const Face &a, const Face &b) {
            return std::tie(a.slant, a.weight, a.style) < std::tie(b.slant, b.weight, b.style);
        });
        for (int i = 0; i < fam.faces.size(); ++i) {
            if (fam.faces.at(i).weight == FC_WEIGHT_REGULAR && fam.faces.at(i).slant == FC_SLANT_ROMAN) {
                fam.faces.move(i, 0);
                break;
            }
        }
    }
    return out;
}

/* ---------------- drawing one exact face ---------------- */

inline FT_Library ftLibrary()
{
    static FT_Library lib = [] {
        FT_Library l = nullptr;
        FT_Init_FreeType(&l);
        return l;
    }();
    return lib;
}

struct RenderMode {
    bool lcd = false; // subpixel ("ClearType")
    bool bgr = false;
    int hinting = 1; // 0 none (hintnone), 1 slight (hintslight), 2 full (hintfull)
    FT_LcdFilter filter = FT_LCD_FILTER_DEFAULT;
    bool antialias = true;
};

/** Black text of one font file on a transparent (or white, for LCD) image, glyph by glyph */
inline QImage renderText(const QString &file, int index, const QString &text, int px, const RenderMode &mode = {}, QColor color = Qt::black)
{
    FT_Face face = nullptr;
    if (FT_New_Face(ftLibrary(), QFile::encodeName(file).constData(), index, &face) != 0) {
        return {};
    }
    if (FT_Set_Pixel_Sizes(face, 0, px) != 0 && face->num_fixed_sizes > 0) {
        FT_Select_Size(face, 0);
    }
    bool symbolMap = false;
    if (FT_Select_Charmap(face, FT_ENCODING_UNICODE) != 0) {
        symbolMap = FT_Select_Charmap(face, FT_ENCODING_MS_SYMBOL) == 0;
    }
    FT_Int32 load = FT_LOAD_DEFAULT;
    if (!mode.antialias) {
        load |= FT_LOAD_TARGET_MONO;
    } else if (mode.hinting == 0) {
        load |= FT_LOAD_NO_HINTING;
    } else if (mode.hinting == 1) {
        load |= FT_LOAD_TARGET_LIGHT;
    } else if (mode.lcd) {
        load |= FT_LOAD_TARGET_LCD;
    } else {
        load |= FT_LOAD_TARGET_NORMAL;
    }
    const FT_Render_Mode render = !mode.antialias ? FT_RENDER_MODE_MONO : (mode.lcd ? FT_RENDER_MODE_LCD : FT_RENDER_MODE_NORMAL);
    if (mode.lcd) {
        FT_Library_SetLcdFilter(ftLibrary(), mode.filter);
    }

    struct Glyph {
        QImage img;
        int x, y;
    };
    QList<Glyph> glyphs;
    qreal pen = 0;
    const int ascent = int(std::ceil(face->size->metrics.ascender / 64.0));
    const int descent = int(std::ceil(-face->size->metrics.descender / 64.0));
    for (const char32_t ch : text.toStdU32String()) {
        if (ch == 0x200b) {
            pen += px * 0.2;
            continue;
        }
        FT_UInt gi = FT_Get_Char_Index(face, symbolMap ? (0xf000 | ch) : ch);
        if (!gi && symbolMap) {
            gi = FT_Get_Char_Index(face, ch);
        }
        if (FT_Load_Glyph(face, gi, load) != 0 || FT_Render_Glyph(face->glyph, render) != 0) {
            continue;
        }
        const FT_Bitmap &bm = face->glyph->bitmap;
        const int w = mode.lcd ? int(bm.width / 3) : int(bm.width);
        QImage img(qMax(1, w), qMax(1, int(bm.rows)), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        for (unsigned y = 0; y < bm.rows; ++y) {
            const uchar *row = bm.buffer + y * bm.pitch;
            auto *out = reinterpret_cast<QRgb *>(img.scanLine(int(y)));
            for (int x = 0; x < w; ++x) {
                if (mode.lcd) {
                    int r = row[x * 3], g = row[x * 3 + 1], b = row[x * 3 + 2];
                    if (mode.bgr) {
                        std::swap(r, b);
                    }
                    // stored as per-channel coverage; composed onto white below
                    out[x] = qRgba(r, g, b, qMax(r, qMax(g, b)));
                } else if (render == FT_RENDER_MODE_MONO) {
                    const int a = (row[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
                    out[x] = qPremultiply(qRgba(color.red(), color.green(), color.blue(), a));
                } else {
                    out[x] = qPremultiply(qRgba(color.red(), color.green(), color.blue(), row[x]));
                }
            }
        }
        glyphs.append({img, int(std::lround(pen)) + face->glyph->bitmap_left, ascent - face->glyph->bitmap_top});
        pen += face->glyph->advance.x / 64.0;
    }
    FT_Done_Face(face);
    const int width = qMax(1, int(std::ceil(pen)) + 2);
    const int height = qMax(1, ascent + descent);
    QImage out(width, height, QImage::Format_ARGB32_Premultiplied);
    if (mode.lcd) {
        // subpixel text only exists on an opaque background
        out.fill(Qt::white);
        for (const Glyph &g : std::as_const(glyphs)) {
            for (int y = 0; y < g.img.height(); ++y) {
                const int oy = g.y + y;
                if (oy < 0 || oy >= height) {
                    continue;
                }
                const auto *src = reinterpret_cast<const QRgb *>(g.img.constScanLine(y));
                auto *dst = reinterpret_cast<QRgb *>(out.scanLine(oy));
                for (int x = 0; x < g.img.width(); ++x) {
                    const int ox = g.x + x;
                    if (ox < 0 || ox >= width) {
                        continue;
                    }
                    const QRgb d = dst[ox];
                    const QRgb s = src[x];
                    dst[ox] = qRgb(qRed(d) * (255 - qRed(s)) / 255, qGreen(d) * (255 - qGreen(s)) / 255, qBlue(d) * (255 - qBlue(s)) / 255);
                }
            }
        }
    } else {
        out.fill(Qt::transparent);
        QPainter p(&out);
        for (const Glyph &g : std::as_const(glyphs)) {
            p.drawImage(g.x, g.y, g.img);
        }
    }
    return out;
}

} // namespace Win7Fonts

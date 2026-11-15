#pragma once
/*
 * The DVD menu styles of Windows DVD Maker (OmdProject.dll XML "DVDSTYLES", pictures in Shared\DvdStyles) drawn
 * as still menus: the main menu, the scenes pages (2, 4 or 6 scene buttons) and the notes page, plus the
 * highlight pictures of their buttons for the DVD's subpictures.
 *
 * A token's rectangle comes from its Scale (size, 1 = the whole frame) and Offset (centre, -1..1 with y up);
 * animated properties (<Point time value>) are taken where the menu has settled.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "win7i18n.h"

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QStandardPaths>

namespace Dvd
{
inline QString sharedDir()
{
    return QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-dvdmaker"), QStandardPaths::LocateDirectory);
}

/** a file of the styles, its name matched case-insensitively ("DvdStyles\Memories\background.png") */
inline QString styleFile(const QString &rel)
{
    static QHash<QString, QString> cache;
    const auto it = cache.constFind(rel);
    if (it != cache.constEnd()) {
        return *it;
    }
    QString cur = sharedDir();
    for (const QString &part : QString(rel).replace(QLatin1Char('\\'), QLatin1Char('/')).split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        const QDir d(cur);
        QString found;
        for (const QString &n : d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
            if (n.compare(part, Qt::CaseInsensitive) == 0) {
                found = n;
                break;
            }
        }
        if (found.isEmpty()) {
            cache.insert(rel, QString());
            return {};
        }
        cur = d.filePath(found);
    }
    cache.insert(rel, cur);
    return cur;
}

inline QImage styleImage(const QString &rel)
{
    static QHash<QString, QImage> cache;
    auto it = cache.find(rel);
    if (it == cache.end()) {
        QImage im(styleFile(rel));
        it = cache.insert(rel, im.isNull() ? im : im.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    }
    return *it;
}

/** "@..\OmdProject.dll,-208" -> the string of OmdProject.dll.mui */
inline QString omdString(const QString &v)
{
    static const QHash<int, QString> s{{105, QStringLiteral("DVD")},
                                       {106, QStringLiteral("Play")},
                                       {107, QStringLiteral("Scenes")},
                                       {108, QStringLiteral("Notes")},
                                       {109, QStringLiteral("Add your notes here.")},
                                       {200, QStringLiteral("Segoe UI")},
                                       {201, QStringLiteral("Trebuchet MS")},
                                       {202, QStringLiteral("Palatino Linotype")},
                                       {203, QStringLiteral("Segoe Print")},
                                       {204, QStringLiteral("Segoe UI")},
                                       {205, QStringLiteral("Segoe UI")},
                                       {206, QStringLiteral("Corbel")},
                                       {207, QStringLiteral("Georgia")},
                                       {208, QStringLiteral("Constantia")},
                                       {209, QStringLiteral("Segoe UI")},
                                       {300, QStringLiteral("FALSE")},
                                       {301, QStringLiteral("FALSE")},
                                       {302, QStringLiteral("FALSE")},
                                       {303, QStringLiteral("TRUE")},
                                       {304, QStringLiteral("TRUE")},
                                       {305, QStringLiteral("FALSE")},
                                       {306, QStringLiteral("TRUE")},
                                       {307, QStringLiteral("FALSE")},
                                       {308, QStringLiteral("TRUE")},
                                       {309, QStringLiteral("FALSE")}};
    if (!v.startsWith(QLatin1Char('@'))) {
        return v;
    }
    return s.value(-v.section(QLatin1Char(','), 1).toInt(), v);
}

/** "Scene3Button" -> 3 */
inline int sceneNumber(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("(\\d+)"));
    return re.match(name).captured(1).toInt();
}

inline QList<double> numbers(const QString &v)
{
    QList<double> out;
    for (const QString &p : QString(v).replace(QLatin1Char(','), QLatin1Char(' ')).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        out << p.toDouble();
    }
    return out;
}

/** a property's value at time t (ms): plain value="", or keyframes <Point time value> (linear) */
inline QList<double> valueAt(const QDomElement &prop, double t)
{
    if (prop.isNull()) {
        return {};
    }
    if (prop.hasAttribute(QStringLiteral("value"))) {
        return numbers(prop.attribute(QStringLiteral("value")));
    }
    QList<QPair<double, QList<double>>> pts;
    for (QDomElement p = prop.firstChildElement(QStringLiteral("Point")); !p.isNull(); p = p.nextSiblingElement(QStringLiteral("Point"))) {
        pts.append({p.attribute(QStringLiteral("time")).toDouble(), numbers(p.attribute(QStringLiteral("value")))});
    }
    if (pts.isEmpty()) {
        return {};
    }
    if (t <= pts.first().first) {
        return pts.first().second;
    }
    for (int i = 1; i < pts.size(); ++i) {
        if (t <= pts[i].first) {
            const double f = (t - pts[i - 1].first) / qMax(1e-9, pts[i].first - pts[i - 1].first);
            QList<double> v;
            for (int k = 0; k < qMin(pts[i].second.size(), pts[i - 1].second.size()); ++k) {
                v << pts[i - 1].second[k] + (pts[i].second[k] - pts[i - 1].second[k]) * f;
            }
            return v;
        }
    }
    return pts.last().second;
}

/** the rectangle of a token's Properties in a frame of size s */
inline QRectF tokenRect(const QDomElement &props, double t, QSizeF s)
{
    const QList<double> sc = valueAt(props.firstChildElement(QStringLiteral("Scale")), t);
    const QList<double> off = valueAt(props.firstChildElement(QStringLiteral("Offset")), t);
    const double w = (sc.size() > 0 ? sc[0] : 1) * s.width();
    const double h = (sc.size() > 1 ? sc[1] : 1) * s.height();
    const double cx = (0.5 + (off.size() > 0 ? off[0] : 0) / 2) * s.width();
    const double cy = (0.5 - (off.size() > 1 ? off[1] : 0) / 2) * s.height();
    return QRectF(cx - w / 2, cy - h / 2, w, h);
}

inline double tokenAlpha(const QDomElement &props, double t)
{
    const QList<double> a = valueAt(props.firstChildElement(QStringLiteral("Alpha")), t);
    return a.isEmpty() ? 1.0 : qBound(0.0, a[0], 1.0);
}

/** "0.568290, 0.396701, 0.225406, 0.078125" (left, top, width, height of the frame) */
inline QRectF fracRect(const QString &v, QSizeF s)
{
    const QList<double> n = numbers(v);
    if (n.size() < 4) {
        return {};
    }
    return QRectF(n[0] * s.width(), n[1] * s.height(), n[2] * s.width(), n[3] * s.height());
}

/** a picture covering rect r (cropped to its proportions) */
inline void drawFill(QPainter &p, const QRectF &r, const QImage &im)
{
    if (im.isNull()) {
        p.fillRect(r, Qt::black);
        return;
    }
    const double s = qMax(r.width() / im.width(), r.height() / im.height());
    const QSizeF sz(r.width() / s, r.height() / s);
    const QRectF src((im.width() - sz.width()) / 2, (im.height() - sz.height()) / 2, sz.width(), sz.height());
    p.drawImage(r, im, src);
}

/** the picture im (a video frame) inside rect r, shown where the mask is opaque */
inline void drawMasked(QPainter &p, const QRectF &r, const QImage &im, const QImage &mask, double alpha = 1)
{
    const QSize sz = r.size().toSize().expandedTo(QSize(1, 1)).boundedTo(QSize(4096, 4096));
    QImage layer(sz, QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    {
        QPainter lp(&layer);
        lp.setRenderHint(QPainter::SmoothPixmapTransform);
        drawFill(lp, QRectF(QPointF(), r.size()), im);
        if (!mask.isNull()) {
            lp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            // a mask is either transparent where nothing shows, or grey without transparency (white = shown)
            QImage m = mask.scaled(sz, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            bool alphaMask = false;
            if (m.hasAlphaChannel()) {
                for (int y = 0; y < m.height() && !alphaMask; y += qMax(1, m.height() / 16)) {
                    for (int x = 0; x < m.width(); x += qMax(1, m.width() / 16)) {
                        if (m.pixelColor(x, y).alpha() < 250) {
                            alphaMask = true;
                            break;
                        }
                    }
                }
            }
            if (!alphaMask) {
                QImage a(m.size(), QImage::Format_ARGB32_Premultiplied);
                for (int y = 0; y < m.height(); ++y) {
                    const QRgb *src = reinterpret_cast<const QRgb *>(m.constScanLine(y));
                    QRgb *dst = reinterpret_cast<QRgb *>(a.scanLine(y));
                    for (int x = 0; x < m.width(); ++x) {
                        const int v = qMin(qGray(src[x]), qAlpha(src[x]));
                        dst[x] = qRgba(v, v, v, v);
                    }
                }
                m = a;
            }
            lp.drawImage(0, 0, m);
        }
    }
    p.setOpacity(alpha);
    p.drawImage(r.topLeft(), layer);
    p.setOpacity(1);
}

struct Texts {
    QString title, play, scenes, notes, notesText;
    QString font;
    bool bold = false, italic = false;
    QColor color;
};

/** one menu style of DVDSTYLES */
struct Style {
    QString guid, name, thumbnail;
    QDomElement el;
    Texts defaults;
    QString sceneButtonToken;
};

class Styles
{
public:
    static Styles &instance()
    {
        static Styles s;
        return s;
    }
    QList<Style> styles;
    QHash<QString, QDomElement> buttons; // DVDButton-* by guid

    const Style *find(const QString &guid) const
    {
        for (const Style &s : styles) {
            if (s.guid == guid) {
                return &s;
            }
        }
        return styles.isEmpty() ? nullptr : &styles.first();
    }

private:
    QDomDocument m_doc;
    Styles()
    {
        QFile f(sharedDir() + QStringLiteral("/dvdstyles.xml"));
        if (!f.open(QIODevice::ReadOnly) || !m_doc.setContent(&f)) {
            return;
        }
        const QDomNodeList btns = m_doc.elementsByTagName(QStringLiteral("DVDButton"));
        for (int i = 0; i < btns.size(); ++i) {
            const QDomElement b = btns.at(i).toElement();
            buttons.insert(b.attribute(QStringLiteral("guid")), b);
        }
        const QDomNodeList list = m_doc.elementsByTagName(QStringLiteral("DVDMenuStyle"));
        for (int i = 0; i < list.size(); ++i) {
            const QDomElement e = list.at(i).toElement();
            auto val = [&e](const char *tag) {
                return omdString(e.firstChildElement(QLatin1String(tag)).attribute(QStringLiteral("value")));
            };
            Style s;
            s.el = e;
            s.guid = e.attribute(QStringLiteral("guid"));
            s.name = e.attribute(QStringLiteral("comment"));
            s.thumbnail = e.firstChildElement(QStringLiteral("StaticThumbnail")).attribute(QStringLiteral("value"));
            s.defaults.title = val("TitleText");
            s.defaults.play = val("PlayButtonText");
            s.defaults.scenes = val("Submenu1ButtonText");
            s.defaults.notes = val("Submenu2ButtonText");
            s.defaults.notesText = val("NotesText");
            s.defaults.font = val("Font");
            s.defaults.bold = val("IsBold") == QLatin1String("TRUE");
            s.defaults.color = QColor(e.firstChildElement(QStringLiteral("FrontColor1")).attribute(QStringLiteral("value"), QStringLiteral("#ffffffff")));
            s.sceneButtonToken = e.firstChildElement(QStringLiteral("SceneButtonTFXToken")).attribute(QStringLiteral("value")).section(QLatin1Char('\\'), -1);
            styles.append(s);
        }
    }
};

/** what is on a menu: the texts, the pictures of the content and the scene thumbnails */
struct MenuContent {
    Texts texts;
    QImage inset;            // foreground video / picture
    QImage background;       // background video / picture
    QList<QImage> scenes;    // thumbnails of this page's scenes
    bool hasNotes = true;
    bool hasPrevious = false, hasNext = false;
};

/** a button of a rendered menu: name and rectangle in the frame */
struct MenuButton {
    QString name;
    QRect rect;
};

class Renderer
{
public:
    /** the menu page "MainMenu", "NotesMenu" or "ScenesMenu2/4/6" of style s, size is the DVD frame (720x576 / 720x480 shown 4:3 or 16:9) */
    static QImage render(const Style &s, const QString &page, const MenuContent &c, QSize size, QList<MenuButton> *buttons = nullptr, QImage *highlight = nullptr)
    {
        QImage img(size, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        const QDomElement menu = s.el.firstChildElement(QStringLiteral("Menus")).firstChildElement(page);
        if (menu.isNull()) {
            return img;
        }
        const double duration = menu.firstChildElement(QStringLiteral("Duration")).attribute(QStringLiteral("value")).toDouble();
        const double t = duration > 0 ? duration * 0.6 : 0; // the menu has settled
        const QSizeF sz(size);
        QPainter p(&img);
        p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
        const QDomElement graph = menu.firstChildElement(QStringLiteral("Graph"));
        int sceneIndex = 0;
        for (QDomElement e = graph.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            const QString tag = e.tagName();
            const QDomElement props = e.firstChildElement(QStringLiteral("Properties"));
            const QString tex = e.firstChildElement(QStringLiteral("Textures")).firstChildElement(QStringLiteral("Source")).attribute(QStringLiteral("value"));
            if (tag == QLatin1String("Token")) {
                const QString kind = e.attribute(QStringLiteral("value")).section(QLatin1Char('\\'), -1);
                const QRectF r = tokenRect(props, t, sz);
                const double a = tokenAlpha(props, t);
                if (kind == QLatin1String("OverlayImage") || kind == QLatin1String("SubpictureImage")) {
                    p.setOpacity(a);
                    p.drawImage(r, styleImage(tex));
                    p.setOpacity(1);
                } else if (kind == QLatin1String("VideoMaskToFill") || kind.startsWith(QLatin1String("VideoMask"))) {
                    drawMasked(p, r, c.inset, styleImage(tex), a);
                } else if (kind == QLatin1String("VideoToFill")) {
                    drawFill(p, QRectF(QPointF(), sz), c.background);
                }
            } else if (tag == QLatin1String("Stream")) {
                // a video stream feeds the next masking token; with none after it, it fills the frame
                const QDomElement items = e.firstChildElement(QStringLiteral("Items"));
                const QString input = items.firstChildElement(QStringLiteral("Input")).attribute(QStringLiteral("value"));
                const QDomElement nextEl = e.nextSiblingElement();
                const bool feedsMask = nextEl.tagName() == QLatin1String("Token") && nextEl.attribute(QStringLiteral("value")).contains(QLatin1String("Mask"));
                if (!feedsMask) {
                    drawFill(p, QRectF(QPointF(), sz), input == QLatin1String("BackgroundVideo") ? c.background : c.inset);
                }
            } else if (tag == QLatin1String("NavigationButton")) {
                const QString which = e.attribute(QStringLiteral("value"));
                if ((which == QLatin1String("NextMenuButton") && !c.hasNext) || (which == QLatin1String("PreviousMenuButton") && !c.hasPrevious)) {
                    continue;
                }
                p.setOpacity(tokenAlpha(props, t));
                p.drawImage(tokenRect(props, t, sz), styleImage(tex));
                p.setOpacity(1);
            } else if (tag == QLatin1String("SceneButton")) {
                const int n = sceneNumber(e.attribute(QStringLiteral("value"))) - 1; // "Scene3Button"
                if (n >= c.scenes.size()) {
                    continue;
                }
                drawSceneButton(p, s, tokenRect(props, t, sz), c.scenes.value(n));
                ++sceneIndex;
            } else if (tag == QLatin1String("ButtonText") || tag == QLatin1String("Text")) {
                const QString which = e.attribute(QStringLiteral("value"));
                QString text = which == QLatin1String("Title") ? c.texts.title
                    : which == QLatin1String("PlayButton")     ? c.texts.play
                    : which == QLatin1String("Submenu1Button") ? c.texts.scenes
                    : which == QLatin1String("Submenu2Button") ? (c.hasNotes ? c.texts.notes : QString())
                    : which == QLatin1String("Notes")          ? c.texts.notesText
                                                               : QString();
                if (text.isEmpty()) {
                    continue;
                }
                const QRectF box = fracRect(props.firstChildElement(QStringLiteral("BoundingRect")).attribute(QStringLiteral("value")), sz);
                const double pt = props.firstChildElement(QStringLiteral("FontSize")).attribute(QStringLiteral("value")).toDouble();
                QFont f(c.texts.font.isEmpty() ? s.defaults.font : c.texts.font);
                f.setBold(c.texts.bold);
                f.setItalic(c.texts.italic);
                f.setPixelSize(qMax(8, int(pt * sz.height() / 380)));
                const bool autoFit = props.firstChildElement(QStringLiteral("ResizingTechnique")).attribute(QStringLiteral("value")) == QLatin1String("AutoResize");
                while (autoFit && f.pixelSize() > 10 && QFontMetrics(f).horizontalAdvance(text) > box.width()) {
                    f.setPixelSize(f.pixelSize() - 1);
                }
                const QString align = props.firstChildElement(QStringLiteral("HorizontalAlignment")).attribute(QStringLiteral("value"));
                const int flags = (align == QLatin1String("Center") ? Qt::AlignHCenter : align == QLatin1String("Right") ? Qt::AlignRight : Qt::AlignLeft)
                    | Qt::AlignVCenter | (which == QLatin1String("Notes") ? Qt::TextWordWrap : 0);
                QRectF tb = box;
                if (which == QLatin1String("Notes")) {
                    tb.setHeight(qMax(box.height(), sz.height() * 0.5));
                }
                p.setFont(f);
                p.setPen(QColor(0, 0, 0, 120)); // the title's shadow
                p.drawText(tb.translated(1.5, 1.5), flags, text);
                p.setPen(c.texts.color.isValid() ? c.texts.color : s.defaults.color);
                p.drawText(tb, flags, text);
            }
        }
        p.end();

        // the buttons: where they are, and the highlight shown on the selected one
        if (buttons || highlight) {
            QList<MenuButton> list;
            const QDomElement locs = menu.firstChildElement(QStringLiteral("ButtonLocations"));
            for (QDomElement b = locs.firstChildElement(); !b.isNull(); b = b.nextSiblingElement()) {
                const QString name = b.tagName();
                if ((name == QLatin1String("Submenu2Button") && !c.hasNotes) || (name == QLatin1String("NextMenuButton") && !c.hasNext)
                    || (name == QLatin1String("PreviousMenuButton") && !c.hasPrevious)) {
                    continue;
                }
                if (name.startsWith(QLatin1String("Scene")) && sceneNumber(name) > c.scenes.size()) {
                    continue;
                }
                const QRect r = fracRect(b.attribute(QStringLiteral("value")), sz).toAlignedRect().intersected(QRect(QPoint(), size));
                list.append({name, QRect(r.x() & ~1, r.y() & ~1, r.width() & ~1, r.height() & ~1)}); // spumux wants even coordinates
            }
            // the first button is the one a DVD player selects: the scenes (or Play) before the arrows
            std::stable_sort(list.begin(), list.end(), [](const MenuButton &a, const MenuButton &b) {
                auto rank = [](const QString &n) {
                    return n.startsWith(QLatin1String("Scene")) || n == QLatin1String("PlayButton") ? 0 : n.endsWith(QLatin1String("MenuButton")) ? 2 : 1;
                };
                return rank(a.name) < rank(b.name);
            });
            if (buttons) {
                *buttons = list;
            }
            if (highlight) {
                *highlight = highlightImage(s, menu, list, size, t);
            }
        }
        return img;
    }

    /** a scene button: the scene's picture through the style's button (mask, underlay, overlay) */
    static void drawSceneButton(QPainter &p, const Style &s, const QRectF &r, const QImage &scene)
    {
        const QDomElement b = Styles::instance().buttons.value(s.sceneButtonToken);
        QStringList tex;
        for (QDomElement e = b.firstChildElement(QStringLiteral("GraphTextures")).firstChildElement(QStringLiteral("Source")); !e.isNull(); e = e.nextSiblingElement(QStringLiteral("Source"))) {
            tex << e.attribute(QStringLiteral("value"));
        }
        const QString technique = b.firstChildElement(QStringLiteral("Technique")).attribute(QStringLiteral("value"));
        // the inputs: 1 = the video, then the textures in order; ImageMask / ImageOverlay / ImageUnderlay give their numbers
        auto input = [&](const char *what) -> QImage {
            const QDomElement e = b.firstChildElement(QLatin1String(what));
            if (e.isNull()) {
                return {};
            }
            const int n = e.attribute(QStringLiteral("value")).toInt() - 2;
            return n >= 0 && n < tex.size() ? styleImage(tex[n]) : QImage();
        };
        const QImage under = technique.contains(QLatin1String("Underlay")) ? input("ImageUnderlay") : QImage();
        const QImage over = technique.contains(QLatin1String("Overlay")) ? input("ImageOverlay") : QImage();
        const QImage mask = input("ImageMask");
        if (!under.isNull()) {
            p.drawImage(r, under);
        }
        drawMasked(p, r, scene, mask);
        if (!over.isNull()) {
            p.drawImage(r, over);
        }
    }

    /** the highlight of every button (one colour, transparent elsewhere: a DVD subpicture has four colours) */
    static QImage highlightImage(const Style &s, const QDomElement &menu, const QList<MenuButton> &buttons, QSize size, double t)
    {
        QImage hl(size, QImage::Format_ARGB32);
        hl.fill(Qt::transparent);
        QPainter p(&hl);
        QColor color(s.el.firstChildElement(QStringLiteral("SubpictureSelectionColor")).attribute(QStringLiteral("value"), QStringLiteral("#0060e3")));
        const QDomElement sub = menu.firstChildElement(QStringLiteral("Subpicture"));
        const QDomElement sceneBtn = Styles::instance().buttons.value(s.sceneButtonToken);
        const QString sceneHl = sceneBtn.firstChildElement(QStringLiteral("SubpictureTextures")).firstChildElement(QStringLiteral("Source")).attribute(QStringLiteral("value"));
        for (QDomElement e = sub.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            const QString name = e.attribute(QStringLiteral("value"));
            if (std::none_of(buttons.cbegin(), buttons.cend(), [&name](const MenuButton &b) { return b.name == name; })) {
                continue;
            }
            QString tex = e.firstChildElement(QStringLiteral("Textures")).firstChildElement(QStringLiteral("Source")).attribute(QStringLiteral("value"));
            if (tex.isEmpty() && e.tagName() == QLatin1String("SceneButton")) {
                tex = sceneHl;
            }
            const QImage im = styleImage(tex);
            const QRectF r = tokenRect(e.firstChildElement(QStringLiteral("Properties")), t, QSizeF(size));
            if (im.isNull()) {
                p.setPen(QPen(color, 3));
                p.drawRect(r.adjusted(2, 2, -2, -2));
                continue;
            }
            // the shape of the highlight picture in the one colour
            QImage shape = im.scaled(r.size().toSize().expandedTo(QSize(1, 1)), Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < shape.height(); ++y) {
                QRgb *line = reinterpret_cast<QRgb *>(shape.scanLine(y));
                for (int x = 0; x < shape.width(); ++x) {
                    const int a = qMax(qAlpha(line[x]) > 0 ? qAlpha(line[x]) : 0, 0);
                    line[x] = a > 96 ? qRgba(color.red(), color.green(), color.blue(), 255) : 0;
                }
            }
            p.drawImage(r.topLeft(), shape);
        }
        return hl;
    }
};
}

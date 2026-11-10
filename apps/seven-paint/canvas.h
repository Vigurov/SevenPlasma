#pragma once
/*
 * The drawing surface of Windows 7 Paint: the white picture at (5,5) on the blue-grey work area with its 8 px shadow and
 * the three hollow 5 px resize handles, all the tools (selection, free-form selection, pencil, fill, text, eraser,
 * colour picker, magnifier, the nine brushes, the 23 shapes with their outline/fill styles), zoom and undo.
 * Brush textures are mspaint.exe IMAGE resources (~/.local/share/seven-paint/tex501xx.png).
 */
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QScreen>
#include <QGuiApplication>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextEdit>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>
#include <functional>

namespace Paint
{
enum class Tool { Select, FreeSelect, Pencil, Fill, Text, Eraser, Picker, Magnifier, Brush, Shape };
enum class Style { None, Solid, Crayon, Marker, Oil, NaturalPencil, Watercolor };
enum class BrushKind { Brush, Calligraphy1, Calligraphy2, Airbrush, Oil, Crayon, Marker, NaturalPencil, Watercolor };
enum class Shape {
    Line, Curve, Oval, Rect, RoundRect, Polygon, Triangle, RightTriangle, Diamond, Pentagon, Hexagon, ArrowRight, ArrowLeft,
    ArrowUp, ArrowDown, Star4, Star5, Star6, RoundCallout, OvalCallout, CloudCallout, Heart, Lightning
};

inline QImage texture(int id)
{
    // dark texel = paint; turned into an alpha mask once
    static QHash<int, QImage> cache;
    auto it = cache.find(id);
    if (it == cache.end()) {
        QImage src(QDir::homePath() + QStringLiteral("/.local/share/seven-paint/tex%1.png").arg(id));
        QImage m(src.size(), QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < src.height(); ++y) {
            for (int x = 0; x < src.width(); ++x) {
                const int a = 255 - qGray(src.pixel(x, y));
                m.setPixel(x, y, qRgba(a, a, a, a));
            }
        }
        it = cache.insert(id, m);
    }
    return *it;
}

/** what a style does to a layer painted in plain colour: a texture mask and an opacity */
struct Look {
    int tex = 0;
    qreal opacity = 1;
    qreal texGain = 1; // >1 makes the texture denser
};
inline Look lookFor(Style s)
{
    switch (s) {
    case Style::Crayon:
        return {50098, 1, 1.6};
    case Style::Marker:
        return {0, 0.55};
    case Style::Oil:
        return {50099, 1, 2.6};
    case Style::NaturalPencil:
        return {50146, 0.9, 1.4};
    case Style::Watercolor:
        return {50100, 0.4, 2.2};
    default:
        return {};
    }
}
inline Look lookFor(BrushKind b)
{
    switch (b) {
    case BrushKind::Oil:
        return lookFor(Style::Oil);
    case BrushKind::Crayon:
        return lookFor(Style::Crayon);
    case BrushKind::Marker:
        return lookFor(Style::Marker);
    case BrushKind::NaturalPencil:
        return lookFor(Style::NaturalPencil);
    case BrushKind::Watercolor:
        return lookFor(Style::Watercolor);
    default:
        return {};
    }
}

/** paints `layer` (plain colour on transparent) over `dst` through the look's texture and opacity */
inline void composite(QImage &dst, const QImage &layer, const Look &look, const QRect &area)
{
    QRect r = area.intersected(dst.rect());
    if (r.isEmpty()) {
        return;
    }
    QImage part = layer.copy(r);
    if (look.tex) {
        QPainter tp(&part);
        tp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        QImage t = texture(look.tex);
        if (look.texGain != 1) {
            t = t.copy();
            for (int y = 0; y < t.height(); ++y) {
                QRgb *l = reinterpret_cast<QRgb *>(t.scanLine(y));
                for (int x = 0; x < t.width(); ++x) {
                    const int a = qMin(255, int(qAlpha(l[x]) * look.texGain));
                    l[x] = qRgba(a, a, a, a);
                }
            }
        }
        QBrush b(t);
        b.setTransform(QTransform::fromTranslate(-r.x(), -r.y()));
        tp.fillRect(part.rect(), b);
    }
    QPainter p(&dst);
    p.setOpacity(look.opacity);
    p.drawImage(r.topLeft(), part);
}

inline QPainterPath polygonPath(const QRectF &r, int n, qreal startDeg, qreal inner = 0)
{
    QPainterPath path;
    const QPointF c = r.center();
    const int pts = inner > 0 ? n * 2 : n;
    for (int i = 0; i < pts; ++i) {
        const qreal a = qDegreesToRadians(startDeg + 360.0 * i / pts);
        const qreal k = (inner > 0 && i % 2) ? inner : 1;
        const QPointF p(c.x() + std::cos(a) * r.width() / 2 * k, c.y() + std::sin(a) * r.height() / 2 * k);
        i ? path.lineTo(p) : path.moveTo(p);
    }
    path.closeSubpath();
    return path;
}

/** the outline of a shape in `r` (line and curve use the points instead) */
inline QPainterPath shapePath(Shape s, const QRectF &rIn, const QList<QPointF> &pts)
{
    const QRectF r = rIn.normalized();
    const qreal l = r.left(), t = r.top(), w = r.width(), h = r.height(), R = r.right(), B = r.bottom();
    QPainterPath p;
    auto poly = [&](std::initializer_list<QPointF> list) {
        QPolygonF pg(list);
        p.addPolygon(pg);
        p.closeSubpath();
    };
    switch (s) {
    case Shape::Line:
        if (pts.size() >= 2) {
            p.moveTo(pts[0]);
            p.lineTo(pts[1]);
        }
        break;
    case Shape::Curve:
        if (pts.size() >= 2) {
            p.moveTo(pts[0]);
            const QPointF c1 = pts.value(2, pts[0]);
            const QPointF c2 = pts.value(3, pts.value(2, pts[1]));
            p.cubicTo(c1, c2, pts[1]);
        }
        break;
    case Shape::Polygon:
        if (!pts.isEmpty()) {
            p.addPolygon(QPolygonF(pts.toVector()));
        }
        break;
    case Shape::Oval:
        p.addEllipse(r);
        break;
    case Shape::Rect:
        p.addRect(r);
        break;
    case Shape::RoundRect:
        p.addRoundedRect(r, qMin(w, h) / 6, qMin(w, h) / 6);
        break;
    case Shape::Triangle:
        poly({{l + w / 2, t}, {R, B}, {l, B}});
        break;
    case Shape::RightTriangle:
        poly({{l, t}, {R, B}, {l, B}});
        break;
    case Shape::Diamond:
        poly({{l + w / 2, t}, {R, t + h / 2}, {l + w / 2, B}, {l, t + h / 2}});
        break;
    case Shape::Pentagon:
        poly({{l + w / 2, t}, {R, t + h * 0.38}, {l + w * 0.81, B}, {l + w * 0.19, B}, {l, t + h * 0.38}});
        break;
    case Shape::Hexagon:
        poly({{l + w / 4, t}, {l + w * 3 / 4, t}, {R, t + h / 2}, {l + w * 3 / 4, B}, {l + w / 4, B}, {l, t + h / 2}});
        break;
    case Shape::ArrowRight:
        poly({{l, t + h / 4}, {l + w / 2, t + h / 4}, {l + w / 2, t}, {R, t + h / 2}, {l + w / 2, B}, {l + w / 2, B - h / 4}, {l, B - h / 4}});
        break;
    case Shape::ArrowLeft:
        poly({{R, t + h / 4}, {l + w / 2, t + h / 4}, {l + w / 2, t}, {l, t + h / 2}, {l + w / 2, B}, {l + w / 2, B - h / 4}, {R, B - h / 4}});
        break;
    case Shape::ArrowUp:
        poly({{l + w / 4, B}, {l + w / 4, t + h / 2}, {l, t + h / 2}, {l + w / 2, t}, {R, t + h / 2}, {R - w / 4, t + h / 2}, {R - w / 4, B}});
        break;
    case Shape::ArrowDown:
        poly({{l + w / 4, t}, {l + w / 4, t + h / 2}, {l, t + h / 2}, {l + w / 2, B}, {R, t + h / 2}, {R - w / 4, t + h / 2}, {R - w / 4, t}});
        break;
    case Shape::Star4:
        p = polygonPath(r, 4, -90, 0.38);
        break;
    case Shape::Star5:
        p = polygonPath(r, 5, -90, 0.4);
        break;
    case Shape::Star6:
        p = polygonPath(r, 6, -90, 0.58);
        break;
    case Shape::RoundCallout: {
        const QRectF body(l, t, w, h * 0.75);
        QPainterPath b;
        b.addRoundedRect(body, qMin(w, h) / 8, qMin(w, h) / 8);
        QPainterPath tail;
        tail.addPolygon(QPolygonF({QPointF(l + w * 0.2, t + h * 0.7), QPointF(l + w * 0.12, B), QPointF(l + w * 0.4, t + h * 0.7)}));
        p = b.united(tail);
        break;
    }
    case Shape::OvalCallout: {
        QPainterPath b;
        b.addEllipse(QRectF(l, t, w, h * 0.78));
        QPainterPath tail;
        tail.addPolygon(QPolygonF({QPointF(l + w * 0.2, t + h * 0.6), QPointF(l + w * 0.1, B), QPointF(l + w * 0.42, t + h * 0.7)}));
        p = b.united(tail);
        break;
    }
    case Shape::CloudCallout: {
        QPainterPath b;
        const QRectF body(l + w * 0.05, t, w * 0.95, h * 0.72);
        b.addEllipse(body.adjusted(body.width() * 0.08, body.height() * 0.12, -body.width() * 0.08, -body.height() * 0.12));
        for (int i = 0; i < 9; ++i) {
            const qreal a = 2 * M_PI * i / 9;
            const QPointF c(body.center().x() + std::cos(a) * body.width() * 0.4, body.center().y() + std::sin(a) * body.height() * 0.38);
            QPainterPath puff;
            puff.addEllipse(c, body.width() * 0.16, body.height() * 0.2);
            b = b.united(puff);
        }
        b.addEllipse(QRectF(l + w * 0.12, t + h * 0.76, w * 0.08, h * 0.08));
        b.addEllipse(QRectF(l + w * 0.04, t + h * 0.9, w * 0.05, h * 0.06));
        p = b;
        break;
    }
    case Shape::Heart:
        p.moveTo(l + w / 2, t + h * 0.25);
        p.cubicTo(l + w / 2, t, l, t, l, t + h * 0.3);
        p.cubicTo(l, t + h * 0.6, l + w / 2, t + h * 0.75, l + w / 2, B);
        p.cubicTo(l + w / 2, t + h * 0.75, R, t + h * 0.6, R, t + h * 0.3);
        p.cubicTo(R, t, l + w / 2, t, l + w / 2, t + h * 0.25);
        p.closeSubpath();
        break;
    case Shape::Lightning:
        poly({{l + w * 0.38, t}, {l + w * 0.62, t + h * 0.3}, {l + w * 0.53, t + h * 0.35}, {l + w * 0.8, t + h * 0.6}, {l + w * 0.7, t + h * 0.65},
              {R, B}, {l + w * 0.45, t + h * 0.72}, {l + w * 0.56, t + h * 0.67}, {l + w * 0.2, t + h * 0.45}, {l + w * 0.32, t + h * 0.4}, {l, t + h * 0.15}});
        break;
    }
    return p;
}

inline bool isOpenShape(Shape s)
{
    return s == Shape::Line || s == Shape::Curve;
}

class Canvas : public QWidget
{
public:
    static constexpr int M = 5; // picture offset in the work area

    // state the ribbon sets
    Tool tool = Tool::Pencil;
    BrushKind brush = BrushKind::Brush;
    Shape shape = Shape::Line;
    Style outline = Style::Solid, fill = Style::None;
    QColor color1 = Qt::black, color2 = Qt::white;
    bool transparentSelection = false;
    bool gridlines = false;
    int sizeIndex[10] = {0, 0, 0, 0, 0, 2, 0, 0, 2, 0}; // per Tool

    // notifications
    std::function<void(const QPoint &)> cursorMoved; // (-1,-1) when outside
    std::function<void(const QSize &)> selectionChanged; // empty = none
    std::function<void()> imageChanged; // size or contents
    std::function<void(bool left, const QColor &)> colorPicked;
    std::function<void()> zoomChanged;
    std::function<void(bool)> textEditing;
    std::function<void()> undoChanged;
    std::function<void()> shapeSettled; // a drawn shape stopped being editable

    explicit Canvas(QScrollArea *area)
        : QWidget(area)
        , m_area(area)
    {
        setMouseTracking(true);
        setAttribute(Qt::WA_OpaquePaintEvent);
        const QSize scr = QGuiApplication::primaryScreen()->size();
        m_img = QImage(scr.width() * 3 / 5, scr.height() * 3 / 5, QImage::Format_ARGB32_Premultiplied);
        m_img.fill(Qt::white);
        m_airTimer.setInterval(30);
        QObject::connect(&m_airTimer, &QTimer::timeout, [this] {
            airburst(m_last);
        });
        updateSize();
    }

    static QList<int> sizesFor(Tool t, BrushKind b)
    {
        switch (t) {
        case Tool::Pencil:
            return {1, 2, 3, 4};
        case Tool::Eraser:
            return {4, 6, 8, 10};
        case Tool::Brush:
            Q_UNUSED(b);
            return {1, 3, 5, 8};
        case Tool::Shape:
        case Tool::Text:
        case Tool::Select:
        case Tool::FreeSelect:
        default:
            return {1, 3, 5, 8};
        }
    }
    QList<int> sizes() const
    {
        return sizesFor(sizeTool(), brush);
    }
    Tool sizeTool() const
    {
        return tool == Tool::Select || tool == Tool::FreeSelect || tool == Tool::Fill || tool == Tool::Text || tool == Tool::Picker
                || tool == Tool::Magnifier
            ? Tool::Shape
            : tool;
    }
    int &sizeIdx()
    {
        return sizeIndex[int(sizeTool())];
    }
    int penSize() const
    {
        return sizes().value(sizeIndex[int(sizeTool())], 1);
    }
    bool sizeAvailable() const
    {
        return tool == Tool::Pencil || tool == Tool::Eraser || tool == Tool::Brush || tool == Tool::Shape;
    }

    const QImage &image() const
    {
        return m_img;
    }
    void setImage(const QImage &img, bool clearUndo)
    {
        settle();
        m_img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (clearUndo) {
            m_undo.clear();
            m_redo.clear();
            if (undoChanged) {
                undoChanged();
            }
        }
        updateSize();
        update();
        if (imageChanged) {
            imageChanged();
        }
    }
    /** the picture as it would be saved, with floating selection / shape / text merged */
    QImage flattened()
    {
        settle();
        return m_img;
    }

    // ---- undo
    void pushUndo()
    {
        m_undo.append(m_img);
        if (m_undo.size() > 50) {
            m_undo.removeFirst();
        }
        m_redo.clear();
        if (undoChanged) {
            undoChanged();
        }
    }
    bool canUndo() const
    {
        return !m_undo.isEmpty() || m_hasShape || m_hasSel;
    }
    bool canRedo() const
    {
        return !m_redo.isEmpty();
    }
    void undo()
    {
        if (m_hasShape) {
            m_hasShape = false;
            m_overlay = QImage();
            update();
            return;
        }
        dropSelection(false);
        if (m_undo.isEmpty()) {
            return;
        }
        m_redo.append(m_img);
        m_img = m_undo.takeLast();
        afterHistory();
    }
    void redo()
    {
        settle();
        if (m_redo.isEmpty()) {
            return;
        }
        m_undo.append(m_img);
        m_img = m_redo.takeLast();
        afterHistory();
    }

    // ---- zoom
    qreal zoom() const
    {
        return m_zoom;
    }
    static QList<qreal> zoomSteps()
    {
        return {0.125, 0.25, 0.5, 1, 2, 3, 4, 5, 6, 7, 8};
    }
    void setZoom(qreal z, QPointF anchorImg = QPointF(-1, -1))
    {
        z = qBound(0.125, z, 8.0);
        if (qFuzzyCompare(z, m_zoom)) {
            return;
        }
        const QSize vp = m_area->viewport()->size();
        if (anchorImg.x() < 0) {
            anchorImg = toImage(QPointF(m_area->horizontalScrollBar()->value() + vp.width() / 2.0, m_area->verticalScrollBar()->value() + vp.height() / 2.0));
        }
        const QPointF before = toWidget(anchorImg) - QPointF(m_area->horizontalScrollBar()->value(), m_area->verticalScrollBar()->value());
        m_zoom = z;
        updateSize();
        const QPointF after = toWidget(anchorImg);
        m_area->horizontalScrollBar()->setValue(int(after.x() - before.x()));
        m_area->verticalScrollBar()->setValue(int(after.y() - before.y()));
        if (m_text) {
            placeText();
        }
        update();
        if (zoomChanged) {
            zoomChanged();
        }
    }
    void zoomIn()
    {
        for (qreal s : zoomSteps()) {
            if (s > m_zoom + 0.001) {
                setZoom(s);
                return;
            }
        }
    }
    void zoomOut()
    {
        const auto st = zoomSteps();
        for (int i = st.size() - 1; i >= 0; --i) {
            if (st[i] < m_zoom - 0.001) {
                setZoom(st[i]);
                return;
            }
        }
    }

    // ---- selection commands
    bool hasSelection() const
    {
        return m_hasSel;
    }
    void selectAll()
    {
        setTool(Tool::Select);
        m_selRect = m_img.rect();
        m_selPath = QPainterPath();
        m_hasSel = true;
        m_floating = false;
        notifySel();
        update();
    }
    void invertSelection()
    {
        if (!m_hasSel) {
            selectAll();
            return;
        }
        lift();
        QPainterPath all;
        all.addRect(m_img.rect());
        QPainterPath cur;
        cur.addRect(QRectF(m_floatPos, m_float.size()));
        // drop the old selection where it is, select the rest
        commitFloat();
        m_selPath = all.subtracted(cur);
        m_selRect = m_img.rect();
        m_hasSel = true;
        m_floating = false;
        notifySel();
        update();
    }
    void deleteSelection()
    {
        if (!m_hasSel) {
            return;
        }
        lift();
        m_float = QImage();
        m_hasSel = false;
        m_floating = false;
        notifySel();
        update();
        changed();
    }
    void copy()
    {
        if (!m_hasSel) {
            return;
        }
        QApplication::clipboard()->setImage(selectionImage());
    }
    void cut()
    {
        if (!m_hasSel) {
            return;
        }
        copy();
        deleteSelection();
    }
    void paste(const QImage &imgIn = QImage())
    {
        QImage img = imgIn.isNull() ? QApplication::clipboard()->image() : imgIn;
        if (img.isNull()) {
            return;
        }
        settle();
        pushUndo();
        if (img.width() > m_img.width() || img.height() > m_img.height()) {
            // Paint grows the picture to fit what is pasted
            QImage big(qMax(img.width(), m_img.width()), qMax(img.height(), m_img.height()), QImage::Format_ARGB32_Premultiplied);
            big.fill(color2);
            QPainter(&big).drawImage(0, 0, m_img);
            m_img = big;
            updateSize();
        }
        setTool(Tool::Select);
        m_float = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const QPoint tl = toImage(QPointF(m_area->horizontalScrollBar()->value(), m_area->verticalScrollBar()->value())).toPoint();
        m_floatPos = QPoint(qMax(0, tl.x()), qMax(0, tl.y()));
        m_selRect = QRect(m_floatPos, m_float.size());
        m_selPath = QPainterPath();
        m_hasSel = true;
        m_floating = true;
        notifySel();
        update();
        changed();
    }
    void crop()
    {
        if (!m_hasSel) {
            return;
        }
        QImage s = selectionImage();
        settle();
        pushUndo();
        m_img = QImage(s.size(), QImage::Format_ARGB32_Premultiplied);
        m_img.fill(color2);
        QPainter(&m_img).drawImage(0, 0, s);
        updateSize();
        update();
        changed();
    }
    /** applies `f` to the selection when there is one, else to the whole picture */
    void transform(const std::function<QImage(const QImage &)> &f)
    {
        if (m_hasSel) {
            lift();
            m_float = f(m_float).convertToFormat(QImage::Format_ARGB32_Premultiplied);
            m_selRect = QRect(m_floatPos, m_float.size());
            notifySel();
            update();
            changed();
            return;
        }
        settle();
        pushUndo();
        m_img = f(m_img).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        updateSize();
        update();
        changed();
    }
    void resizeCanvas(const QSize &s)
    {
        settle();
        pushUndo();
        QImage n(s, QImage::Format_ARGB32_Premultiplied);
        n.fill(color2);
        QPainter(&n).drawImage(0, 0, m_img);
        m_img = n;
        updateSize();
        update();
        changed();
    }

    void setTool(Tool t)
    {
        if (t != tool) {
            if (t != Tool::Select && t != Tool::FreeSelect) {
                dropSelection(true);
            }
            settleShape();
            commitText();
        }
        if (t != Tool::Picker) {
            m_beforePicker = t;
        }
        tool = t;
        updateCursor();
        update();
    }
    /** anything floating (selection, a shape being edited, text) goes into the picture */
    void settle()
    {
        dropSelection(true);
        settleShape();
        commitText();
    }
    void settleShape()
    {
        if (!m_hasShape) {
            return;
        }
        renderShape();
        pushUndo();
        QPainter(&m_img).drawImage(0, 0, m_overlay);
        m_hasShape = false;
        m_polyOpen = false;
        m_overlay = QImage();
        update();
        changed();
        if (shapeSettled) {
            shapeSettled();
        }
    }
    void restyleShape()
    {
        if (m_hasShape) {
            renderShape();
            update();
        }
        if (m_text) {
            styleText();
        }
    }

    // ---- text
    bool textOpaque = false;
    QFont textFont = QFont(QStringLiteral("Calibri"), 11);
    QTextEdit *textEdit() const
    {
        return m_text;
    }
    void styleText()
    {
        if (!m_text) {
            return;
        }
        QFont f = textFont;
        f.setPointSizeF(textFont.pointSizeF() * m_zoom);
        m_text->setFont(f);
        QTextCursor c = m_text->textCursor();
        m_text->selectAll();
        m_text->setCurrentFont(f);
        m_text->setTextColor(color1);
        m_text->setTextCursor(c);
        QPalette pal = m_text->palette();
        pal.setColor(QPalette::Base, textOpaque ? color2 : Qt::transparent);
        pal.setColor(QPalette::Text, color1);
        m_text->setPalette(pal);
        m_text->viewport()->setAutoFillBackground(textOpaque);
        update();
    }

protected:
    QPointF toImage(const QPointF &w) const
    {
        return (w - QPointF(M, M)) / m_zoom;
    }
    QPointF toWidget(const QPointF &i) const
    {
        return i * m_zoom + QPointF(M, M);
    }
    QRectF toWidget(const QRectF &r) const
    {
        return QRectF(toWidget(r.topLeft()), r.size() * m_zoom);
    }
    QRect picRect() const
    {
        return QRect(M, M, qRound(m_img.width() * m_zoom), qRound(m_img.height() * m_zoom));
    }
public:
    void updateSizePublic()
    {
        updateSize();
        update();
    }

protected:
    void updateSize()
    {
        const QRect r = picRect();
        const QSize vp = m_area->viewport()->size();
        resize(qMax(vp.width(), r.right() + 1 + 20), qMax(vp.height(), r.bottom() + 1 + 20));
    }
    void resizeEvent(QResizeEvent *) override
    {
    }
    void afterHistory()
    {
        updateSize();
        update();
        if (imageChanged) {
            imageChanged();
        }
        if (undoChanged) {
            undoChanged();
        }
    }
    void changed()
    {
        if (imageChanged) {
            imageChanged();
        }
        if (undoChanged) {
            undoChanged();
        }
    }
    void notifySel()
    {
        if (selectionChanged) {
            selectionChanged(m_hasSel ? (m_floating ? m_float.size() : m_selRect.size()) : QSize());
        }
    }

    // handles of the picture: right middle, bottom middle, bottom right
    QRect picHandle(int i) const
    {
        const QRect r = picRect();
        const QPoint c = i == 0 ? QPoint(r.right() + 1, r.center().y()) : i == 1 ? QPoint(r.center().x(), r.bottom() + 1) : QPoint(r.right() + 1, r.bottom() + 1);
        return QRect(c.x() - (i == 1 ? 2 : 0), c.y() - (i == 0 ? 2 : 0), 5, 5);
    }
    static void drawHandle(QPainter &p, const QRect &h)
    {
        p.fillRect(h, QColor(0x55, 0x55, 0x55));
        p.fillRect(h.adjusted(1, 1, -1, -1), Qt::white);
    }
    static void dashedRect(QPainter &p, const QRectF &r)
    {
        // the 1 px marching-ants frame of selections and text boxes: blue on white dots
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(Qt::white, 1));
        p.drawRect(r);
        QPen dash(QColor(0x33, 0x99, 0xff), 1, Qt::CustomDashLine);
        dash.setDashPattern({4, 4});
        p.setPen(dash);
        p.drawRect(r);
        p.restore();
    }
    QList<QRect> boxHandles(const QRectF &wr) const
    {
        const QRect r = wr.toAlignedRect();
        const int xs[3] = {r.left() - 3, r.center().x() - 2, r.right() - 1};
        const int ys[3] = {r.top() - 3, r.center().y() - 2, r.bottom() - 1};
        QList<QRect> out;
        for (int j = 0; j < 3; ++j) {
            for (int i = 0; i < 3; ++i) {
                if (i != 1 || j != 1) {
                    out.append(QRect(xs[i], ys[j], 5, 5));
                }
            }
        }
        return out; // TL T TR L R BL B BR
    }

    void paintEvent(QPaintEvent *e) override
    {
        QPainter p(this);
        // work area: vertical gradient over the visible part
        const int top = m_area->verticalScrollBar()->value();
        const int vh = m_area->viewport()->height();
        QLinearGradient bg(0, top, 0, top + vh);
        bg.setColorAt(0, QColor(197, 207, 223));
        bg.setColorAt(1, QColor(220, 229, 242));
        p.fillRect(e->rect(), bg);
        const QRect pr = picRect();
        // shadow: 8 px fading out to the right and below
        const QColor sh[8] = {{171, 184, 202}, {176, 188, 206}, {181, 193, 211}, {187, 198, 215}, {192, 203, 220}, {196, 206, 222}, {199, 209, 225}, {202, 212, 227}};
        for (int i = 0; i < 8; ++i) {
            QColor c = sh[i];
            c.setAlpha(255 - i * 22);
            p.fillRect(QRect(pr.right() + 1 + i, pr.top() + 3, 1, pr.height() - 3 + i), c);
            p.fillRect(QRect(pr.left() + 3, pr.bottom() + 1 + i, pr.width() - 3 + i, 1), c);
        }
        // picture
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1);
        const QRectF src = QRectF(toImage(e->rect().intersected(pr).topLeft()), QSizeF(e->rect().intersected(pr).size()) / m_zoom).adjusted(-1, -1, 1, 1);
        const QRectF srcC = src.intersected(QRectF(m_img.rect()));
        if (!srcC.isEmpty()) {
            const QRect s = srcC.toAlignedRect();
            p.drawImage(toWidget(QRectF(s)), m_img, s);
            if (m_hasShape && !m_overlay.isNull()) {
                p.drawImage(toWidget(QRectF(s)), m_overlay, s);
            }
        }
        if (m_floating && !m_float.isNull()) {
            p.save();
            p.setClipRect(pr);
            p.drawImage(toWidget(QRectF(m_floatPos, m_float.size())), displayFloat());
            p.restore();
        }
        if (gridlines && m_zoom >= 2) {
            p.setPen(QColor(0xc0, 0xc0, 0xc0));
            for (int x = 0; x <= m_img.width(); ++x) {
                const int wx = M + qRound(x * m_zoom);
                if (wx >= e->rect().left() - 1 && wx <= e->rect().right() + 1) {
                    p.drawLine(wx, pr.top(), wx, pr.bottom());
                }
            }
            for (int y = 0; y <= m_img.height(); ++y) {
                const int wy = M + qRound(y * m_zoom);
                if (wy >= e->rect().top() - 1 && wy <= e->rect().bottom() + 1) {
                    p.drawLine(pr.left(), wy, pr.right(), wy);
                }
            }
        }
        // selection frame
        if (m_hasSel) {
            const QRectF wr = toWidget(QRectF(m_floating ? QRect(m_floatPos, m_float.size()) : m_selRect)).adjusted(0, 0, -1, -1);
            if (!m_selPath.isEmpty() && !m_floating) {
                QPainterPath wp = QTransform().translate(M, M).scale(m_zoom, m_zoom).map(m_selPath);
                p.setPen(QPen(Qt::black, 1, Qt::DotLine));
                p.drawPath(wp);
            }
            dashedRect(p, wr);
            for (const QRect &h : boxHandles(wr.adjusted(0, 0, 1, 1))) {
                drawHandle(p, h);
            }
        }
        if (m_dragSel) {
            dashedRect(p, toWidget(QRectF(QRectF(m_press, m_last).normalized().toAlignedRect())).adjusted(0, 0, -1, -1));
        }
        if (m_freePath.elementCount() > 1) {
            p.setPen(QPen(Qt::black, 1, Qt::DotLine));
            p.drawPath(QTransform().translate(M, M).scale(m_zoom, m_zoom).map(m_freePath));
        }
        if (m_hasShape && !isOpenShape(m_shapeKind) && !(m_shapeKind == Shape::Polygon && m_polyOpen)) {
            const QRectF wr = toWidget(m_shapeRect.normalized()).adjusted(0, 0, -1, -1);
            dashedRect(p, wr);
            for (const QRect &h : boxHandles(wr.adjusted(0, 0, 1, 1))) {
                drawHandle(p, h);
            }
        } else if (m_hasShape && isOpenShape(m_shapeKind) && m_shapePts.size() >= 2) {
            drawHandle(p, QRect(toWidget(m_shapePts[0]).toPoint() - QPoint(2, 2), QSize(5, 5)));
            drawHandle(p, QRect(toWidget(m_shapePts[1]).toPoint() - QPoint(2, 2), QSize(5, 5)));
        }
        if (m_text) {
            const QRectF wr = QRectF(m_text->geometry()).adjusted(-1, -1, 0, 0);
            dashedRect(p, wr);
            for (const QRect &h : boxHandles(wr.adjusted(0, 0, 1, 1))) {
                drawHandle(p, h);
            }
        }
        if (tool == Tool::Magnifier && m_hover && m_zoom < 8) {
            const QSizeF s(m_area->viewport()->width() / 2.0, m_area->viewport()->height() / 2.0);
            QRectF r(m_hoverW - QPointF(s.width() / 2, s.height() / 2), s);
            p.setPen(QPen(Qt::black, 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r.intersected(QRectF(pr)));
        }
        // picture resize handles, or the dotted outline while dragging them
        if (m_resizing >= 0) {
            QPen pen(Qt::black, 1, Qt::DotLine);
            p.setPen(pen);
            p.drawRect(QRect(pr.topLeft(), QSize(qRound(m_newSize.width() * m_zoom), qRound(m_newSize.height() * m_zoom))));
        }
        for (int i = 0; i < 3; ++i) {
            drawHandle(p, picHandle(i));
        }
    }

    QImage displayFloat() const
    {
        if (!transparentSelection) {
            return m_float;
        }
        QImage f = m_float.copy();
        const QRgb key = color2.rgb();
        for (int y = 0; y < f.height(); ++y) {
            QRgb *l = reinterpret_cast<QRgb *>(f.scanLine(y));
            for (int x = 0; x < f.width(); ++x) {
                if ((l[x] | 0xff000000) == key) {
                    l[x] = 0;
                }
            }
        }
        return f;
    }
    QImage selectionImage() const
    {
        if (m_floating) {
            return m_float;
        }
        QImage s = m_img.copy(m_selRect);
        if (!m_selPath.isEmpty()) {
            QImage masked(s.size(), QImage::Format_ARGB32_Premultiplied);
            masked.fill(color2);
            QPainter mp(&masked);
            mp.setClipPath(m_selPath.translated(-m_selRect.topLeft()));
            mp.drawImage(0, 0, s);
            return masked;
        }
        return s;
    }
    /** turns the selected area into a floating image, leaving colour 2 behind */
    void lift()
    {
        if (!m_hasSel || m_floating) {
            return;
        }
        pushUndo();
        m_float = selectionImage();
        m_floatPos = m_selRect.topLeft();
        QPainter p(&m_img);
        if (m_selPath.isEmpty()) {
            p.fillRect(m_selRect, color2);
        } else {
            p.fillPath(m_selPath, color2);
            // free-form: keep only the inside floating
            QImage f(m_float.size(), QImage::Format_ARGB32_Premultiplied);
            f.fill(Qt::transparent);
            QPainter fp(&f);
            fp.setClipPath(m_selPath.translated(-m_selRect.topLeft()));
            fp.drawImage(0, 0, m_float);
            fp.end();
            m_float = f;
        }
        m_selPath = QPainterPath();
        m_floating = true;
    }
    void commitFloat()
    {
        if (m_floating && !m_float.isNull()) {
            QPainter(&m_img).drawImage(m_floatPos, displayFloat());
        }
        m_floating = false;
        m_float = QImage();
    }
    void dropSelection(bool keep)
    {
        if (!m_hasSel) {
            return;
        }
        if (keep) {
            commitFloat();
        } else {
            m_floating = false;
            m_float = QImage();
        }
        m_hasSel = false;
        m_selPath = QPainterPath();
        notifySel();
        update();
        changed();
    }

    // ---- shapes
    void renderShape()
    {
        m_overlay = QImage(m_img.size(), QImage::Format_ARGB32_Premultiplied);
        m_overlay.fill(Qt::transparent);
        const QPainterPath path = shapePath(m_shapeKind, m_shapeRect, m_shapePts);
        const QColor line = m_shapeSwap ? color2 : color1;
        const QColor area = m_shapeSwap ? color1 : color2;
        const int w = penSize();
        const QRect bounds = path.boundingRect().toAlignedRect().adjusted(-w - 2, -w - 2, w + 2, w + 2);
        if (!isOpenShape(m_shapeKind) && fill != Style::None && !(m_shapeKind == Shape::Polygon && m_polyOpen)) {
            QImage layer(m_img.size(), QImage::Format_ARGB32_Premultiplied);
            layer.fill(Qt::transparent);
            QPainter lp(&layer);
            lp.setRenderHint(QPainter::Antialiasing);
            lp.fillPath(path, area);
            lp.end();
            composite(m_overlay, layer, lookFor(fill), bounds);
        }
        const Style os = isOpenShape(m_shapeKind) && outline == Style::None ? Style::Solid : outline;
        if (os != Style::None) {
            QImage layer(m_img.size(), QImage::Format_ARGB32_Premultiplied);
            layer.fill(Qt::transparent);
            QPainter lp(&layer);
            lp.setRenderHint(QPainter::Antialiasing, w > 1 || os != Style::Solid);
            QPen pen(line, os == Style::Marker || os == Style::Watercolor ? w * 2 : os == Style::Crayon || os == Style::Oil ? w + 3 : w);
            pen.setJoinStyle(Qt::MiterJoin);
            pen.setCapStyle(isOpenShape(m_shapeKind) ? Qt::RoundCap : Qt::SquareCap);
            lp.strokePath(path, pen);
            lp.end();
            composite(m_overlay, layer, lookFor(os), bounds);
        }
    }
    int shapeHandleAt(const QPointF &w) const
    {
        if (!m_hasShape) {
            return -1;
        }
        if (isOpenShape(m_shapeKind)) {
            for (int i = 0; i < 2 && i < m_shapePts.size(); ++i) {
                if (QRectF(toWidget(m_shapePts[i]) - QPointF(4, 4), QSizeF(9, 9)).contains(w)) {
                    return 100 + i;
                }
            }
            return -1;
        }
        const auto hs = boxHandles(toWidget(m_shapeRect.normalized()));
        for (int i = 0; i < hs.size(); ++i) {
            if (hs[i].adjusted(-2, -2, 2, 2).contains(w.toPoint())) {
                return i;
            }
        }
        return -1;
    }
    static QRectF resizeBy(QRectF r, int h, QPointF d)
    {
        // h: TL T TR L R BL B BR
        if (h == 0 || h == 3 || h == 5) {
            r.setLeft(r.left() + d.x());
        }
        if (h == 2 || h == 4 || h == 7) {
            r.setRight(r.right() + d.x());
        }
        if (h <= 2) {
            r.setTop(r.top() + d.y());
        }
        if (h >= 5) {
            r.setBottom(r.bottom() + d.y());
        }
        return r;
    }
    Qt::CursorShape handleCursor(int h) const
    {
        static const Qt::CursorShape c[8] = {Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor,
                                             Qt::SizeHorCursor, Qt::SizeBDiagCursor, Qt::SizeVerCursor, Qt::SizeFDiagCursor};
        return h >= 0 && h < 8 ? c[h] : Qt::SizeAllCursor;
    }

    // ---- freehand painting
    void beginStroke(bool right)
    {
        pushUndo();
        m_strokeBase = m_img;
        m_layer = QImage(m_img.size(), QImage::Format_ARGB32_Premultiplied);
        m_layer.fill(Qt::transparent);
        m_strokeColor = right ? color2 : color1;
        if (tool == Tool::Eraser) {
            m_strokeColor = color2;
        }
        m_dirty = QRect();
        m_strokeRight = right;
    }
    void stamp(QPainter &lp, const QPointF &c)
    {
        const int w = penSize();
        switch (tool) {
        case Tool::Pencil:
            if (w == 1) {
                lp.fillRect(QRect(c.toPoint(), QSize(1, 1)), m_strokeColor);
            } else {
                lp.fillRect(QRectF(c.x() - w / 2.0, c.y() - w / 2.0, w, w).toRect(), m_strokeColor);
            }
            break;
        case Tool::Eraser:
            if (m_strokeRight) {
                // right-button eraser only swaps colour 1 for colour 2
                const QRect r = QRectF(c.x() - w / 2.0, c.y() - w / 2.0, w, w).toRect().intersected(m_img.rect());
                for (int y = r.top(); y <= r.bottom(); ++y) {
                    for (int x = r.left(); x <= r.right(); ++x) {
                        if ((m_strokeBase.pixel(x, y) | 0xff000000) == (color1.rgb() | 0xff000000)) {
                            lp.fillRect(QRect(x, y, 1, 1), color2);
                        }
                    }
                }
            } else {
                lp.fillRect(QRectF(c.x() - w / 2.0, c.y() - w / 2.0, w, w).toRect(), m_strokeColor);
            }
            break;
        case Tool::Brush:
            switch (brush) {
            case BrushKind::Calligraphy1:
                lp.setPen(QPen(m_strokeColor, 1.6));
                lp.drawLine(c + QPointF(-w * 0.6, w * 0.6), c + QPointF(w * 0.6, -w * 0.6));
                break;
            case BrushKind::Calligraphy2:
                lp.setPen(QPen(m_strokeColor, 1.6));
                lp.drawLine(c + QPointF(-w * 0.6, -w * 0.6), c + QPointF(w * 0.6, w * 0.6));
                break;
            case BrushKind::Marker:
                lp.fillRect(QRectF(c.x() - w, c.y() - w * 0.6, w * 2, w * 1.2), m_strokeColor);
                break;
            case BrushKind::Watercolor:
            case BrushKind::Oil:
            case BrushKind::Crayon:
                lp.setPen(Qt::NoPen);
                lp.setBrush(m_strokeColor);
                lp.drawEllipse(c, w * 1.2, w * 1.2);
                break;
            case BrushKind::NaturalPencil:
                lp.setPen(Qt::NoPen);
                lp.setBrush(m_strokeColor);
                lp.drawEllipse(c, w * 0.5, w * 0.5);
                break;
            default:
                lp.setPen(Qt::NoPen);
                lp.setBrush(m_strokeColor);
                lp.drawEllipse(c, w / 2.0, w / 2.0);
            }
            break;
        default:
            break;
        }
    }
    void strokeTo(const QPointF &from, const QPointF &to)
    {
        QPainter lp(&m_layer);
        lp.setRenderHint(QPainter::Antialiasing, tool == Tool::Brush);
        const qreal d = QLineF(from, to).length();
        const int steps = qMax(1, int(std::ceil(d)));
        for (int i = (from == to ? 0 : 1); i <= steps; ++i) {
            stamp(lp, from + (to - from) * (qreal(i) / steps));
        }
        lp.end();
        const int w = penSize() * 2 + 4;
        const QRect r = QRectF(from, to).normalized().toAlignedRect().adjusted(-w, -w, w, w);
        m_dirty = m_dirty.united(r);
        refreshStroke(r);
    }
    void refreshStroke(const QRect &r)
    {
        const QRect rr = r.intersected(m_img.rect());
        if (rr.isEmpty()) {
            return;
        }
        {
            QPainter p(&m_img);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.drawImage(rr.topLeft(), m_strokeBase, rr);
        }
        composite(m_img, m_layer, tool == Tool::Brush ? lookFor(brush) : Look{}, rr);
        update(toWidget(QRectF(rr)).toAlignedRect().adjusted(-2, -2, 2, 2));
    }
    void airburst(const QPointF &c)
    {
        const int w = penSize() * 3 + 4; // spray radius
        QPainter p(&m_img);
        auto *rng = QRandomGenerator::global();
        for (int i = 0; i < w * 2; ++i) {
            const qreal a = rng->generateDouble() * 2 * M_PI, rr = std::sqrt(rng->generateDouble()) * w;
            p.fillRect(QRectF(c.x() + std::cos(a) * rr, c.y() + std::sin(a) * rr, 1, 1).toRect(), m_strokeColor);
        }
        update(toWidget(QRectF(c.x() - w - 1, c.y() - w - 1, 2 * w + 2, 2 * w + 2)).toAlignedRect().adjusted(-2, -2, 2, 2));
    }

    void floodFill(QPoint s, const QColor &c)
    {
        if (!m_img.rect().contains(s)) {
            return;
        }
        const QRgb target = m_img.pixel(s);
        const QRgb with = c.rgba();
        if (target == with) {
            return;
        }
        pushUndo();
        const int W = m_img.width(), H = m_img.height();
        QList<QPoint> stack{s};
        auto px = [&](int x, int y) -> QRgb & {
            return reinterpret_cast<QRgb *>(m_img.scanLine(y))[x];
        };
        while (!stack.isEmpty()) {
            QPoint p = stack.takeLast();
            int x = p.x();
            const int y = p.y();
            while (x > 0 && px(x - 1, y) == target) {
                --x;
            }
            bool up = false, down = false;
            for (; x < W && px(x, y) == target; ++x) {
                px(x, y) = with;
                if (y > 0) {
                    const bool t = px(x, y - 1) == target;
                    if (t && !up) {
                        stack.append(QPoint(x, y - 1));
                    }
                    up = t;
                }
                if (y < H - 1) {
                    const bool t = px(x, y + 1) == target;
                    if (t && !down) {
                        stack.append(QPoint(x, y + 1));
                    }
                    down = t;
                }
            }
        }
        update();
        changed();
    }

    // ---- text box
    void openText(const QRect &imgRect)
    {
        commitText();
        m_textRect = imgRect;
        m_text = new QTextEdit(this);
        m_text->setFrameShape(QFrame::NoFrame);
        m_text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_text->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_text->setAcceptRichText(false);
        m_text->document()->setDocumentMargin(1);
        m_text->setAttribute(Qt::WA_TranslucentBackground);
        m_text->viewport()->setAttribute(Qt::WA_TranslucentBackground);
        QObject::connect(m_text, &QTextEdit::textChanged, [this] {
            // grow downwards as lines are added, like Paint's box
            const int need = int(m_text->document()->size().height() / m_zoom) + 2;
            if (need > m_textRect.height()) {
                m_textRect.setHeight(need);
                placeText();
            }
        });
        styleText();
        placeText();
        m_text->show();
        m_text->setFocus();
        if (textEditing) {
            textEditing(true);
        }
        update();
    }
    void placeText()
    {
        if (m_text) {
            m_text->setGeometry(toWidget(QRectF(m_textRect)).toAlignedRect());
            styleText();
        }
    }
    void commitText()
    {
        if (!m_text) {
            return;
        }
        const QString s = m_text->toPlainText();
        if (!s.isEmpty() || textOpaque) {
            pushUndo();
            QPainter p(&m_img);
            if (textOpaque) {
                p.fillRect(m_textRect, color2);
            }
            p.setRenderHint(QPainter::TextAntialiasing);
            p.setFont(textFont);
            p.setPen(color1);
            p.drawText(QRectF(m_textRect).adjusted(1, 1, -1, -1), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, s);
        }
        m_text->deleteLater();
        m_text = nullptr;
        if (textEditing) {
            textEditing(false);
        }
        update();
        changed();
    }

    // ---- cursors: the real ones from mspaint.exe
    static QCursor paintCursor(int id)
    {
        static QHash<int, QCursor> cache;
        if (!cache.contains(id)) {
            const QDir d(QDir::homePath() + QStringLiteral("/.local/share/seven-paint"));
            const QStringList f = d.entryList({QStringLiteral("cur%1_*.png").arg(id)});
            if (f.isEmpty()) {
                return QCursor(Qt::CrossCursor);
            }
            const QStringList parts = QFileInfo(f.first()).completeBaseName().split(QLatin1Char('_'));
            cache.insert(id, QCursor(QPixmap(d.filePath(f.first())), parts.value(1).toInt(), parts.value(2).toInt()));
        }
        return cache.value(id);
    }
    void updateCursor()
    {
        switch (tool) {
        case Tool::Pencil:
            setCursor(paintCursor(9271));
            break;
        case Tool::Fill:
            setCursor(paintCursor(9272));
            break;
        case Tool::Picker:
            setCursor(paintCursor(9270));
            break;
        case Tool::Magnifier:
            setCursor(paintCursor(9274));
            break;
        case Tool::Text:
            setCursor(Qt::IBeamCursor);
            break;
        case Tool::Eraser: {
            // a square of the eraser's size, outlined
            const int w = qMax(3, int(penSize() * m_zoom));
            QPixmap pm(w + 2, w + 2);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.fillRect(QRect(0, 0, w + 2, w + 2), Qt::black);
            p.fillRect(QRect(1, 1, w, w), color2);
            p.end();
            setCursor(QCursor(pm, (w + 2) / 2, (w + 2) / 2));
            break;
        }
        case Tool::Brush:
            setCursor(paintCursor(brush == BrushKind::Airbrush ? 9273 : 9269));
            break;
        default:
            setCursor(paintCursor(9265));
        }
    }

    // ---- mouse
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton && e->button() != Qt::RightButton) {
            return;
        }
        const bool right = e->button() == Qt::RightButton;
        const QPointF w = e->position();
        const QPointF ip = toImage(w);
        m_pressW = w;
        m_press = ip;
        m_last = ip;
        m_button = e->button();
        // picture resize handles
        for (int i = 0; i < 3; ++i) {
            if (!right && picHandle(i).adjusted(-2, -2, 2, 2).contains(w.toPoint())) {
                settle();
                m_resizing = i;
                m_newSize = m_img.size();
                return;
            }
        }
        if (m_text) {
            if (m_text->geometry().adjusted(-4, -4, 4, 4).contains(w.toPoint())) {
                m_mode = Mode::MoveText;
                m_grabOffset = ip - QPointF(m_textRect.topLeft());
                return;
            }
            commitText();
            if (tool == Tool::Text) {
                return;
            }
        }
        if (m_hasShape) {
            const int h = shapeHandleAt(w);
            if (h >= 0) {
                m_mode = Mode::ShapeHandle;
                m_handle = h;
                m_origRect = m_shapeRect.normalized();
                m_origPts = m_shapePts;
                return;
            }
            if (!isOpenShape(m_shapeKind) && m_shapeKind != Shape::Polygon && m_shapeRect.normalized().contains(ip)) {
                m_mode = Mode::ShapeMove;
                m_origRect = m_shapeRect;
                return;
            }
            if (m_shapeKind == Shape::Polygon && m_polyOpen) {
                m_shapePts.append(ip);
                renderShape();
                update();
                m_mode = Mode::PolyPoint;
                return;
            }
            if (m_shapeKind == Shape::Curve && m_curveClicks < 2) {
                // two more clicks bend the curve
                ++m_curveClicks;
                if (m_curveClicks == 1) {
                    m_shapePts = {m_shapePts[0], m_shapePts[1], ip, ip};
                } else {
                    m_shapePts[3] = ip;
                }
                m_mode = Mode::CurveBend;
                renderShape();
                update();
                return;
            }
            settleShape();
            if (tool != Tool::Shape) {
                return;
            }
        }
        if (m_hasSel) {
            const QRect sr = m_floating ? QRect(m_floatPos, m_float.size()) : m_selRect;
            const auto hs = boxHandles(toWidget(QRectF(sr)));
            for (int i = 0; i < hs.size(); ++i) {
                if (hs[i].adjusted(-2, -2, 2, 2).contains(w.toPoint())) {
                    lift();
                    m_mode = Mode::SelHandle;
                    m_handle = i;
                    m_origRect = QRectF(m_floatPos, m_float.size());
                    m_origFloat = m_float;
                    return;
                }
            }
            if (sr.contains(ip.toPoint())) {
                if (e->modifiers() & Qt::ControlModifier && m_floating) {
                    QPainter(&m_img).drawImage(m_floatPos, displayFloat()); // ctrl-drag leaves a copy
                } else if (e->modifiers() & Qt::ControlModifier) {
                    pushUndo();
                    m_float = selectionImage();
                    m_floatPos = m_selRect.topLeft();
                    m_floating = true;
                }
                lift();
                m_mode = Mode::SelMove;
                m_grabOffset = ip - QPointF(m_floatPos);
                return;
            }
            dropSelection(true);
            if (tool == Tool::Select || tool == Tool::FreeSelect) {
                // the click that drops a selection may start a new one
            }
        }
        switch (tool) {
        case Tool::Select:
            m_dragSel = true;
            m_mode = Mode::SelDraw;
            break;
        case Tool::FreeSelect:
            m_freePath = QPainterPath(ip);
            m_mode = Mode::FreeDraw;
            break;
        case Tool::Pencil:
        case Tool::Eraser:
        case Tool::Brush:
            beginStroke(right);
            m_mode = Mode::Stroke;
            if (tool == Tool::Brush && brush == BrushKind::Airbrush) {
                airburst(ip);
                m_airTimer.start();
            } else {
                strokeTo(ip, ip);
            }
            break;
        case Tool::Fill:
            floodFill(ip.toPoint(), right ? color2 : color1);
            break;
        case Tool::Picker:
            if (m_img.rect().contains(ip.toPoint()) && colorPicked) {
                colorPicked(!right, QColor(m_img.pixel(ip.toPoint())));
            }
            setTool(m_beforePicker == Tool::Picker ? Tool::Pencil : m_beforePicker);
            break;
        case Tool::Magnifier:
            if (right) {
                zoomOut();
            } else {
                const auto st = zoomSteps();
                for (qreal s : st) {
                    if (s > m_zoom + 0.001) {
                        setZoom(s, ip);
                        break;
                    }
                }
            }
            break;
        case Tool::Text:
            m_dragSel = true;
            m_mode = Mode::TextDraw;
            break;
        case Tool::Shape:
            m_shapeKind = shape;
            m_shapeSwap = right;
            m_hasShape = true;
            m_curveClicks = isOpenShape(shape) && shape == Shape::Curve ? 0 : 2;
            if (shape == Shape::Polygon) {
                m_polyOpen = true;
                m_shapePts = {ip, ip};
            } else {
                m_shapePts = {ip, ip};
            }
            m_shapeRect = QRectF(ip, ip);
            m_mode = Mode::ShapeDraw;
            renderShape();
            break;
        }
        update();
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const QPointF w = e->position();
        QPointF ip = toImage(w);
        m_hoverW = w;
        m_hover = picRect().contains(w.toPoint());
        if (cursorMoved) {
            cursorMoved(m_hover ? QPoint(int(std::floor(ip.x())), int(std::floor(ip.y()))) : QPoint(-1, -1));
        }
        if (m_resizing >= 0) {
            const QSize s(qMax(1, qRound(ip.x())), qMax(1, qRound(ip.y())));
            m_newSize = QSize(m_resizing == 1 ? m_img.width() : s.width(), m_resizing == 0 ? m_img.height() : s.height());
            if (selectionChanged) {
                selectionChanged(m_newSize);
            }
            update();
            return;
        }
        if (!(e->buttons() & (Qt::LeftButton | Qt::RightButton))) {
            hoverCursor(w);
            if (tool == Tool::Magnifier) {
                update();
            }
            return;
        }
        const bool shift = e->modifiers() & Qt::ShiftModifier;
        switch (m_mode) {
        case Mode::Stroke:
            if (tool == Tool::Brush && brush == BrushKind::Airbrush) {
                airburst(ip);
            } else {
                strokeTo(m_last, ip);
            }
            break;
        case Mode::SelDraw:
        case Mode::TextDraw:
            ip = QPointF(qBound(0.0, ip.x(), qreal(m_img.width())), qBound(0.0, ip.y(), qreal(m_img.height())));
            if (selectionChanged) {
                selectionChanged(QRectF(m_press, ip).normalized().toAlignedRect().size());
            }
            update();
            break;
        case Mode::FreeDraw:
            m_freePath.lineTo(QPointF(qBound(0.0, ip.x(), qreal(m_img.width())), qBound(0.0, ip.y(), qreal(m_img.height()))));
            update();
            break;
        case Mode::SelMove:
            m_floatPos = (ip - m_grabOffset).toPoint();
            update();
            break;
        case Mode::SelHandle: {
            const QRectF r = resizeBy(m_origRect, m_handle, ip - m_press).normalized();
            const QSize s(qMax(1, qRound(r.width())), qMax(1, qRound(r.height())));
            m_float = m_origFloat.scaled(s, Qt::IgnoreAspectRatio, Qt::FastTransformation);
            m_floatPos = r.topLeft().toPoint();
            notifySel();
            update();
            break;
        }
        case Mode::ShapeDraw:
            if (isOpenShape(m_shapeKind) || m_shapeKind == Shape::Polygon) {
                QPointF to = ip;
                if (shift) {
                    to = snap45(m_shapePts[m_shapePts.size() - 2], ip);
                }
                m_shapePts.last() = to;
                m_shapeRect = QRectF(m_shapePts[0], to);
            } else {
                QPointF to = ip;
                if (shift) {
                    const qreal d = qMax(qAbs(ip.x() - m_press.x()), qAbs(ip.y() - m_press.y()));
                    to = m_press + QPointF(ip.x() < m_press.x() ? -d : d, ip.y() < m_press.y() ? -d : d);
                }
                m_shapeRect = QRectF(m_press, to);
            }
            if (selectionChanged) {
                selectionChanged(m_shapeRect.normalized().toAlignedRect().size());
            }
            renderShape();
            update();
            break;
        case Mode::PolyPoint:
            m_shapePts.last() = shift ? snap45(m_shapePts[m_shapePts.size() - 2], ip) : ip;
            renderShape();
            update();
            break;
        case Mode::CurveBend:
            m_shapePts[m_curveClicks == 1 ? 2 : 3] = ip;
            if (m_curveClicks == 1) {
                m_shapePts[3] = ip;
            }
            renderShape();
            update();
            break;
        case Mode::ShapeMove:
            m_shapeRect = m_origRect.translated(ip - m_press);
            renderShape();
            update();
            break;
        case Mode::ShapeHandle:
            if (m_handle >= 100) {
                m_shapePts[m_handle - 100] = m_origPts[m_handle - 100] + (ip - m_press);
                m_shapeRect = QRectF(m_shapePts[0], m_shapePts[1]);
            } else {
                m_shapeRect = resizeBy(m_origRect, m_handle, ip - m_press);
            }
            renderShape();
            update();
            break;
        case Mode::MoveText:
            m_textRect.moveTopLeft((ip - m_grabOffset).toPoint());
            placeText();
            update();
            break;
        default:
            break;
        }
        m_last = ip;
    }
    static QPointF snap45(const QPointF &a, const QPointF &b)
    {
        const QPointF d = b - a;
        const qreal ang = std::round(std::atan2(d.y(), d.x()) / (M_PI / 4)) * (M_PI / 4);
        const qreal len = std::hypot(d.x(), d.y());
        return a + QPointF(std::cos(ang) * len, std::sin(ang) * len);
    }
    void hoverCursor(const QPointF &w)
    {
        for (int i = 0; i < 3; ++i) {
            if (picHandle(i).adjusted(-2, -2, 2, 2).contains(w.toPoint())) {
                setCursor(i == 0 ? Qt::SizeHorCursor : i == 1 ? Qt::SizeVerCursor : Qt::SizeFDiagCursor);
                return;
            }
        }
        if (m_hasShape) {
            const int h = shapeHandleAt(w);
            if (h >= 0) {
                setCursor(h >= 100 ? Qt::SizeAllCursor : handleCursor(h));
                return;
            }
            if (!isOpenShape(m_shapeKind) && m_shapeRect.normalized().contains(toImage(w))) {
                setCursor(Qt::SizeAllCursor);
                return;
            }
        }
        if (m_hasSel) {
            const QRect sr = m_floating ? QRect(m_floatPos, m_float.size()) : m_selRect;
            const auto hs = boxHandles(toWidget(QRectF(sr)));
            for (int i = 0; i < hs.size(); ++i) {
                if (hs[i].adjusted(-2, -2, 2, 2).contains(w.toPoint())) {
                    setCursor(handleCursor(i));
                    return;
                }
            }
            if (sr.contains(toImage(w).toPoint())) {
                setCursor(Qt::SizeAllCursor);
                return;
            }
        }
        if (m_text && m_text->geometry().adjusted(-4, -4, 4, 4).contains(w.toPoint())) {
            setCursor(Qt::SizeAllCursor);
            return;
        }
        updateCursor();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() != m_button) {
            return;
        }
        const QPointF ip = toImage(e->position());
        if (m_resizing >= 0) {
            const QSize s = m_newSize;
            m_resizing = -1;
            if (s != m_img.size()) {
                resizeCanvas(s);
            }
            notifySel();
            update();
            return;
        }
        switch (m_mode) {
        case Mode::Stroke:
            m_airTimer.stop();
            changed();
            break;
        case Mode::SelDraw: {
            m_dragSel = false;
            const QRect r = QRectF(m_press, m_last).normalized().toAlignedRect().intersected(m_img.rect());
            if (r.width() > 0 && r.height() > 0 && QLineF(m_press, ip).length() * m_zoom > 1) {
                m_selRect = r;
                m_selPath = QPainterPath();
                m_hasSel = true;
                m_floating = false;
            }
            notifySel();
            break;
        }
        case Mode::FreeDraw: {
            m_freePath.closeSubpath();
            const QRect r = m_freePath.boundingRect().toAlignedRect().intersected(m_img.rect());
            if (r.width() > 1 && r.height() > 1) {
                m_selRect = r;
                m_selPath = m_freePath;
                m_hasSel = true;
                m_floating = false;
            }
            m_freePath = QPainterPath();
            notifySel();
            break;
        }
        case Mode::TextDraw: {
            m_dragSel = false;
            QRect r = QRectF(m_press, m_last).normalized().toAlignedRect();
            if (r.width() < 10 || r.height() < 10) {
                const QFontMetrics fm(textFont);
                r = QRect(m_press.toPoint(), QSize(qMax(150, r.width()), qMax(fm.height() + 4, r.height())));
            }
            notifySel();
            openText(r);
            break;
        }
        case Mode::ShapeDraw:
            if (m_shapeKind == Shape::Polygon) {
                m_shapePts.append(m_shapePts.last());
                renderShape();
            } else if (QLineF(m_press, ip).length() < 1) {
                m_hasShape = false;
                m_overlay = QImage();
            } else if (m_shapeKind == Shape::Curve) {
                m_curveClicks = 0;
            }
            notifySel();
            break;
        case Mode::PolyPoint:
            if (QLineF(m_shapePts.last(), m_shapePts.first()).length() * m_zoom < 6) {
                m_shapePts.removeLast();
                closePolygon();
            } else {
                m_shapePts.append(m_shapePts.last());
            }
            break;
        default:
            break;
        }
        m_mode = Mode::None;
        update();
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        if (m_hasShape && m_shapeKind == Shape::Polygon && m_polyOpen) {
            m_shapePts.removeLast();
            closePolygon();
            return;
        }
        mousePressEvent(e);
    }
    void closePolygon()
    {
        m_polyOpen = false;
        m_shapeRect = QPolygonF(m_shapePts.toVector()).boundingRect();
        renderShape();
        settleShape();
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = false;
        if (cursorMoved) {
            cursorMoved(QPoint(-1, -1));
        }
        if (tool == Tool::Magnifier) {
            update();
        }
    }
    void wheelEvent(QWheelEvent *e) override
    {
        if (e->modifiers() & Qt::ControlModifier) {
            e->angleDelta().y() > 0 ? zoomIn() : zoomOut();
            e->accept();
            return;
        }
        e->ignore();
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Escape) {
            if (m_hasShape && m_shapeKind == Shape::Polygon && m_polyOpen) {
                m_shapePts.removeLast();
                closePolygon();
            } else {
                settle();
            }
            return;
        }
        if (e->key() == Qt::Key_Delete && m_hasSel) {
            deleteSelection();
            return;
        }
        if (m_hasSel && m_floating && (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right || e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
            m_floatPos += QPoint(e->key() == Qt::Key_Left ? -1 : e->key() == Qt::Key_Right ? 1 : 0, e->key() == Qt::Key_Up ? -1 : e->key() == Qt::Key_Down ? 1 : 0);
            update();
            return;
        }
        QWidget::keyPressEvent(e);
    }

private:
    enum class Mode { None, Stroke, SelDraw, FreeDraw, SelMove, SelHandle, ShapeDraw, ShapeMove, ShapeHandle, PolyPoint, CurveBend, TextDraw, MoveText };
    QScrollArea *m_area;
    QImage m_img;
    QList<QImage> m_undo, m_redo;
    qreal m_zoom = 1;
    Mode m_mode = Mode::None;
    Qt::MouseButton m_button = Qt::NoButton;
    QPointF m_press, m_last, m_pressW, m_hoverW, m_grabOffset;
    bool m_hover = false;
    Tool m_beforePicker = Tool::Pencil;
    // stroke
    QImage m_strokeBase, m_layer;
    QColor m_strokeColor;
    bool m_strokeRight = false;
    QRect m_dirty;
    QTimer m_airTimer;
    // selection
    bool m_hasSel = false, m_floating = false, m_dragSel = false;
    QRect m_selRect;
    QPainterPath m_selPath, m_freePath;
    QImage m_float, m_origFloat;
    QPoint m_floatPos;
    int m_handle = -1;
    QRectF m_origRect;
    // shape being edited
    bool m_hasShape = false, m_shapeSwap = false, m_polyOpen = false;
    int m_curveClicks = 2;
    Shape m_shapeKind = Shape::Line;
    QRectF m_shapeRect;
    QList<QPointF> m_shapePts, m_origPts;
    QImage m_overlay;
    // picture resize
    int m_resizing = -1;
    QSize m_newSize;
    // text
    QTextEdit *m_text = nullptr;
    QRect m_textRect;
};
}

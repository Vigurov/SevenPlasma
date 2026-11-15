#include "win7i18n.h"
/*
 * Sticky Notes in the style of Windows 7 (StikyNot.exe).
 *
 * Every note is its own frameless window: the colours, sizes and pictures follow the program's DirectUI layout
 * (UIFILE 1001: 28 px toolbar with New / Delete, 5 px text margins, 12 px bottom strip with the size grip) and its
 * bitmaps (extracted to <data>/seven-stickynotes). Notes are saved as they are typed; closing a note closes the
 * program and the notes come back next time. One instance: "--new-note" (the jump list's task) asks the running
 * program for a new note.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <KWindowEffects>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextList>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QWindow>

namespace
{
const char *const kApp = "seven-stickynotes";

// StikyNot.exe UIFILE 1001: MainBack gradient (top, bottom) and NoteToolBar colour of each style
struct Theme {
    const char *id;
    const char *name;
    QColor top, bottom, bar;
};
const Theme kThemes[] = {
    {"Blue", QT_TRANSLATE_NOOP("Win7", "Blue"), {217, 243, 251}, {184, 219, 244}, {201, 236, 248}},
    {"Green", QT_TRANSLATE_NOOP("Win7", "Green"), {210, 255, 204}, {177, 232, 174}, {197, 247, 193}},
    {"Pink", QT_TRANSLATE_NOOP("Win7", "Pink"), {246, 211, 246}, {235, 174, 235}, {241, 195, 241}},
    {"Purple", QT_TRANSLATE_NOOP("Win7", "Purple"), {222, 218, 254}, {198, 184, 255}, {212, 205, 243}},
    {"White", QT_TRANSLATE_NOOP("Win7", "White"), {255, 255, 255}, {235, 235, 235}, {245, 245, 245}},
    {"Yellow", QT_TRANSLATE_NOOP("Win7", "Yellow"), {254, 254, 204}, {252, 249, 161}, {248, 247, 182}},
};

const Theme &theme(const QString &id)
{
    for (const Theme &t : kThemes) {
        if (id == QLatin1String(t.id)) {
            return t;
        }
    }
    return kThemes[5]; // yellow, the colour of a new note
}

QPixmap asset(const QString &name)
{
    static QHash<QString, QPixmap> cache;
    auto it = cache.find(name);
    if (it == cache.end()) {
        const QString path = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QLatin1String(kApp) + QLatin1Char('/') + name + QStringLiteral(".png"));
        it = cache.insert(name, QPixmap(path));
    }
    return *it;
}

// the drop shadow around a note (IMAGE 3022-3033): 5 px left, top, right and 7 px below
const QMargins kShadow(5, 5, 5, 7);
const int kBar = 28;      // toolbar
const int kButton = 24;   // New / Delete
const int kBottom = 12;   // the strip with the size grip
const int kEdge = 4;      // edges that resize the note
const QSize kNoteSize(180, 165);

QString storePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/notes.json");
}

/** Windows 7's task dialog "Delete Note": main instruction, text, "Don't display this message again", Yes / No */
bool confirmDelete(QWidget *parent)
{
    QSettings settings(QStringLiteral("win7"), QStringLiteral("stickynotes"));
    if (settings.value(QStringLiteral("DontAskDelete"), false).toBool()) {
        return true;
    }
    QDialog dlg(parent);
    dlg.setWindowTitle(tr7("Sticky Notes"));
    dlg.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    auto *outer = new QVBoxLayout(&dlg);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *content = new QWidget(&dlg);
    content->setAutoFillBackground(true);
    QPalette pw = content->palette();
    pw.setColor(QPalette::Window, Qt::white);
    content->setPalette(pw);
    auto *cl = new QVBoxLayout(content);
    cl->setContentsMargins(10, 10, 20, 20);
    cl->setSpacing(15);
    auto *main = new QLabel(tr7("Delete Note"), content);
    QFont mf = main->font();
    mf.setPointSizeF(mf.pointSizeF() * 12.0 / 9.0);
    main->setFont(mf);
    main->setStyleSheet(QStringLiteral("color: #003399;"));
    auto *text = new QLabel(tr7("Are you sure you want to delete this note?"), content);
    cl->addWidget(main);
    cl->addWidget(text);
    outer->addWidget(content);

    auto *line = new QFrame(&dlg);
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: #dfdfdf;"));
    outer->addWidget(line);

    auto *footer = new QWidget(&dlg);
    footer->setAutoFillBackground(true);
    QPalette pf = footer->palette();
    pf.setColor(QPalette::Window, QColor(0xf0, 0xf0, 0xf0));
    footer->setPalette(pf);
    auto *fl = new QHBoxLayout(footer);
    fl->setContentsMargins(14, 9, 12, 10);
    fl->setSpacing(8);
    auto *dont = new QCheckBox(tr7("Don't display this message again"), footer);
    auto *yes = new QPushButton(tr7("Yes"), footer);
    auto *no = new QPushButton(tr7("No"), footer);
    for (QPushButton *b : {yes, no}) {
        b->setMinimumSize(66, 23);
        b->setAutoDefault(false);
    }
    fl->addWidget(dont);
    fl->addStretch(1);
    fl->addWidget(yes);
    fl->addWidget(no);
    outer->addWidget(footer);
    no->setDefault(true);
    no->setFocus();
    QObject::connect(yes, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(no, &QPushButton::clicked, &dlg, &QDialog::reject);
    dlg.setFixedSize(qMax(dlg.sizeHint().width(), 396), dlg.sizeHint().height());
    const bool ok = dlg.exec() == QDialog::Accepted;
    if (ok && dont->isChecked()) {
        settings.setValue(QStringLiteral("DontAskDelete"), true);
    }
    return ok;
}
}

class Notes;

/** The text of a note: transparent, Segoe Print, Windows 7's context menu and formatting keys */
class NoteEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit NoteEdit(QWidget *parent)
        : QTextEdit(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setAcceptRichText(true);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        viewport()->setAutoFillBackground(false);
        setAutoFillBackground(false);
        setStyleSheet(QStringLiteral("QTextEdit { background: transparent; color: black; }"));
        QFont f(QStringLiteral("Segoe Print"));
        f.setPointSize(11);
        document()->setDefaultFont(f);
        document()->setDocumentMargin(0);
        setFont(f);
    }

Q_SIGNALS:
    void themeChosen(const QString &id);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        QMenu menu(this);
        const bool sel = textCursor().hasSelection();
        menu.addAction(tr7("Cut"), this, &QTextEdit::cut)->setEnabled(sel);
        menu.addAction(tr7("Copy"), this, &QTextEdit::copy)->setEnabled(sel);
        const QMimeData *clip = QApplication::clipboard()->mimeData();
        menu.addAction(tr7("Paste"), this, &QTextEdit::paste)->setEnabled(clip && (clip->hasText() || clip->hasHtml()));
        menu.addAction(tr7("Delete"), this, [this]() {
                textCursor().removeSelectedText();
            })->setEnabled(sel);
        menu.addSeparator();
        menu.addAction(tr7("Select All"), this, &QTextEdit::selectAll);
        menu.addSeparator();
        for (const Theme &t : kThemes) {
            const QString id = QLatin1String(t.id);
            menu.addAction(QIcon(asset(QStringLiteral("swatch-") + id.toLower())), tr7(t.name), this, [this, id]() {
                Q_EMIT themeChosen(id);
            });
        }
        menu.exec(event->globalPos());
    }
};

/** One note window */
class Note : public QWidget
{
    Q_OBJECT
public:
    Note(Notes *owner, const QString &id, const QString &themeId);

    QString id() const
    {
        return m_id;
    }
    QString themeId() const
    {
        return m_theme;
    }
    QRect noteGeometry() const
    {
        return geometry().marginsRemoved(kShadow);
    }
    void setNoteGeometry(const QRect &r)
    {
        setGeometry(r.marginsAdded(kShadow));
    }
    NoteEdit *edit() const
    {
        return m_edit;
    }
    void setTheme(const QString &id)
    {
        m_theme = id;
        update();
    }

Q_SIGNALS:
    void changed();
    void newNoteRequested(Note *from);
    void deleteRequested(Note *note);

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRect r = rect().marginsRemoved(kShadow);
        // the style may have painted a (translucent) window background: the shadow area starts empty
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(rect(), Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        paintShadow(p, r);
        const Theme &t = theme(m_theme);
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        g.setColorAt(0, t.top);
        g.setColorAt(1, t.bottom);
        p.fillRect(r, g);
        p.fillRect(QRect(r.left(), r.top(), r.width(), kBar), t.bar);
        if (isActiveWindow()) {
            paintButton(p, newRect(), QStringLiteral("new"), m_hot == 1, m_pressed == 1);
            paintButton(p, deleteRect(), QStringLiteral("delete"), m_hot == 2, m_pressed == 2);
        }
        const QPixmap grip = asset(QStringLiteral("grip"));
        p.drawPixmap(gripRect().topLeft(), grip);
    }

    void showEvent(QShowEvent *e) override
    {
        QWidget::showEvent(e);
        // the Kvantum theme blurs what is behind translucent windows: a note has only its own shadow
        QTimer::singleShot(0, this, [this]() {
            if (windowHandle()) {
                KWindowEffects::enableBlurBehind(windowHandle(), false);
            }
        });
    }

    void changeEvent(QEvent *e) override
    {
        if (e->type() == QEvent::ActivationChange) {
            update();
        }
        QWidget::changeEvent(e);
    }

    void resizeEvent(QResizeEvent *) override
    {
        const QRect r = rect().marginsRemoved(kShadow);
        m_edit->setGeometry(r.left() + 5, r.top() + kBar + 5, r.width() - 10, r.height() - kBar - 5 - kBottom);
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int hot = newRect().contains(e->pos()) ? 1 : deleteRect().contains(e->pos()) ? 2 : 0;
        if (hot != m_hot) {
            m_hot = hot;
            update();
        }
        const Qt::Edges edges = edgesAt(e->pos());
        if (edges == (Qt::TopEdge | Qt::LeftEdge) || edges == (Qt::BottomEdge | Qt::RightEdge)) {
            setCursor(Qt::SizeFDiagCursor);
        } else if (edges == (Qt::TopEdge | Qt::RightEdge) || edges == (Qt::BottomEdge | Qt::LeftEdge)) {
            setCursor(Qt::SizeBDiagCursor);
        } else if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
            setCursor(Qt::SizeHorCursor);
        } else if (edges & (Qt::TopEdge | Qt::BottomEdge)) {
            setCursor(Qt::SizeVerCursor);
        } else {
            unsetCursor();
        }
    }

    void leaveEvent(QEvent *) override
    {
        m_hot = 0;
        update();
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) {
            return;
        }
        if (isActiveWindow() && newRect().contains(e->pos())) {
            m_pressed = 1;
            update();
            return;
        }
        if (isActiveWindow() && deleteRect().contains(e->pos())) {
            m_pressed = 2;
            update();
            return;
        }
        const Qt::Edges edges = edgesAt(e->pos());
        if (edges) {
            windowHandle()->startSystemResize(edges);
            return;
        }
        if (e->pos().y() < kShadow.top() + kBar) {
            windowHandle()->startSystemMove();
        }
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        const int pressed = m_pressed;
        m_pressed = 0;
        update();
        if (pressed == 1 && newRect().contains(e->pos())) {
            Q_EMIT newNoteRequested(this);
        } else if (pressed == 2 && deleteRect().contains(e->pos())) {
            Q_EMIT deleteRequested(this);
        }
    }

    void moveEvent(QMoveEvent *) override
    {
        Q_EMIT changed();
    }

    void closeEvent(QCloseEvent *e) override;

private:
    QRect newRect() const
    {
        const QRect r = rect().marginsRemoved(kShadow);
        return QRect(r.left() + 5, r.top() + 2, kButton, kButton);
    }
    QRect deleteRect() const
    {
        const QRect r = rect().marginsRemoved(kShadow);
        return QRect(r.right() - 5 - kButton + 1, r.top() + 2, kButton, kButton);
    }
    QRect gripRect() const
    {
        const QRect r = rect().marginsRemoved(kShadow);
        return QRect(r.right() - 3 - 8 + 1, r.bottom() - 3 - 8 + 1, 8, 8);
    }
    Qt::Edges edgesAt(const QPoint &pos) const
    {
        const QRect r = rect().marginsRemoved(kShadow);
        if (gripRect().adjusted(-3, -3, 3, 3).contains(pos)) {
            return Qt::BottomEdge | Qt::RightEdge;
        }
        Qt::Edges e;
        if (pos.x() < r.left() + kEdge) {
            e |= Qt::LeftEdge;
        } else if (pos.x() > r.right() - kEdge) {
            e |= Qt::RightEdge;
        }
        if (pos.y() < r.top() + kEdge) {
            e |= Qt::TopEdge;
        } else if (pos.y() > r.bottom() - kEdge) {
            e |= Qt::BottomEdge;
        }
        return e;
    }
    void paintButton(QPainter &p, const QRect &r, const QString &name, bool hot, bool pressed) const
    {
        const QPixmap pm = asset(pressed && hot ? name + QStringLiteral("-pressed") : hot ? name + QStringLiteral("-hot") : name);
        p.drawPixmap(r.left() + (r.width() - pm.width()) / 2, r.top() + (r.height() - pm.height()) / 2, pm);
    }
    void paintShadow(QPainter &p, const QRect &r) const
    {
        // corners 10 px wide, edges tiled (IMAGE 3022-3027 above and to the sides, 3028-3033 below)
        const int L = kShadow.left(), T = kShadow.top(), R = kShadow.right(), B = kShadow.bottom();
        const QRect o = rect();
        auto img = [](int id) {
            return asset(QStringLiteral("shadow-%1").arg(id));
        };
        p.drawPixmap(o.left(), o.top(), img(3022));
        p.drawTiledPixmap(QRect(o.left() + 10, o.top(), o.width() - 20, T), img(3023));
        p.drawPixmap(o.right() - 9, o.top(), img(3024));
        p.drawPixmap(o.left(), o.top() + T, img(3025));
        p.drawTiledPixmap(QRect(o.left(), o.top() + T + 5, L, r.height() - 10), img(3026));
        p.drawPixmap(o.left(), r.bottom() - 4, img(3027));
        p.drawPixmap(o.right() - R + 1, o.top() + T, img(3031));
        p.drawTiledPixmap(QRect(o.right() - R + 1, o.top() + T + 5, R, r.height() - 10), img(3032));
        p.drawPixmap(o.right() - R + 1, r.bottom() - 4, img(3033));
        p.drawPixmap(o.left(), o.bottom() - B + 1, img(3028));
        p.drawTiledPixmap(QRect(o.left() + 10, o.bottom() - B + 1, o.width() - 20, B), img(3029));
        p.drawPixmap(o.right() - 9, o.bottom() - B + 1, img(3030));
    }

    Notes *m_owner;
    QString m_id;
    QString m_theme;
    NoteEdit *m_edit;
    int m_hot = 0;
    int m_pressed = 0;
};

/** All notes: loading, saving, new / delete, the single instance */
class Notes : public QObject
{
    Q_OBJECT
public:
    Notes()
    {
        m_save.setSingleShot(true);
        m_save.setInterval(400);
        connect(&m_save, &QTimer::timeout, this, &Notes::save);
    }

    bool load()
    {
        QFile f(storePath());
        if (!f.open(QIODevice::ReadOnly)) {
            m_loading = false;
            return false;
        }
        const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("notes")).toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            Note *n = create(o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("theme")).toString());
            n->edit()->setHtml(o.value(QStringLiteral("html")).toString());
            const QJsonArray g = o.value(QStringLiteral("geometry")).toArray();
            if (g.size() == 4) {
                n->setNoteGeometry(QRect(g[0].toInt(), g[1].toInt(), g[2].toInt(), g[3].toInt()));
            }
            n->show();
        }
        m_loading = false;
        return !m_notes.isEmpty();
    }

    Note *newNote(Note *from = nullptr)
    {
        if (m_notes.size() >= 50) {
            return nullptr; // "Too many notes have been created"
        }
        Note *n = create(QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("Yellow"));
        // Windows 7 puts a new note left of the one it came from, else in the top right corner of the screen
        const QRect avail = (from ? from->screen() : QGuiApplication::primaryScreen())->availableGeometry();
        QRect r(QPoint(), kNoteSize);
        if (from) {
            const QRect f = from->noteGeometry();
            r.moveTo(f.left() - 10 - r.width(), f.top());
            if (r.left() < avail.left()) {
                r.moveTo(f.right() + 11, f.top());
            }
        } else {
            r.moveTo(avail.right() - 100 - r.width(), avail.top() + 100);
            for (Note *o : std::as_const(m_notes)) { // not exactly over another one
                if (o != n && o->noteGeometry().topLeft() == r.topLeft()) {
                    r.translate(-r.width() - 10, 0);
                }
            }
        }
        n->setNoteGeometry(r);
        n->show();
        n->activateWindow();
        n->raise();
        n->edit()->setFocus();
        scheduleSave();
        return n;
    }

    void showAll()
    {
        for (Note *n : std::as_const(m_notes)) {
            n->show();
            n->raise();
        }
        if (!m_notes.isEmpty()) {
            m_notes.last()->activateWindow();
        }
    }

    void scheduleSave()
    {
        if (!m_loading) {
            m_save.start();
        }
    }

    void save()
    {
        QJsonArray arr;
        for (Note *n : std::as_const(m_notes)) {
            const QRect g = n->noteGeometry();
            arr.append(QJsonObject{{QStringLiteral("id"), n->id()},
                                   {QStringLiteral("theme"), n->themeId()},
                                   {QStringLiteral("html"), n->edit()->toHtml()},
                                   {QStringLiteral("geometry"), QJsonArray{g.x(), g.y(), g.width(), g.height()}}});
        }
        QDir().mkpath(QFileInfo(storePath()).absolutePath());
        QSaveFile f(storePath());
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(QJsonObject{{QStringLiteral("notes"), arr}}).toJson());
            f.commit();
        }
    }

    void quit()
    {
        m_save.stop();
        save();
        qApp->quit();
    }

private:
    Note *create(const QString &id, const QString &themeId)
    {
        auto *n = new Note(this, id, themeId);
        m_notes.append(n);
        connect(n, &Note::changed, this, &Notes::scheduleSave);
        connect(n->edit(), &QTextEdit::textChanged, this, &Notes::scheduleSave);
        connect(n->edit(), &NoteEdit::themeChosen, n, [this, n](const QString &t) {
            n->setTheme(t);
            scheduleSave();
        });
        connect(n, &Note::newNoteRequested, this, [this](Note *from) {
            newNote(from);
        });
        connect(n, &Note::deleteRequested, this, [this](Note *note) {
            if (!confirmDelete(note)) {
                return;
            }
            m_notes.removeAll(note);
            note->hide();
            note->deleteLater();
            save();
            if (m_notes.isEmpty()) { // the last note: the program ends, a new one starts with a blank note
                QFile::remove(storePath());
                qApp->quit();
            }
        });
        return n;
    }

    QList<QPointer<Note>> m_notes;
    QTimer m_save;
    bool m_loading = true;
};

Note::Note(Notes *owner, const QString &id, const QString &themeId)
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint)
    , m_owner(owner)
    , m_id(id)
    , m_theme(themeId)
    , m_edit(new NoteEdit(this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMouseTracking(true);
    setWindowTitle(tr7("Sticky Note"));
    setMinimumSize(QSize(135, 100).grownBy(kShadow));
    resize(kNoteSize.grownBy(kShadow));

    auto keys = [this](const QKeySequence &k, auto fn) {
        auto *s = new QShortcut(k, this);
        s->setContext(Qt::WindowShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    keys(QKeySequence(Qt::CTRL | Qt::Key_N), [this]() {
        Q_EMIT newNoteRequested(this);
    });
    keys(QKeySequence(Qt::CTRL | Qt::Key_D), [this]() {
        Q_EMIT deleteRequested(this);
    });
    auto fmt = [this](auto change) {
        QTextCursor c = m_edit->textCursor();
        QTextCharFormat f = c.charFormat();
        change(f);
        c.mergeCharFormat(f);
        m_edit->mergeCurrentCharFormat(f);
    };
    keys(QKeySequence(Qt::CTRL | Qt::Key_B), [fmt]() {
        fmt([](QTextCharFormat &f) {
            f.setFontWeight(f.fontWeight() > QFont::Normal ? QFont::Normal : QFont::Bold);
        });
    });
    keys(QKeySequence(Qt::CTRL | Qt::Key_I), [fmt]() {
        fmt([](QTextCharFormat &f) {
            f.setFontItalic(!f.fontItalic());
        });
    });
    keys(QKeySequence(Qt::CTRL | Qt::Key_U), [fmt]() {
        fmt([](QTextCharFormat &f) {
            f.setFontUnderline(!f.fontUnderline());
        });
    });
    keys(QKeySequence(Qt::CTRL | Qt::Key_T), [fmt]() {
        fmt([](QTextCharFormat &f) {
            f.setFontStrikeOut(!f.fontStrikeOut());
        });
    });
    // Ctrl+Shift+> / < : bigger / smaller text
    keys(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Greater), [this, fmt]() {
        const qreal s = m_edit->textCursor().charFormat().fontPointSize();
        fmt([s](QTextCharFormat &f) {
            f.setFontPointSize(qMin<qreal>((s > 0 ? s : 11) + 1, 72));
        });
    });
    keys(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Less), [this, fmt]() {
        const qreal s = m_edit->textCursor().charFormat().fontPointSize();
        fmt([s](QTextCharFormat &f) {
            f.setFontPointSize(qMax<qreal>((s > 0 ? s : 11) - 1, 6));
        });
    });
    // Ctrl+Shift+L: bullets, then numbers, then letters..., then none
    keys(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L), [this]() {
        QTextCursor c = m_edit->textCursor();
        static const QTextListFormat::Style order[] = {QTextListFormat::ListDisc, QTextListFormat::ListDecimal, QTextListFormat::ListLowerAlpha,
                                                       QTextListFormat::ListUpperAlpha, QTextListFormat::ListLowerRoman, QTextListFormat::ListUpperRoman};
        QTextList *list = c.currentList();
        if (!list) {
            c.createList(order[0]);
            return;
        }
        const auto *it = std::find(std::begin(order), std::end(order), list->format().style());
        if (it == std::end(order) || it + 1 == std::end(order)) {
            QTextBlockFormat bf = c.blockFormat();
            bf.setIndent(0);
            list->remove(c.block());
            c.setBlockFormat(bf);
        } else {
            QTextListFormat lf = list->format();
            lf.setStyle(*(it + 1));
            list->setFormat(lf);
        }
    });
    auto align = [this](Qt::Alignment a) {
        m_edit->setAlignment(a);
    };
    keys(QKeySequence(Qt::CTRL | Qt::Key_L), [align]() {
        align(Qt::AlignLeft);
    });
    keys(QKeySequence(Qt::CTRL | Qt::Key_E), [align]() {
        align(Qt::AlignHCenter);
    });
    keys(QKeySequence(Qt::CTRL | Qt::Key_R), [align]() {
        align(Qt::AlignRight);
    });
}

void Note::closeEvent(QCloseEvent *e)
{
    // Alt+F4 / "Close window": Windows 7 closes the whole program, the notes stay for next time
    e->accept();
    QTimer::singleShot(0, m_owner, &Notes::quit);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Win7I18n::install(&app);
    app.setApplicationName(QLatin1String(kApp));
    app.setApplicationDisplayName(tr7("Sticky Notes"));
    app.setDesktopFileName(QLatin1String(kApp));
    app.setQuitOnLastWindowClosed(false);

    const bool wantNew = app.arguments().contains(QStringLiteral("--new-note"));
    const QString server = QLatin1String(kApp) + QLatin1Char('-') + qEnvironmentVariable("USER");
    {
        QLocalSocket probe;
        probe.connectToServer(server);
        if (probe.waitForConnected(300)) {
            probe.write(wantNew ? "new\n" : "show\n");
            probe.waitForBytesWritten(300);
            return 0;
        }
    }
    QLocalServer::removeServer(server);
    QLocalServer local;
    local.listen(server);

    Notes notes;
    QObject::connect(&local, &QLocalServer::newConnection, &notes, [&]() {
        while (QLocalSocket *s = local.nextPendingConnection()) {
            QObject::connect(s, &QLocalSocket::readyRead, s, [s, &notes]() {
                const QByteArray cmd = s->readAll().trimmed();
                if (cmd == "new") {
                    notes.newNote();
                } else {
                    notes.showAll();
                }
                s->deleteLater();
            });
        }
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &notes, &Notes::save);

    if (!notes.load() || wantNew) {
        notes.newNote();
    }
    return app.exec();
}

#include "main.moc"

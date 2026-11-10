/*
 * Control Panel\Fonts: the Windows 7 Fonts folder (fontext.dll) — the page tiles with a sample of every family,
 * Preview / Delete / Show / Hide, the details pane, one family's faces and the Font settings page.
 * Fonts are fontconfig's; "hidden" ones are rejected through ~/.config/fontconfig/conf.d.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7chrome.h"
#include "win7devices.h"
#include "win7display.h"
#include "win7fontdata.h"
#include "win7programs.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>

namespace Win7Fonts
{

struct Nav {
    std::function<void(int)> open;
    std::function<void()> home;
    std::function<void()> back;
    std::function<void(const QStringList &)> spawn; // another seven-control process: --fontview, --charmap, --cttune...
};

/** The family opened from the folder (FamilyView) */
inline QString &openedFamily()
{
    static QString name;
    return name;
}

inline bool installAsShortcut()
{
    return fontSettings().value(QStringLiteral("Install/Shortcut"), false).toBool();
}

inline void refreshFontCache()
{
    QProcess::execute(QStringLiteral("fc-cache"), {QStringLiteral("-f"), userFontDir()});
    FcInitReinitialize();
}

/** Copies (or links, with "install as shortcut") font files into ~/.local/share/fonts */
inline int installFonts(QWidget *parent, const QStringList &files)
{
    QDir().mkpath(userFontDir());
    int done = 0;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const QString &file : files) {
        const QFileInfo fi(file);
        static const QStringList ok{QStringLiteral("ttf"), QStringLiteral("otf"), QStringLiteral("ttc"), QStringLiteral("otc"), QStringLiteral("pfb"),
                                    QStringLiteral("pfa"), QStringLiteral("woff"), QStringLiteral("woff2")};
        if (!fi.isFile() || !ok.contains(fi.suffix().toLower()) || !SfntFile(file).valid()) {
            QApplication::restoreOverrideCursor();
            QMessageBox::warning(parent, tr7("Install Font"), tr7("The file \"%1\" does not appear to be a valid font.").arg(fi.fileName()));
            QApplication::setOverrideCursor(Qt::WaitCursor);
            continue;
        }
        const QString target = userFontDir() + QLatin1Char('/') + fi.fileName();
        if (QFileInfo(target).canonicalFilePath() == fi.canonicalFilePath()) {
            continue;
        }
        if (QFileInfo::exists(target) || QFileInfo(target).isSymLink()) {
            QApplication::restoreOverrideCursor();
            const auto answer = QMessageBox::question(parent, tr7("Install Font"),
                                                      tr7("The font \"%1\" is already installed. Do you want to replace it?").arg(fi.completeBaseName()));
            QApplication::setOverrideCursor(Qt::WaitCursor);
            if (answer != QMessageBox::Yes) {
                continue;
            }
            QFile::remove(target);
        }
        const bool okCopy = installAsShortcut() ? QFile::link(fi.absoluteFilePath(), target) : QFile::copy(file, target);
        if (okCopy) {
            ++done;
        }
    }
    if (done) {
        refreshFontCache();
    }
    QApplication::restoreOverrideCursor();
    return done;
}

/** Deletes the user's fonts after the Windows 7 questions; packaged system fonts are protected */
inline bool deleteFaces(QWidget *parent, const QList<Face> &faces)
{
    QStringList files;
    QString first;
    for (const Face &f : faces) {
        if (!files.contains(f.file)) {
            files << f.file;
            if (first.isEmpty()) {
                first = f.family + (f.style.isEmpty() ? QString() : QLatin1Char(' ') + styleName(f.style));
            }
        }
    }
    if (files.isEmpty()) {
        return false;
    }
    for (const QString &file : std::as_const(files)) {
        if (!isUserFont(file)) {
            QMessageBox::information(parent, tr7("Delete Font"),
                                     tr7("The font \"%1\" cannot be deleted because it is a protected system font.").arg(QFileInfo(file).completeBaseName()));
            return false;
        }
    }
    QMessageBox box(QMessageBox::Warning, files.size() == 1 ? tr7("Delete Font") : tr7("Delete Fonts"),
                    files.size() == 1 ? tr7("Are you sure you want to permanently delete this font?") : tr7("Are you sure you want to permanently delete these %1 fonts?").arg(files.size()),
                    QMessageBox::Yes | QMessageBox::No, parent);
    box.setInformativeText(files.size() == 1 ? tr7("%1\n\nIf you delete this font, some text might not appear as intended.").arg(first)
                                             : tr7("If you delete these fonts, some text might not appear as intended."));
    box.button(QMessageBox::Yes)->setText(tr7("Yes"));
    box.button(QMessageBox::No)->setText(tr7("No"));
    if (box.exec() != QMessageBox::Yes) {
        return false;
    }
    QStringList hidden = hiddenFiles();
    for (const QString &file : std::as_const(files)) {
        QFile::remove(file);
        hidden.removeAll(file);
    }
    setHiddenFiles(hidden);
    refreshFontCache();
    return true;
}

inline void setFacesHidden(const QList<Face> &faces, bool hide)
{
    QStringList hidden = hiddenFiles();
    for (const Face &f : faces) {
        if (hide) {
            hidden << f.file;
        } else {
            hidden.removeAll(f.file);
        }
    }
    setHiddenFiles(hidden);
}

/* ---------------- tiles ---------------- */

/** The page icon of fontext.dll with the sample letters of the face: a stack of pages for a family */
inline QPixmap tilePicture(const Face &face, bool stack, bool hidden, int size)
{
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1#%2#%3#%4#%5").arg(face.file).arg(face.index).arg(stack).arg(hidden).arg(size);
    auto it = cache.find(key);
    if (it != cache.end()) {
        return *it;
    }
    const QString icon = QStringLiteral("seven-font-%1%2").arg(stack ? QStringLiteral("pages") : QStringLiteral("page"), hidden ? QStringLiteral("-hidden") : QString());
    QPixmap pm = QIcon::fromTheme(icon).pixmap(size, size);
    if (pm.isNull()) {
        pm = QPixmap(size, size);
        pm.fill(Qt::transparent);
    }
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        // the front page: the stack shifts it down and left (pages stop at 7/8 of the icon height)
        const QPointF centre = stack ? QPointF(size * 0.42, size * 0.53) : QPointF(size * 0.5, size * 0.47);
        const qreal maxWidth = size * (stack ? 0.66 : 0.76);
        QString sample = face.sample;
        if (face.rtl) {
            std::reverse(sample.begin(), sample.end()); // drawn glyph by glyph, so lay right-to-left out by hand
        }
        const QImage text = renderText(face.file, face.index, sample, qMax(9, int(size * 0.3)));
        if (!text.isNull()) {
            QSizeF ts = text.size();
            if (ts.width() > maxWidth) {
                ts.scale(maxWidth, ts.height(), Qt::KeepAspectRatio);
            }
            p.setOpacity(hidden ? 0.3 : 1.0);
            p.drawImage(QRectF(centre.x() - ts.width() / 2, centre.y() - ts.height() / 2, ts.width(), ts.height()), text);
        }
    }
    cache.insert(key, pm);
    return pm;
}

/** One entry of the folder: a family (top level) or a face (family view) */
struct Entry {
    Family family;
    bool isFace = false;
    QString title() const
    {
        if (!isFace) {
            return family.name;
        }
        const Face &f = family.faces.first();
        return f.family + QLatin1Char(' ') + styleName(f.style);
    }
    bool stack() const
    {
        return !isFace && family.faces.size() > 1;
    }
    bool hidden() const
    {
        return family.hidden();
    }
};

/** The large icons view: tiles laid out and painted by hand, like the folder window */
class IconCanvas : public QWidget
{
public:
    std::function<void()> selectionChanged;
    std::function<void(int)> activated;
    std::function<void(int, QPoint)> menu; // -1: background
    std::function<void(const QStringList &)> dropped;
    QList<Entry> entries;
    QSet<int> selected;
    int iconSize = 96;

    IconCanvas()
    {
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setAcceptDrops(true);
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral("background: #ffffff;"));
    }

    QSize tileSize() const
    {
        return QSize(iconSize + 18, iconSize + 44);
    }
    int columns() const
    {
        return qMax(1, (width() - 12) / tileSize().width());
    }
    QRect tileRect(int i) const
    {
        const QSize t = tileSize();
        return QRect(12 + (i % columns()) * t.width(), 6 + (i / columns()) * t.height(), t.width() - 4, t.height() - 4);
    }
    int heightFor(int w) const
    {
        const int cols = qMax(1, (w - 12) / tileSize().width());
        return 12 + int((entries.size() + cols - 1) / cols) * tileSize().height();
    }
    int indexAt(QPoint pos) const
    {
        for (int i = 0; i < entries.size(); ++i) {
            if (tileRect(i).contains(pos)) {
                return i;
            }
        }
        return -1;
    }

protected:
    void paintEvent(QPaintEvent *e) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing);
        const QFontMetrics fm(font());
        for (int i = 0; i < entries.size(); ++i) {
            const QRect r = tileRect(i);
            if (!r.intersects(e->rect())) {
                continue;
            }
            const Entry &en = entries.at(i);
            Win7Dev::paintSelection(p, r, selected.contains(i), i == m_hot, hasFocus());
            const QPixmap pic = tilePicture(en.family.faces.first(), en.stack(), en.hidden(), iconSize);
            p.drawPixmap(r.left() + (r.width() - iconSize) / 2, r.top() + 4, pic);
            // name, and the style for single faces ("Algerian звичайний"), at most two lines
            QString text = en.title();
            if (!en.isFace && en.family.faces.size() == 1) {
                text = en.family.name + QLatin1Char(' ') + styleName(en.family.faces.first().style);
            }
            const QRect tr(r.left() + 2, r.top() + iconSize + 8, r.width() - 4, fm.height() * 2 + 2);
            p.setPen(en.hidden() ? QColor(0x80, 0x80, 0x80) : QColor(0x1e, 0x1e, 0x1e));
            p.drawText(tr, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, elideTwoLines(fm, text, tr.width()));
        }
    }

    static QString elideTwoLines(const QFontMetrics &fm, const QString &text, int width)
    {
        if (fm.boundingRect(QRect(0, 0, width, 1000), Qt::TextWordWrap, text).height() <= fm.height() * 2 + 1) {
            return text;
        }
        QString t = text;
        while (t.size() > 1 && fm.boundingRect(QRect(0, 0, width, 1000), Qt::TextWordWrap, t + QStringLiteral("…")).height() > fm.height() * 2 + 1) {
            t.chop(1);
        }
        return t + QStringLiteral("…");
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int hot = indexAt(e->position().toPoint());
        if (hot != m_hot) {
            if (m_hot >= 0) {
                update(tileRect(m_hot));
            }
            m_hot = hot;
            if (m_hot >= 0) {
                update(tileRect(m_hot));
                setToolTip(entries.at(m_hot).title());
            }
        }
    }
    void leaveEvent(QEvent *) override
    {
        if (m_hot >= 0) {
            update(tileRect(m_hot));
        }
        m_hot = -1;
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        const int i = indexAt(e->position().toPoint());
        if (e->button() == Qt::RightButton && i >= 0 && selected.contains(i)) {
            // keep a multi-selection for its menu
        } else if (i < 0) {
            selected.clear();
        } else if (e->modifiers() & Qt::ControlModifier) {
            if (!selected.remove(i)) {
                selected.insert(i);
            }
            m_anchor = i;
        } else if ((e->modifiers() & Qt::ShiftModifier) && m_anchor >= 0) {
            selected.clear();
            for (int k = qMin(m_anchor, i); k <= qMax(m_anchor, i); ++k) {
                selected.insert(k);
            }
        } else {
            selected = {i};
            m_anchor = i;
        }
        update();
        if (selectionChanged) {
            selectionChanged();
        }
        if (e->button() == Qt::RightButton && menu) {
            menu(i, e->globalPosition().toPoint());
        }
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        const int i = indexAt(e->position().toPoint());
        if (i >= 0 && activated) {
            activated(i);
        }
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        if (entries.isEmpty()) {
            return;
        }
        int cur = m_anchor >= 0 ? m_anchor : 0;
        switch (e->key()) {
        case Qt::Key_Right:
            cur = qMin(int(entries.size()) - 1, cur + 1);
            break;
        case Qt::Key_Left:
            cur = qMax(0, cur - 1);
            break;
        case Qt::Key_Down:
            cur = qMin(int(entries.size()) - 1, cur + columns());
            break;
        case Qt::Key_Up:
            cur = qMax(0, cur - columns());
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (activated && m_anchor >= 0) {
                activated(m_anchor);
            }
            return;
        case Qt::Key_A:
            if (e->modifiers() & Qt::ControlModifier) {
                for (int i = 0; i < entries.size(); ++i) {
                    selected.insert(i);
                }
                update();
                if (selectionChanged) {
                    selectionChanged();
                }
            }
            return;
        default:
            QWidget::keyPressEvent(e);
            return;
        }
        selected = {cur};
        m_anchor = cur;
        update();
        if (selectionChanged) {
            selectionChanged();
        }
        // keep it in view
        if (auto *area = qobject_cast<QScrollArea *>(parentWidget() ? parentWidget()->parentWidget() : nullptr)) {
            area->ensureVisible(tileRect(cur).center().x(), tileRect(cur).center().y(), 0, tileSize().height());
        }
    }
    void focusInEvent(QFocusEvent *) override
    {
        update();
    }
    void focusOutEvent(QFocusEvent *) override
    {
        update();
    }
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (e->mimeData()->hasUrls()) {
            e->acceptProposedAction();
        }
    }
    void dropEvent(QDropEvent *e) override
    {
        QStringList files;
        for (const QUrl &u : e->mimeData()->urls()) {
            if (u.isLocalFile()) {
                files << u.toLocalFile();
            }
        }
        if (!files.isEmpty() && dropped) {
            dropped(files);
        }
    }

private:
    int m_hot = -1;
    int m_anchor = -1;
};

/* ---------------- the folder page ---------------- */

class FontsPage : public QWidget
{
public:
    FontsPage(bool familyView, Nav nav)
        : m_familyView(familyView)
        , m_nav(std::move(nav))
    {
        auto *side = Win7Display::sidePane(
            m_nav.home,
            {{tr7("Font settings"), [this] { m_nav.open(SettingsView); }},
             {tr7("Get more font information online"),
              [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://learn.microsoft.com/typography/"))); }},
             {tr7("Adjust ClearType text"), [this] { m_nav.spawn({QStringLiteral("--cttune")}); }},
             {tr7("Find a character"), [this] { m_nav.spawn({QStringLiteral("--charmap")}); }},
             {tr7("Change font size"), [this] { m_nav.open(Win7Display::DisplayView); }}},
            {{tr7("Text Services and Input Language"), [this] { m_nav.spawn({QStringLiteral("--text-services")}); }},
             {tr7("Personalization"), [this] { m_nav.open(Win7Display::PersonalizeView); }}});

        auto *content = new QWidget;
        auto *v = new QVBoxLayout(content);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        auto *head = new QWidget;
        auto *hv = new QVBoxLayout(head);
        hv->setContentsMargins(32, 18, 30, 12);
        hv->addWidget(Win7Programs::pageTitle(m_familyView ? tr7("Preview, delete, or show and hide the fonts in this family.")
                                                           : tr7("Preview, delete, or show and hide fonts")));
        v->addWidget(head);

        m_bar = Win7Programs::commandBar();
        auto *organize = new QMenu(this);
        organize->addAction(tr7("Copy"), this, [this] { copySelected(); });
        m_paste = organize->addAction(tr7("Paste"), this, [this] { paste(); });
        organize->addSeparator();
        organize->addAction(tr7("Select all", "edit"), this, [this] { selectAll(); });
        organize->addSeparator();
        auto *layoutMenu = organize->addMenu(tr7("Layout"));
        m_detailsToggle = layoutMenu->addAction(tr7("Details pane"));
        m_detailsToggle->setCheckable(true);
        m_detailsToggle->setChecked(true);
        connect(m_detailsToggle, &QAction::toggled, this, [this](bool on) { m_details->setVisible(on); });
        organize->addSeparator();
        m_organizeDelete = organize->addAction(tr7("Delete"), this, [this] { removeSelected(); });
        connect(organize, &QMenu::aboutToShow, this, [this] {
            m_paste->setEnabled(QApplication::clipboard()->mimeData() && QApplication::clipboard()->mimeData()->hasUrls());
            m_organizeDelete->setEnabled(!selectedFaces().isEmpty());
        });
        m_bar->addWidget(Win7Programs::menuButton(tr7("Organize"), organize));
        m_preview = m_bar->addAction(QIcon::fromTheme(QStringLiteral("seven-font-preview")), tr7("Preview", "button"), this, [this] { previewSelected(); });
        m_preview->setToolTip(tr7("Open the selected files in Font Viewer"));
        m_delete = m_bar->addAction(QIcon::fromTheme(QStringLiteral("seven-font-delete")), tr7("Delete"), this, [this] { removeSelected(); });
        m_delete->setToolTip(tr7("Delete the selected fonts"));
        m_show = m_bar->addAction(tr7("Show"), this, [this] { setSelectedHidden(false); });
        m_show->setToolTip(tr7("Show the selected fonts in programs"));
        m_hide = m_bar->addAction(tr7("Hide"), this, [this] { setSelectedHidden(true); });
        m_hide->setToolTip(tr7("Hide the selected fonts from programs"));
        if (auto *tb = qobject_cast<QToolButton *>(m_bar->widgetForAction(m_preview))) {
            tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        }
        if (auto *tb = qobject_cast<QToolButton *>(m_bar->widgetForAction(m_delete))) {
            tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        }
        auto *spacer = new QWidget;
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_bar->addWidget(spacer);
        auto *viewMenu = new QMenu(this);
        auto *viewGroup = new QActionGroup(viewMenu);
        const QList<QPair<QString, int>> views{{tr7("Large icons"), 96}, {tr7("Medium Icons"), 48}, {tr7("Details"), 0}};
        const int saved = fontSettings().value(QStringLiteral("View/Icons"), 96).toInt();
        for (const auto &vw : views) {
            QAction *a = viewMenu->addAction(vw.first, this, [this, size = vw.second] { setIconMode(size); });
            a->setCheckable(true);
            a->setChecked(vw.second == saved);
            viewGroup->addAction(a);
        }
        auto *viewBtn = new QToolButton;
        viewBtn->setIcon(QIcon::fromTheme(QStringLiteral("view-list-icons")));
        viewBtn->setToolTip(tr7("Change your view."));
        viewBtn->setPopupMode(QToolButton::InstantPopup);
        viewBtn->setMenu(viewMenu);
        m_bar->addWidget(viewBtn);
        v->addWidget(m_bar);

        m_stack = new QStackedWidget;
        m_scroll = new QScrollArea;
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setWidgetResizable(false);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->viewport()->setStyleSheet(QStringLiteral("background: #ffffff;"));
        m_canvas = new IconCanvas;
        m_scroll->setWidget(m_canvas);
        m_scroll->viewport()->installEventFilter(this);
        m_stack->addWidget(m_scroll);
        m_tree = new Win7Programs::DetailsTree;
        m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_tree->setHeaderLabels({tr7("Name"), tr7("Font style"), tr7("Show/hide"), tr7("Designed for"),
                                 tr7("Category"), tr7("Designer/foundry"), tr7("Font embeddability")});
        int col = 0;
        for (int w : {200, 150, 120, 170, 80, 170, 150}) {
            m_tree->setColumnWidth(col++, w);
        }
        m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
        m_stack->addWidget(m_tree);
        v->addWidget(m_stack, 1);

        m_details = new QWidget;
        m_details->setObjectName(QStringLiteral("win7details"));
        m_details->setAttribute(Qt::WA_StyledBackground, true);
        m_details->setStyleSheet(QStringLiteral("#win7details { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffffff, stop:1 #e9eff8);"
                                                " border-top: 1px solid #d5dfe9; }"));
        m_details->setFixedHeight(72);
        auto *dh = new QHBoxLayout(m_details);
        dh->setContentsMargins(14, 8, 14, 8);
        dh->setSpacing(12);
        m_detailsIcon = new QLabel;
        m_detailsIcon->setFixedSize(52, 52);
        m_detailsIcon->setAlignment(Qt::AlignCenter);
        dh->addWidget(m_detailsIcon);
        m_detailsText = new QLabel;
        m_detailsText->setTextFormat(Qt::RichText);
        m_detailsText->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); // long values never widen the window
        dh->addWidget(m_detailsText, 1);
        v->addWidget(m_details);

        auto *root = Win7Display::pageWithSide(side, content);
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->addWidget(root);

        m_canvas->selectionChanged = [this] { updateSelection(); };
        m_canvas->activated = [this](int i) { activate(i); };
        m_canvas->menu = [this](int i, QPoint pos) { contextMenu(i, pos); };
        m_canvas->dropped = [this](const QStringList &files) {
            if (installFonts(this, files)) {
                reload();
            }
        };
        connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] {
            if (m_syncing) {
                return;
            }
            m_canvas->selected.clear();
            for (QTreeWidgetItem *it : m_tree->selectedItems()) {
                m_canvas->selected.insert(it->data(0, Qt::UserRole).toInt());
            }
            updateSelection();
        });
        connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) { activate(it->data(0, Qt::UserRole).toInt()); });
        connect(m_tree, &QWidget::customContextMenuRequested, this, [this](QPoint pos) {
            QTreeWidgetItem *it = m_tree->itemAt(pos);
            contextMenu(it ? it->data(0, Qt::UserRole).toInt() : -1, m_tree->viewport()->mapToGlobal(pos));
        });
        // the folder notices fonts installed elsewhere, like Explorer does
        auto *watch = new QTimer(this);
        connect(watch, &QTimer::timeout, this, [this] {
            if (signature() != m_signature) {
                reload();
            }
        });
        watch->start(5000);
        m_iconSize = saved;
        QTimer::singleShot(0, this, [this] {
            reload();
            setIconMode(m_iconSize);
        });
    }

    void setFilter(const QString &text)
    {
        m_filter = text;
        fill();
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == m_scroll->viewport() && e->type() == QEvent::Resize) {
            relayoutCanvas();
        }
        return QWidget::eventFilter(o, e);
    }

private:
    static QString signature()
    {
        QStringList parts;
        for (const QString &dir : {userFontDir(), QDir::homePath() + QStringLiteral("/.fonts"), QStringLiteral("/usr/share/fonts"), QStringLiteral("/usr/local/share/fonts")}) {
            const QFileInfo fi(dir);
            parts << QString::number(fi.lastModified().toSecsSinceEpoch());
        }
        parts << hiddenFiles().join(QLatin1Char('|'));
        return parts.join(QLatin1Char(','));
    }

    void reload()
    {
        m_signature = signature();
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QList<Face> faces = installedFaces();
        m_all.clear();
        if (m_familyView) {
            for (const Family &fam : families(faces)) {
                if (fam.name == openedFamily()) {
                    for (const Face &f : fam.faces) {
                        m_all.append({{fam.name, {f}}, true});
                    }
                }
            }
        } else {
            for (const Family &fam : families(faces)) {
                m_all.append({fam, false});
            }
        }
        QApplication::restoreOverrideCursor();
        fill();
    }

    void fill()
    {
        m_shown.clear();
        for (const Entry &e : std::as_const(m_all)) {
            if (m_filter.isEmpty() || e.title().contains(m_filter, Qt::CaseInsensitive)) {
                m_shown << e;
            }
        }
        m_canvas->entries = m_shown;
        m_canvas->selected.clear();
        relayoutCanvas();
        m_canvas->update();
        if (m_iconSize == 0) {
            fillTree();
        }
        updateSelection();
    }

    void fillTree()
    {
        m_syncing = true;
        m_tree->clear();
        for (int i = 0; i < m_shown.size(); ++i) {
            const Entry &e = m_shown.at(i);
            const Face &f = e.family.faces.first();
            SfntFile sf(f.file, f.index);
            QStringList styles;
            for (const Face &face : e.family.faces) {
                styles << styleName(face.style);
            }
            QString vendor = sf.name(8);
            if (vendor.isEmpty()) {
                vendor = sf.name(9);
            }
            auto *it = new QTreeWidgetItem({e.isFace ? e.title() : e.family.name, styles.join(QStringLiteral("; ")), showHideText(e), f.scripts.join(QStringLiteral("; ")),
                                            f.symbol ? tr7("Symbol") : tr7("Text"), vendor, embeddability(sf.fsType())});
            it->setIcon(0, QIcon(tilePicture(f, e.stack(), e.hidden(), 32).scaled(16, 16, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            it->setData(0, Qt::UserRole, i);
            if (e.hidden()) {
                for (int c = 0; c < m_tree->columnCount(); ++c) {
                    it->setForeground(c, QColor(0x80, 0x80, 0x80));
                }
            }
            m_tree->addTopLevelItem(it);
        }
        m_syncing = false;
    }

    static QString showHideText(const Entry &e)
    {
        return e.family.mixed() ? tr7("Mixed") : (e.hidden() ? tr7("Hide") : tr7("Show"));
    }

    void relayoutCanvas()
    {
        const int w = m_scroll->viewport()->width();
        m_canvas->resize(w, qMax(m_scroll->viewport()->height(), m_canvas->heightFor(w)));
    }

    void setIconMode(int size)
    {
        m_iconSize = size;
        fontSettings().setValue(QStringLiteral("View/Icons"), size);
        if (size == 0) {
            fillTree();
            m_stack->setCurrentWidget(m_tree);
        } else {
            m_canvas->iconSize = size;
            relayoutCanvas();
            m_canvas->update();
            m_stack->setCurrentWidget(m_scroll);
        }
    }

    QList<int> selection() const
    {
        QList<int> s(m_canvas->selected.begin(), m_canvas->selected.end());
        std::sort(s.begin(), s.end());
        return s;
    }

    QList<Face> selectedFaces() const
    {
        QList<Face> faces;
        for (int i : selection()) {
            if (i >= 0 && i < m_shown.size()) {
                faces += m_shown.at(i).family.faces;
            }
        }
        return faces;
    }

    void updateSelection()
    {
        const QList<int> sel = selection();
        bool anyHidden = false, anyShown = false;
        for (const Face &f : selectedFaces()) {
            (f.hidden ? anyHidden : anyShown) = true;
        }
        m_preview->setVisible(!sel.isEmpty());
        m_delete->setVisible(!sel.isEmpty());
        m_show->setVisible(anyHidden);
        m_hide->setVisible(anyShown && !anyHidden);
        if (m_iconSize == 0 && !m_syncing) {
            m_syncing = true;
            for (int r = 0; r < m_tree->topLevelItemCount(); ++r) {
                QTreeWidgetItem *it = m_tree->topLevelItem(r);
                it->setSelected(m_canvas->selected.contains(it->data(0, Qt::UserRole).toInt()));
            }
            m_syncing = false;
        }
        if (sel.isEmpty()) {
            m_detailsIcon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-fonts-folder")).pixmap(48, 48));
            m_detailsText->setText(tr7("%1 items").arg(m_shown.size()));
            return;
        }
        if (sel.size() > 1) {
            m_detailsIcon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-font-files")).pixmap(48, 48));
            m_detailsText->setText(tr7("%1 items selected").arg(sel.size()));
            return;
        }
        const Entry &e = m_shown.at(sel.first());
        const Face &f = e.family.faces.first();
        m_detailsIcon->setPixmap(tilePicture(f, e.stack(), e.hidden(), 52));
        SfntFile sf(f.file, f.index);
        QString vendor = sf.name(8);
        if (vendor.isEmpty()) {
            vendor = sf.name(9);
        }
        QStringList styles;
        for (const Face &face : e.family.faces) {
            styles << styleName(face.style);
        }
        auto row = [](const QString &k, const QString &val) {
            return QStringLiteral("<td style='color:#5a6779; padding-right:6px' align='right'>%1</td><td style='padding-right:24px'>%2</td>").arg(k, val.toHtmlEscaped());
        };
        m_detailsText->setText(QStringLiteral("<table cellspacing='0' cellpadding='0'><tr><td rowspan='3' valign='top' style='padding-right:28px'>"
                                              "<span style='font-size:15px'>%1</span></td>%2%3</tr><tr>%4%5</tr><tr>%6%7</tr></table>")
                                   .arg(e.title().toHtmlEscaped(), row(tr7("Font style:"), styles.join(QStringLiteral("; "))),
                                        row(tr7("Category:"), f.symbol ? tr7("Symbol") : tr7("Text")),
                                        row(tr7("Show/hide:"), showHideText(e)), row(tr7("Designer/foundry:"), vendor),
                                        row(tr7("Designed for:"), f.scripts.join(QStringLiteral("; "))),
                                        row(tr7("Font embeddability:"), embeddability(sf.fsType()))));
    }

    void activate(int i)
    {
        if (i < 0 || i >= m_shown.size()) {
            return;
        }
        const Entry &e = m_shown.at(i);
        if (!e.isFace && e.family.faces.size() > 1) {
            openedFamily() = e.family.name;
            m_nav.open(FamilyView);
            return;
        }
        preview(e.family.faces.first());
    }

    void preview(const Face &f)
    {
        m_nav.spawn({QStringLiteral("--fontview"), f.file, QString::number(f.index)});
    }

    void previewSelected()
    {
        QList<Face> faces;
        for (int i : selection()) {
            faces << m_shown.at(i).family.faces.first();
        }
        if (faces.size() > 3) {
            const auto answer = QMessageBox::question(this, tr7("Font Preview"),
                                                      tr7("Open %1 font windows?\n\nEach selected font opens in its own window.").arg(faces.size()));
            if (answer != QMessageBox::Yes) {
                return;
            }
        }
        for (const Face &f : std::as_const(faces)) {
            preview(f);
        }
    }

    void removeSelected()
    {
        if (deleteFaces(this, selectedFaces())) {
            reload();
        }
    }

    void setSelectedHidden(bool hide)
    {
        setFacesHidden(selectedFaces(), hide);
        const QList<int> keep = selection();
        reload();
        for (int i : keep) {
            m_canvas->selected.insert(i);
        }
        m_canvas->update();
        updateSelection();
    }

    void selectAll()
    {
        for (int i = 0; i < m_shown.size(); ++i) {
            m_canvas->selected.insert(i);
        }
        m_canvas->update();
        updateSelection();
    }

    void copySelected()
    {
        QList<QUrl> urls;
        for (const Face &f : selectedFaces()) {
            const QUrl u = QUrl::fromLocalFile(f.file);
            if (!urls.contains(u)) {
                urls << u;
            }
        }
        if (urls.isEmpty()) {
            return;
        }
        auto *mime = new QMimeData;
        mime->setUrls(urls);
        QApplication::clipboard()->setMimeData(mime);
    }

    void paste()
    {
        const QMimeData *mime = QApplication::clipboard()->mimeData();
        if (!mime || !mime->hasUrls()) {
            return;
        }
        QStringList files;
        for (const QUrl &u : mime->urls()) {
            if (u.isLocalFile()) {
                files << u.toLocalFile();
            }
        }
        if (installFonts(this, files)) {
            reload();
        }
    }

    void contextMenu(int i, QPoint pos)
    {
        QMenu menu;
        if (i < 0) {
            QMenu *viewMenu = menu.addMenu(tr7("View"));
            viewMenu->addAction(tr7("Large icons"), this, [this] { setIconMode(96); });
            viewMenu->addAction(tr7("Medium Icons"), this, [this] { setIconMode(48); });
            viewMenu->addAction(tr7("Details"), this, [this] { setIconMode(0); });
            menu.addAction(tr7("Refresh"), this, [this] { reload(); });
            menu.addSeparator();
            QAction *paste = menu.addAction(tr7("Paste"), this, [this] { this->paste(); });
            paste->setEnabled(QApplication::clipboard()->mimeData() && QApplication::clipboard()->mimeData()->hasUrls());
            menu.exec(pos);
            return;
        }
        QAction *open = menu.addAction(tr7("Preview", "button"), this, [this] { previewSelected(); });
        QFont bold = open->font();
        bold.setBold(true);
        open->setFont(bold);
        if (!m_shown.at(i).isFace && m_shown.at(i).family.faces.size() > 1) {
            menu.addAction(tr7("Open"), this, [this, i] { activate(i); });
        }
        menu.addAction(tr7("Print"), this, [this] {
            for (int k : selection()) {
                const Face &f = m_shown.at(k).family.faces.first();
                m_nav.spawn({QStringLiteral("--fontview"), f.file, QString::number(f.index), QStringLiteral("--print")});
            }
        });
        menu.addSeparator();
        bool anyHidden = false;
        for (const Face &f : selectedFaces()) {
            anyHidden |= f.hidden;
        }
        menu.addAction(anyHidden ? tr7("Show") : tr7("Hide"), this, [this, anyHidden] { setSelectedHidden(!anyHidden); });
        menu.addSeparator();
        menu.addAction(tr7("Copy"), this, [this] { copySelected(); });
        menu.addSeparator();
        menu.addAction(tr7("Delete"), this, [this] { removeSelected(); });
        menu.exec(pos);
    }

    bool m_familyView;
    Nav m_nav;
    QList<Entry> m_all;
    QList<Entry> m_shown;
    QString m_filter;
    QString m_signature;
    int m_iconSize = 96;
    bool m_syncing = false;
    QToolBar *m_bar;
    QAction *m_preview;
    QAction *m_delete;
    QAction *m_show;
    QAction *m_hide;
    QAction *m_paste;
    QAction *m_organizeDelete;
    QAction *m_detailsToggle;
    QStackedWidget *m_stack;
    QScrollArea *m_scroll;
    IconCanvas *m_canvas;
    Win7Programs::DetailsTree *m_tree;
    QWidget *m_details;
    QLabel *m_detailsIcon;
    QLabel *m_detailsText;
};

/* ---------------- Font settings ---------------- */

inline QWidget *buildSettingsPage(const Nav &nav)
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("win7page"));
    page->setAttribute(Qt::WA_StyledBackground, true);
    page->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(32, 18, 30, 16);
    v->setSpacing(10);
    v->addWidget(Win7Programs::pageTitle(tr7("Change Font Settings")));
    v->addSpacing(6);
    auto *section = new QLabel(tr7("Installation Settings"));
    QFont sf = section->font();
    sf.setPixelSize(13);
    section->setFont(sf);
    section->setStyleSheet(QStringLiteral("color: #1e3287;"));
    v->addWidget(section);
    auto *text = Win7Programs::wrapped(tr7("To save space on your computer, you can install a shortcut to a font instead of the font file itself. If the font file becomes unavailable, you might not be able to use the font."));
    text->setContentsMargins(16, 0, 0, 0);
    v->addWidget(text);
    auto *shortcut = new QCheckBox(tr7("Allow fonts to be installed using a shortcut (advanced)"));
    shortcut->setChecked(installAsShortcut());
    auto *row = new QHBoxLayout;
    row->setContentsMargins(16, 0, 0, 0);
    row->addWidget(shortcut);
    row->addStretch(1);
    v->addLayout(row);
    v->addSpacing(14);
    auto *restore = new QPushButton(tr7("Restore default font settings"));
    auto *rrow = new QHBoxLayout;
    rrow->addWidget(restore);
    rrow->addStretch(1);
    v->addLayout(rrow);
    auto *note = Win7Programs::wrapped(tr7("Restoring the default settings shows all hidden fonts again."));
    note->setStyleSheet(QStringLiteral("color: #5a6779;"));
    v->addWidget(note);
    v->addStretch(1);
    auto *line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: #dfdfdf;"));
    v->addWidget(line);
    auto *btns = new QHBoxLayout;
    btns->addStretch(1);
    auto *ok = new QPushButton(QStringLiteral("OK"));
    auto *cancel = new QPushButton(tr7("Cancel"));
    for (QPushButton *b : {ok, cancel}) {
        b->setMinimumWidth(86);
        btns->addWidget(b);
    }
    ok->setEnabled(false); // like Windows 7: nothing to save yet
    v->addLayout(btns);
    auto *restoreAll = new bool(false);
    QObject::connect(page, &QObject::destroyed, [restoreAll] { delete restoreAll; });
    QObject::connect(shortcut, &QCheckBox::toggled, ok, [ok] { ok->setEnabled(true); });
    QObject::connect(restore, &QPushButton::clicked, page, [shortcut, ok, restoreAll] {
        shortcut->setChecked(false);
        *restoreAll = true;
        ok->setEnabled(true);
    });
    QObject::connect(ok, &QPushButton::clicked, page, [nav, shortcut, restoreAll] {
        fontSettings().setValue(QStringLiteral("Install/Shortcut"), shortcut->isChecked());
        fontSettings().sync();
        if (*restoreAll) {
            setHiddenFiles({});
        }
        nav.back();
    });
    QObject::connect(cancel, &QPushButton::clicked, page, [nav] { nav.back(); });
    return page;
}

} // namespace Win7Fonts

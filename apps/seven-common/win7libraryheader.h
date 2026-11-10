#pragma once
/*
 * The header of a Windows 7 library above the file list (Explorer and the Open / Save As dialogs):
 *
 *   Бібліотека "Документи"                                   Упорядкувати за:  Папка ▾
 *   Включає: 1 розташування            (in a subfolder: the folder name)
 *   ─────────────────────────────────────────────────────────────────────────────────
 *
 * Measured on Windows 7 (Explorer > Libraries > Documents): green #006e12 title in a 17 px font, grey #5a6779
 * second line with a #0066cc link, a #d6e5f5 line under it, 54 px tall, text 19 px from the left.
 * Header-only, no Q_OBJECT: used by dolphin-aero and kio-win7 alike.
 */
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include <KLocalizedString>

namespace Win7Library
{
struct Library {
    QString name; // "Документи"
    QString root; // ~/Documents
};

inline QString clean(const QString &p)
{
    QString c = QDir::cleanPath(p);
    while (c.size() > 1 && c.endsWith(QLatin1Char('/'))) {
        c.chop(1);
    }
    return c;
}

/** the four libraries of Windows 7, on the XDG user folders */
inline QList<Library> libraries()
{
    QList<Library> out;
    const struct {
        QStandardPaths::StandardLocation loc;
        const char *name;
    } known[] = {{QStandardPaths::DocumentsLocation, "Documents"},
                 {QStandardPaths::MusicLocation, "Music"},
                 {QStandardPaths::PicturesLocation, "Pictures"},
                 {QStandardPaths::MoviesLocation, "Videos"}};
    const QString home = clean(QDir::homePath());
    for (const auto &k : known) {
        const QString dir = clean(QStandardPaths::writableLocation(k.loc));
        if (!dir.isEmpty() && dir != home) {
            out << Library{i18nd("sevenplasma", k.name), dir};
        }
    }
    return out;
}

/** the library a local path belongs to (the folder itself or anything under it) */
inline Library libraryOf(const QUrl &url)
{
    if (!url.isLocalFile()) {
        return {};
    }
    const QString p = clean(url.toLocalFile());
    for (const Library &l : libraries()) {
        if (p == l.root || p.startsWith(l.root + QLatin1Char('/'))) {
            return l;
        }
    }
    return {};
}

/** Library locations dialog ("Розташування бібліотеки ...") */
inline void showLocations(QWidget *parent, const Library &lib)
{
    QDialog d(parent);
    d.setWindowTitle(i18nd("sevenplasma", "%1 Library Locations").arg(lib.name));
    auto *v = new QVBoxLayout(&d);
    auto *head = new QLabel(i18nd("sevenplasma", "<span style=\"font-size:12pt; color:#1e3287;\">Change how this library gathers its contents: \"%1\"</span><br>When you include a folder in a library, the files will appear in the library, but will continue to be stored in their original locations.")
                                .arg(lib.name));
    head->setWordWrap(true);
    v->addWidget(head);
    v->addWidget(new QLabel(i18nd("sevenplasma", "Library locations")));
    auto *list = new QListWidget;
    auto *it = new QListWidgetItem(QIcon::fromTheme(QStringLiteral("folder")),
                                   QFileInfo(lib.root).fileName() + QStringLiteral("\n") + lib.root + i18nd("sevenplasma", "   (Save location)"), list);
    Q_UNUSED(it)
    list->setIconSize(QSize(32, 32));
    v->addWidget(list, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("OK"));
    bb->button(QDialogButtonBox::Cancel)->setText(i18nd("sevenplasma", "Cancel"));
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    v->addWidget(bb);
    d.resize(520, 360);
    d.exec();
}

class Header : public QWidget
{
public:
    /** "Упорядкувати за" choice: 0 Папка, 1 Дата змінення, 2 Тип, 3 Ім'я */
    std::function<void(int)> arrange;

    explicit Header(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(54);
        setMouseTracking(true);
        setAutoFillBackground(false);
        hide();
    }
    /** shows the header for library folders, hides it elsewhere */
    void setUrl(const QUrl &url)
    {
        m_lib = libraryOf(url);
        m_sub = m_lib.root.isEmpty() || clean(url.toLocalFile()) == m_lib.root ? QString() : QFileInfo(clean(url.toLocalFile())).fileName();
        setVisible(!m_lib.root.isEmpty());
        update();
    }
    void setArrangement(int a)
    {
        m_arrange = a;
        update();
    }

protected:
    static QStringList arrangeNames()
    {
        return {i18nd("sevenplasma", "Folder"), i18nd("sevenplasma", "Date modified"), i18nd("sevenplasma", "Type"), i18nd("sevenplasma", "Name")};
    }
    QFont titleFont() const
    {
        QFont f = font();
        f.setPixelSize(17);
        return f;
    }
    QRect linkRect() const
    {
        const QFontMetrics fm(font());
        const int x = 19 + fm.horizontalAdvance(i18nd("sevenplasma", "Includes: "));
        return QRect(x, 31, fm.horizontalAdvance(locationsText()), fm.height());
    }
    QRect arrangeRect() const
    {
        const QFontMetrics fm(font());
        const int w = fm.horizontalAdvance(arrangeNames().value(m_arrange)) + 14;
        return QRect(width() - 26 - w, 18, w, fm.height());
    }
    QString locationsText() const
    {
        return i18nd("sevenplasma", "1 location");
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0xfa, 0xfb, 0xfc));
        p.setFont(titleFont());
        p.setPen(QColor(0x00, 0x6e, 0x12));
        p.drawText(QRect(19, 7, width() - 300, 24), Qt::AlignLeft | Qt::AlignVCenter, i18nd("sevenplasma", "%1 Library").arg(m_lib.name));
        p.setFont(font());
        const QFontMetrics fm(font());
        if (m_sub.isEmpty()) {
            p.setPen(QColor(0x5a, 0x67, 0x79));
            p.drawText(QPoint(19, 31 + fm.ascent()), i18nd("sevenplasma", "Includes: "));
            QFont u = font();
            u.setUnderline(m_hot == 1);
            p.setFont(u);
            p.setPen(QColor(0x00, 0x66, 0xcc));
            p.drawText(linkRect().bottomLeft() - QPoint(0, fm.descent()), locationsText());
            p.setFont(font());
        } else {
            p.setPen(QColor(0x5a, 0x67, 0x79));
            p.drawText(QPoint(19, 31 + fm.ascent()), m_sub);
        }
        // Arrange by: Folder ▾
        const QRect ar = arrangeRect();
        p.setPen(QColor(0x5a, 0x67, 0x79));
        const QString label = i18nd("sevenplasma", "Arrange by:");
        p.drawText(QPoint(ar.x() - 14 - fm.horizontalAdvance(label), ar.y() + fm.ascent()), label);
        QFont u = font();
        u.setUnderline(m_hot == 2);
        p.setFont(u);
        p.setPen(QColor(0x00, 0x66, 0xcc));
        p.drawText(QPoint(ar.x(), ar.y() + fm.ascent()), arrangeNames().value(m_arrange));
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF t(ar.right() - 4, ar.center().y() + 1);
        QPolygonF tri{t + QPointF(-4, -2), t + QPointF(4, -2), t + QPointF(0, 2)};
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x00, 0x66, 0xcc));
        p.drawPolygon(tri);
        // the line under the header
        p.setPen(QColor(0xd6, 0xe5, 0xf5));
        p.drawLine(0, height() - 1, width(), height() - 1);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const QPoint pt = e->position().toPoint();
        const int hot = (m_sub.isEmpty() && linkRect().contains(pt)) ? 1 : arrangeRect().adjusted(-2, -2, 2, 2).contains(pt) ? 2 : 0;
        if (hot != m_hot) {
            m_hot = hot;
            setCursor(hot ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hot = 0;
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) {
            return;
        }
        if (m_hot == 1) {
            showLocations(window(), m_lib);
        } else if (m_hot == 2) {
            QMenu m(this);
            const QStringList names = arrangeNames();
            for (int i = 0; i < names.size(); ++i) {
                QAction *a = m.addAction(names[i]);
                a->setCheckable(true);
                a->setChecked(i == m_arrange);
                a->setData(i);
            }
            if (QAction *a = m.exec(mapToGlobal(arrangeRect().bottomLeft() + QPoint(0, 2)))) {
                m_arrange = a->data().toInt();
                update();
                if (arrange) {
                    arrange(m_arrange);
                }
            }
        }
    }

private:
    Library m_lib;
    QString m_sub;
    int m_arrange = 0;
    int m_hot = 0;
};
}

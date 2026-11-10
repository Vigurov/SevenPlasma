/*
 * Windows Photo Viewer of Windows 7 ("Засіб перегляду фотографій Windows"): the blue Explorer command bar
 * (File, Print, E-mail, Burn, Open), the picture on a light background, and the bottom bar with the
 * original buttons from PhotoViewer.dll (zoom, actual size, previous, slide show, next, rotate, delete).
 * Pictures: ~/.local/share/seven-photoviewer/<resource id>.png and ~/.local/share/seven-aero/cmd-*.png.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <KApplicationTrader>
#include <KIO/ApplicationLauncherJob>
#include <KIO/JobUiDelegateFactory>
#include <QAbstractButton>
#include <QApplication>
#include <QCollator>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QImageWriter>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QProcess>
#include <QScreen>
#include <QSlider>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>

namespace
{
const QString TITLE = QStringLiteral("Засіб перегляду фотографій Windows");
const QColor BACKGROUND(0xee, 0xf3, 0xfa);

QString res(const QString &name)
{
    return QDir::homePath() + QStringLiteral("/.local/share/seven-photoviewer/") + name + QStringLiteral(".png");
}
QString aero(const QString &name)
{
    return QDir::homePath() + QStringLiteral("/.local/share/seven-aero/") + name + QStringLiteral(".png");
}

/** a button drawn from a PhotoViewer.dll strip of 4 frames: disabled, normal, hot, pressed */
class SpriteButton : public QAbstractButton
{
public:
    SpriteButton(const QString &id, const QString &tip, QWidget *parent)
        : QAbstractButton(parent)
        , m_strip(res(id))
    {
        setToolTip(tip);
        setCursor(Qt::ArrowCursor);
        setFixedSize(m_strip.width() / 4, m_strip.height());
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        int f = 1;
        if (!isEnabled()) {
            f = 0;
        } else if (isDown()) {
            f = 3;
        } else if (underMouse()) {
            f = 2;
        }
        QPainter p(this);
        const int w = m_strip.width() / 4;
        p.drawPixmap(0, 0, m_strip, f * w, 0, w, m_strip.height());
    }
    void enterEvent(QEnterEvent *e) override
    {
        update();
        QAbstractButton::enterEvent(e);
    }
    void leaveEvent(QEvent *e) override
    {
        update();
        QAbstractButton::leaveEvent(e);
    }

private:
    QPixmap m_strip;
};

/** the rounded white "pill" behind the buttons at the bottom */
class Pill : public QWidget
{
public:
    explicit Pill(QWidget *parent)
        : QWidget(parent)
        , m_left(res(QStringLiteral("4146")))
        , m_right(res(QStringLiteral("4147")))
        , m_mid(res(QStringLiteral("4148")))
        , m_sep(res(QStringLiteral("4149")))
    {
    }
    QList<int> separators;   // x positions

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const int y = (height() - m_left.height()) / 2;
        p.drawPixmap(0, y, m_left);
        p.drawTiledPixmap(m_left.width(), y, width() - m_left.width() - m_right.width(), m_mid.height(), m_mid);
        p.drawPixmap(width() - m_right.width(), y, m_right);
        for (int x : std::as_const(separators)) {
            p.drawPixmap(x - m_sep.width() / 2, (height() - m_sep.height()) / 2, m_sep);
        }
    }

private:
    QPixmap m_left, m_right, m_mid, m_sep;
};

/** the picture: fitted into the window (never enlarged) unless zoomed, dragged when bigger than the view */
class ImageView : public QWidget
{
public:
    explicit ImageView(QWidget *parent)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }
    void setImage(const QImage &img)
    {
        m_img = img;
        m_zoom = 0;
        m_offset = {};
        update();
    }
    const QImage &image() const
    {
        return m_img;
    }
    double fitScale() const
    {
        if (m_img.isNull()) {
            return 1;
        }
        return std::min(1.0, std::min(double(width() - 20) / m_img.width(), double(height() - 20) / m_img.height()));
    }
    double scale() const
    {
        return m_zoom > 0 ? m_zoom : fitScale();
    }
    void setScale(double s)
    {
        m_zoom = std::clamp(s, fitScale(), 16.0);
        if (std::abs(m_zoom - fitScale()) < 1e-3) {
            m_zoom = 0;
            m_offset = {};
        }
        update();
        if (onZoom) {
            onZoom();
        }
    }
    void actualSize()
    {
        setScale(std::abs(scale() - 1.0) < 1e-3 ? fitScale() : 1.0);
    }
    std::function<void()> onZoom;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), BACKGROUND);
        if (m_img.isNull()) {
            return;
        }
        const double s = scale();
        const QSizeF sz(m_img.width() * s, m_img.height() * s);
        QRectF r(QPointF((width() - sz.width()) / 2 + m_offset.x(), (height() - sz.height()) / 2 + m_offset.y()), sz);
        p.setRenderHint(QPainter::SmoothPixmapTransform, s < 4);
        p.drawImage(r, m_img);
    }
    void wheelEvent(QWheelEvent *e) override
    {
        setScale(scale() * (e->angleDelta().y() > 0 ? 1.25 : 0.8));
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        m_drag = e->position();
        if (m_zoom > 0) {
            setCursor(Qt::ClosedHandCursor);
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_zoom > 0 && (e->buttons() & Qt::LeftButton)) {
            m_offset += e->position() - m_drag;
            m_drag = e->position();
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent *) override
    {
        unsetCursor();
    }
    void resizeEvent(QResizeEvent *) override
    {
        if (m_zoom > 0 && m_zoom < fitScale()) {
            m_zoom = 0;
        }
    }

private:
    QImage m_img;
    double m_zoom = 0;     // 0 = fit
    QPointF m_offset;
    QPointF m_drag;
};

/** full screen slide show on black, 4 s per picture */
class SlideShow : public QWidget
{
public:
    SlideShow(const QStringList &files, int start)
        : m_files(files)
        , m_i(start)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setCursor(Qt::BlankCursor);
        connect(&m_timer, &QTimer::timeout, this, [this]() {
            m_i = (m_i + 1) % m_files.size();
            load();
        });
        m_timer.start(4000);
        load();
        showFullScreen();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        if (!m_img.isNull()) {
            const QSize s = m_img.size().scaled(size(), Qt::KeepAspectRatio);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(QRect(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s), m_img);
        }
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Right || e->key() == Qt::Key_Space) {
            m_i = (m_i + 1) % m_files.size();
            load();
            m_timer.start();
        } else if (e->key() == Qt::Key_Left) {
            m_i = (m_i + m_files.size() - 1) % m_files.size();
            load();
            m_timer.start();
        } else {
            close();
        }
    }
    void mousePressEvent(QMouseEvent *) override
    {
        close();
    }

private:
    void load()
    {
        QImageReader r(m_files.value(m_i));
        r.setAutoTransform(true);
        m_img = r.read();
        update();
    }
    QStringList m_files;
    int m_i;
    QImage m_img;
    QTimer m_timer;
};

class PhotoViewer : public QMainWindow
{
public:
    explicit PhotoViewer(const QString &file)
    {
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-photoviewer")));
        auto *central = new QWidget(this);
        auto *v = new QVBoxLayout(central);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        m_view = new ImageView(central);
        v->addWidget(m_view, 1);
        v->addWidget(makeBottomBar(central));
        central->setAutoFillBackground(true);
        QPalette pal = central->palette();
        pal.setColor(QPalette::Window, BACKGROUND);
        central->setPalette(pal);
        setCentralWidget(central);
        makeCommandBar();
        m_view->onZoom = [this]() {
            if (m_zoomSlider) {
                QSignalBlocker b(m_zoomSlider);
                m_zoomSlider->setValue(sliderFromScale(m_view->scale()));
            }
        };
        open(file);
        const QSize avail = screen()->availableSize();
        resize(std::min(1100, avail.width() * 3 / 4), std::min(780, avail.height() * 3 / 4));
    }

    void open(const QString &file)
    {
        const QFileInfo fi(file);
        m_file = fi.absoluteFilePath();
        // all pictures of the folder, in Explorer's order (natural sort)
        const QDir dir = fi.dir();
        QStringList filters;
        for (const QByteArray &f : QImageReader::supportedImageFormats()) {
            filters << QStringLiteral("*.") + QString::fromLatin1(f);
        }
        QStringList names = dir.entryList(filters, QDir::Files | QDir::Readable, QDir::NoSort);
        QCollator coll;
        coll.setNumericMode(true);
        coll.setCaseSensitivity(Qt::CaseInsensitive);
        std::sort(names.begin(), names.end(), [&coll](const QString &a, const QString &b) {
            return coll.compare(a, b) < 0;
        });
        m_files.clear();
        for (const QString &n : std::as_const(names)) {
            m_files << dir.absoluteFilePath(n);
        }
        if (!m_files.contains(m_file) && fi.exists()) {
            m_files << m_file;
        }
        m_index = m_files.indexOf(m_file);
        load();
    }

protected:
    void keyPressEvent(QKeyEvent *e) override
    {
        switch (e->key()) {
        case Qt::Key_Right:
        case Qt::Key_Down:
        case Qt::Key_PageDown:
            step(1);
            break;
        case Qt::Key_Left:
        case Qt::Key_Up:
        case Qt::Key_PageUp:
            step(-1);
            break;
        case Qt::Key_Delete:
            deleteFile();
            break;
        case Qt::Key_F11:
            slideShow();
            break;
        case Qt::Key_Escape:
            close();
            break;
        default:
            if (e->modifiers() & Qt::ControlModifier) {
                if (e->key() == Qt::Key_Comma) {
                    rotate(-90);
                } else if (e->key() == Qt::Key_Period) {
                    rotate(90);
                } else if (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal) {
                    m_view->setScale(m_view->scale() * 1.25);
                } else if (e->key() == Qt::Key_Minus) {
                    m_view->setScale(m_view->scale() * 0.8);
                } else if (e->key() == Qt::Key_0) {
                    m_view->actualSize();
                }
            }
            QMainWindow::keyPressEvent(e);
        }
    }

private:
    QWidget *makeBottomBar(QWidget *parent)
    {
        auto *bar = new QWidget(parent);
        bar->setFixedHeight(58);
        auto *outer = new QHBoxLayout(bar);
        outer->setContentsMargins(0, 2, 0, 6);
        outer->addStretch();
        auto *pill = new Pill(bar);
        pill->setFixedHeight(50);
        auto *h = new QHBoxLayout(pill);
        h->setContentsMargins(14, 0, 14, 0);
        h->setSpacing(2);
        auto *zoom = new SpriteButton(QStringLiteral("4019"), QStringLiteral("Змінити розмір відображення"), pill);
        auto *actual = new SpriteButton(QStringLiteral("4124"), QStringLiteral("Фактичний розмір (Ctrl+Alt+0)"), pill);
        m_prev = new SpriteButton(QStringLiteral("4006"), QStringLiteral("Попереднє (стрілка вліво)"), pill);
        auto *show = new SpriteButton(QStringLiteral("4000"), QStringLiteral("Відтворити показ слайдів (F11)"), pill);
        m_next = new SpriteButton(QStringLiteral("4003"), QStringLiteral("Наступне (стрілка вправо)"), pill);
        auto *ccw = new SpriteButton(QStringLiteral("4009"), QStringLiteral("Повернути проти годинникової стрілки (Ctrl+,)"), pill);
        auto *cw = new SpriteButton(QStringLiteral("4012"), QStringLiteral("Повернути за годинниковою стрілкою (Ctrl+.)"), pill);
        auto *del = new SpriteButton(QStringLiteral("4015"), QStringLiteral("Видалити (Del)"), pill);
        h->addWidget(zoom);
        h->addWidget(actual);
        h->addSpacing(10);
        h->addWidget(m_prev);
        h->addSpacing(-6);
        h->addWidget(show);
        h->addSpacing(-6);
        h->addWidget(m_next);
        h->addSpacing(10);
        h->addWidget(ccw);
        h->addWidget(cw);
        h->addSpacing(10);
        h->addWidget(del);
        outer->addWidget(pill);
        outer->addStretch();
        // separators between the groups, placed once the layout knows the button positions
        QTimer::singleShot(0, pill, [pill, actual, m_prev = m_prev, m_next = m_next, cw, del]() {
            pill->separators = {(actual->geometry().right() + m_prev->geometry().left()) / 2,
                                (m_next->geometry().right() + cw->geometry().left() - cw->width()) / 2 + 1,
                                (cw->geometry().right() + del->geometry().left()) / 2};
            pill->update();
        });
        connect(m_prev, &QAbstractButton::clicked, this, [this]() {
            step(-1);
        });
        connect(m_next, &QAbstractButton::clicked, this, [this]() {
            step(1);
        });
        connect(show, &QAbstractButton::clicked, this, &PhotoViewer::slideShow);
        connect(ccw, &QAbstractButton::clicked, this, [this]() {
            rotate(-90);
        });
        connect(cw, &QAbstractButton::clicked, this, [this]() {
            rotate(90);
        });
        connect(del, &QAbstractButton::clicked, this, &PhotoViewer::deleteFile);
        connect(actual, &QAbstractButton::clicked, this, [this]() {
            m_view->actualSize();
        });
        // the zoom button drops down a vertical slider, like Windows 7
        auto *zoomMenu = new QMenu(this);
        auto *wa = new QWidgetAction(zoomMenu);
        m_zoomSlider = new QSlider(Qt::Vertical);
        m_zoomSlider->setRange(0, 100);
        m_zoomSlider->setFixedHeight(150);
        connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int v) {
            m_view->setScale(scaleFromSlider(v));
        });
        wa->setDefaultWidget(m_zoomSlider);
        zoomMenu->addAction(wa);
        connect(zoom, &QAbstractButton::clicked, this, [zoom, zoomMenu]() {
            zoomMenu->popup(zoom->mapToGlobal(QPoint(0, -zoomMenu->sizeHint().height())));
        });
        return bar;
    }

    double scaleFromSlider(int v) const
    {
        const double lo = std::log(m_view->fitScale()), hi = std::log(16.0);
        return std::exp(lo + (hi - lo) * v / 100.0);
    }
    int sliderFromScale(double s) const
    {
        const double lo = std::log(m_view->fitScale()), hi = std::log(16.0);
        return hi > lo ? int(std::round((std::log(s) - lo) / (hi - lo) * 100)) : 0;
    }

    void makeCommandBar()
    {
        auto *bar = new QToolBar(this);
        bar->setMovable(false);
        bar->setFloatable(false);
        bar->setContextMenuPolicy(Qt::PreventContextMenu);
        bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
        bar->setStyleSheet(QStringLiteral(
                               "QToolBar { border-image: url(%1) 3 1 3 1 stretch; border-width: 3px 1px 3px 1px; padding: 0px 4px; spacing: 0px; min-height: 25px; }"
                               "QToolButton { border: 3px solid transparent; border-width: 6px 3px; padding: -3px 6px; color: #1e395b; background: transparent; }"
                               "QToolButton:hover { border-image: url(%2) 6 3 6 3 stretch; border-width: 6px 3px; }"
                               "QToolButton:pressed, QToolButton:open { border-image: url(%3) 6 3 6 3 stretch; border-width: 6px 3px; }"
                               "QToolButton::menu-indicator { image: url(%4); subcontrol-position: right center; right: 4px; }"
                               "QToolButton[popupMode=\"2\"] { padding-right: 20px; }")
                               .arg(aero(QStringLiteral("cmd-bg")), aero(QStringLiteral("cmd-button-hot")), aero(QStringLiteral("cmd-button-pressed")),
                                    aero(QStringLiteral("cmd-arrow"))));
        auto addMenu = [bar](const QString &text) {
            auto *b = new QToolButton(bar);
            b->setText(text);
            b->setPopupMode(QToolButton::InstantPopup);
            auto *m = new QMenu(b);
            b->setMenu(m);
            bar->addWidget(b);
            return m;
        };
        QMenu *file = addMenu(QStringLiteral("Файл"));
        file->addAction(QStringLiteral("Створити копію..."), this, &PhotoViewer::makeCopy);
        file->addAction(QStringLiteral("Видалити"), this, &PhotoViewer::deleteFile);
        file->addSeparator();
        file->addAction(QStringLiteral("Властивості"), this, [this]() {
            QProcess::startDetached(QDir::homePath() + QStringLiteral("/.local/bin/seven-control"), {QStringLiteral("--properties"), m_file});
        });
        file->addSeparator();
        file->addAction(QStringLiteral("Вихід"), this, &QWidget::close);
        QMenu *print = addMenu(QStringLiteral("Друк"));
        print->addAction(QStringLiteral("Друк..."), this, &PhotoViewer::printImage);
        auto *mail = new QToolButton(bar);
        mail->setText(QStringLiteral("Надіслати ел. поштою"));
        connect(mail, &QToolButton::clicked, this, [this]() {
            QProcess::startDetached(QStringLiteral("xdg-email"), {QStringLiteral("--attach"), m_file});
        });
        bar->addWidget(mail);
        QMenu *burn = addMenu(QStringLiteral("Записати"));
        burn->addAction(QStringLiteral("Диск із даними"))->setEnabled(false);
        m_openMenu = addMenu(QStringLiteral("Відкрити"));
        connect(m_openMenu, &QMenu::aboutToShow, this, &PhotoViewer::fillOpenMenu);
        addToolBar(Qt::TopToolBarArea, bar);
    }

    void fillOpenMenu()
    {
        m_openMenu->clear();
        const QString mime = QMimeDatabase().mimeTypeForFile(m_file).name();
        for (const KService::Ptr &s : KApplicationTrader::queryByMimeType(mime)) {
            if (s->desktopEntryName() == QLatin1String("seven-photoviewer")) {
                continue;
            }
            m_openMenu->addAction(QIcon::fromTheme(s->icon()), s->name(), this, [this, s]() {
                auto *job = new KIO::ApplicationLauncherJob(s);
                job->setUrls({QUrl::fromLocalFile(m_file)});
                job->start();
            });
        }
        m_openMenu->addSeparator();
        m_openMenu->addAction(QStringLiteral("Вибрати програму..."), this, [this]() {
            auto *job = new KIO::ApplicationLauncherJob();
            job->setUrls({QUrl::fromLocalFile(m_file)});
            job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, this));
            job->start();
        });
    }

    void load()
    {
        if (m_index < 0 || m_index >= m_files.size()) {
            m_view->setImage({});
            setWindowTitle(TITLE);
            return;
        }
        m_file = m_files.at(m_index);
        QImageReader r(m_file);
        r.setAutoTransform(true);
        m_view->setImage(r.read());
        setWindowTitle(QFileInfo(m_file).fileName() + QStringLiteral(" - ") + TITLE);
        m_prev->setEnabled(m_files.size() > 1);
        m_next->setEnabled(m_files.size() > 1);
    }

    void step(int d)
    {
        if (m_files.isEmpty()) {
            return;
        }
        m_index = (m_index + d + m_files.size()) % m_files.size();
        load();
    }

    void rotate(int degrees)
    {
        if (m_view->image().isNull()) {
            return;
        }
        // Windows 7 turns the file itself round
        const QImage img = m_view->image().transformed(QTransform().rotate(degrees), Qt::SmoothTransformation);
        QImageWriter w(m_file);
        w.setQuality(95);
        if (!w.canWrite() || !w.write(img)) {
            QMessageBox::warning(this, TITLE, QStringLiteral("Не вдалося повернути цей рисунок."));
            return;
        }
        m_view->setImage(img);
    }

    void deleteFile()
    {
        if (m_file.isEmpty()) {
            return;
        }
        QMessageBox box(QMessageBox::Warning, QStringLiteral("Видалення файлу"),
                        QStringLiteral("Ви дійсно бажаєте перемістити цей файл до кошика?\n\n%1").arg(QFileInfo(m_file).fileName()),
                        QMessageBox::Yes | QMessageBox::No, this);
        box.setButtonText(QMessageBox::Yes, QStringLiteral("Так"));
        box.setButtonText(QMessageBox::No, QStringLiteral("Ні"));
        if (box.exec() != QMessageBox::Yes) {
            return;
        }
        if (!QFile::moveToTrash(m_file)) {
            QMessageBox::warning(this, TITLE, QStringLiteral("Не вдалося видалити файл."));
            return;
        }
        m_files.removeAt(m_index);
        if (m_files.isEmpty()) {
            close();
            return;
        }
        m_index = std::min<int>(m_index, m_files.size() - 1);
        load();
    }

    void makeCopy()
    {
        const QFileInfo fi(m_file);
        const QString to = QFileDialog::getSaveFileName(this, QStringLiteral("Створити копію"),
                                                        fi.absolutePath() + QStringLiteral("/") + fi.completeBaseName() + QStringLiteral(" - копія.") + fi.suffix());
        if (!to.isEmpty()) {
            QFile::copy(m_file, to);
        }
    }

    void printImage()
    {
        QPrinter printer;
        QPrintDialog dlg(&printer, this);
        dlg.setWindowTitle(QStringLiteral("Друк рисунків"));
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        QPainter p(&printer);
        const QRect page = p.viewport();
        const QSize s = m_view->image().size().scaled(page.size(), Qt::KeepAspectRatio);
        p.drawImage(QRect(page.x() + (page.width() - s.width()) / 2, page.y() + (page.height() - s.height()) / 2, s.width(), s.height()), m_view->image());
    }

    void slideShow()
    {
        if (!m_files.isEmpty()) {
            new SlideShow(m_files, m_index);
        }
    }

    ImageView *m_view;
    SpriteButton *m_prev = nullptr;
    SpriteButton *m_next = nullptr;
    QSlider *m_zoomSlider = nullptr;
    QMenu *m_openMenu = nullptr;
    QString m_file;
    QStringList m_files;
    int m_index = -1;
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("seven-photoviewer"));
    app.setApplicationDisplayName(QString());
    app.setDesktopFileName(QStringLiteral("seven-photoviewer"));
    QString file = app.arguments().value(1);
    if (file.startsWith(QLatin1String("file://"))) {
        file = QUrl(file).toLocalFile();
    }
    if (file.isEmpty()) {
        file = QFileDialog::getOpenFileName(nullptr, QStringLiteral("Відкрити"), QDir::homePath() + QStringLiteral("/Pictures"),
                                            QStringLiteral("Рисунки (*.jpg *.jpeg *.png *.bmp *.gif *.tif *.tiff *.webp *.ico)"));
        if (file.isEmpty()) {
            return 0;
        }
    }
    PhotoViewer w(file);
    w.show();
    return app.exec();
}

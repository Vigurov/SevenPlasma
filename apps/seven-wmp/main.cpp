#include "win7kwin.h"
#include "win7i18n.h"
/*
 * Windows Media Player 12 ("Програвач Windows Media") of Windows 7 for KDE: library mode and Now Playing mode,
 * pictures from the original wmploc.dll (~/.local/share/seven-wmp), playback with Qt Multimedia (FFmpeg).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "engine.h"
#include "library.h"
#include "libraryview.h"
#include "mpris.h"
#include "options.h"
#include "skin.h"
#include "eq.h"
#include "autoplaylist.h"
#include "devices.h"
#include <KIO/MkpathJob>
#include "nowplaying.h"
#include "transport.h"

#include <QActionGroup>
#include <QTemporaryDir>
#include <QComboBox>
#include <QGroupBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QCloseEvent>
#include <QCommandLineParser>
#include <QFileDialog>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLocalServer>
#include <QLocalSocket>
#include <QDBusInterface>
#include <QDBusReply>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QScreen>
#include <QShortcut>
#include <QStandardItemModel>
#include <QWindow>

namespace
{
const QString APP_TITLE = tr7("Windows Media Player");

QString playlistsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + QStringLiteral("/Playlists");
}

Library *g_library = nullptr;

QList<QUrl> readPlaylist(const QString &file)
{
    if (file.endsWith(QLatin1String(".autopl")) && g_library) {
        return AutoPl::evaluate(file, g_library);
    }
    QList<QUrl> out;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        return out;
    }
    const QString base = QFileInfo(file).absolutePath();
    const QString text = QString::fromUtf8(f.readAll());
    if (file.endsWith(QLatin1String(".wpl"), Qt::CaseInsensitive)) {
        static const QRegularExpression re(QStringLiteral("<media[^>]*src=\"([^\"]+)\""));
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            QString s = it.next().captured(1).replace(QStringLiteral("&amp;"), QStringLiteral("&"));
            s.replace(QLatin1Char('\\'), QLatin1Char('/'));
            out << QUrl::fromLocalFile(QFileInfo(s).isAbsolute() ? s : base + QLatin1Char('/') + s);
        }
        return out;
    }
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (line.contains(QStringLiteral("://"))) {
            out << QUrl(line);
        } else {
            out << QUrl::fromLocalFile(QFileInfo(line).isAbsolute() ? line : base + QLatin1Char('/') + line);
        }
    }
    return out;
}
}

// ----------------------------------------------------------------------------------------------------------------

/** rows of the list pane: ▸ marks the playing item (blue), the length at the right */
class ListPaneDelegate : public QStyledItemDelegate
{
public:
    ListPaneDelegate(Engine *engine, QObject *parent)
        : QStyledItemDelegate(parent)
        , m_engine(engine)
    {
    }
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(100, 19);
    }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &idx) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, idx);
        const QRect r = opt.rect;
        if (opt.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            p->fillRect(r, opt.state & QStyle::State_Selected ? QColor(0xcc, 0xe4, 0xfc, 210) : QColor(0xe5, 0xf1, 0xfd, 170));
        }
        const bool isCurrent = idx.row() == (current ? current() : m_engine->index());
        const QColor c = isCurrent ? QColor(0x33, 0x66, 0xcc) : Wmp::TEXT;
        if (isCurrent) {
            QPainterPath tri;
            const int x = r.x() + 3, y = r.center().y();
            tri.moveTo(x, y - 3);
            tri.lineTo(x + 3, y);
            tri.lineTo(x, y + 3);
            tri.closeSubpath();
            p->save();
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(Qt::NoPen);
            p->setBrush(c);
            p->drawPath(tri);
            p->restore();
        }
        const QString len = idx.data(Qt::UserRole + 1).toString();
        const int lw = opt.fontMetrics.horizontalAdvance(len) + 8;
        p->setPen(QColor(0xe0, 0xe8, 0xf2));
        p->drawLine(r.right() - 44, r.top(), r.right() - 44, r.bottom());
        p->setPen(c);
        p->drawText(r.adjusted(12, 0, -qMax(lw, 50), 0), Qt::AlignLeft | Qt::AlignVCenter,
                    opt.fontMetrics.elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideRight, r.width() - 12 - qMax(lw, 50)));
        p->drawText(r.adjusted(0, 0, -6, 0), Qt::AlignRight | Qt::AlignVCenter, len);
    }

public:
    std::function<int()> current;

private:
    Engine *m_engine;
};

/** the music model hands the files of dragged rows to drops (list pane, Explorer) */
class MusicModel : public QStandardItemModel
{
public:
    using QStandardItemModel::QStandardItemModel;
    QStringList mimeTypes() const override
    {
        return {QStringLiteral("text/uri-list")};
    }
    QMimeData *mimeData(const QModelIndexList &indexes) const override
    {
        QList<QUrl> urls;
        QSet<int> rows;
        for (const QModelIndex &i : indexes) {
            if (rows.contains(i.row())) {
                continue;
            }
            rows.insert(i.row());
            const QString p = index(i.row(), 0).data(MusicDelegate::PathRole).toString();
            if (!p.isEmpty()) {
                urls << QUrl::fromLocalFile(p);
            }
        }
        auto *m = new QMimeData;
        m->setUrls(urls);
        return m;
    }
};

/** tiles / thumbnails: dragged items give their files */
class IconList : public Transparent<QListWidget>
{
public:
    std::function<QList<QUrl>(QListWidgetItem *)> urlsOf;
    using Transparent<QListWidget>::Transparent;

protected:
    QStringList mimeTypes() const override
    {
        return {QStringLiteral("text/uri-list")};
    }
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override
    {
        QList<QUrl> urls;
        for (auto *it : items) {
            urls << (urlsOf ? urlsOf(it) : QList<QUrl>());
        }
        auto *m = new QMimeData;
        m->setUrls(urls);
        return m;
    }
};

/** the list widget of the pane: takes files dragged from the library or Explorer */
class DropList : public Transparent<QListWidget>
{
public:
    std::function<void(const QList<QUrl> &)> dropped;
    explicit DropList(QWidget *parent)
        : Transparent<QListWidget>(parent)
    {
        setAcceptDrops(true);
        viewport()->setAcceptDrops(true);
        setDropIndicatorShown(true);
    }

protected:
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (e->mimeData()->hasUrls() && e->source() != this) {
            e->acceptProposedAction();
        } else {
            Transparent<QListWidget>::dragEnterEvent(e);
        }
    }
    void dragMoveEvent(QDragMoveEvent *e) override
    {
        if (e->mimeData()->hasUrls() && e->source() != this) {
            e->acceptProposedAction();
        } else {
            Transparent<QListWidget>::dragMoveEvent(e);
        }
    }
    void dropEvent(QDropEvent *e) override
    {
        if (e->mimeData()->hasUrls() && e->source() != this) {
            if (dropped) {
                dropped(e->mimeData()->urls());
            }
            e->acceptProposedAction();
        } else {
            Transparent<QListWidget>::dropEvent(e);
        }
    }
};

/** the right pane: Play (Now Playing list), Burn (burn list) and Sync (sync list) */
class ListPane : public QWidget
{
    Q_OBJECT
public:
    enum Mode { PlayMode, BurnMode, SyncMode };

    ListPane(Engine *engine, Library *lib, QWidget *parent)
        : QWidget(parent)
        , m_engine(engine)
        , m_lib(lib)
    {
        setMinimumWidth(200);
        setAcceptDrops(true);
        auto *l = new QVBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        auto *bar = new QWidget(this);
        bar->setFixedHeight(29);
        auto *bl = new QHBoxLayout(bar);
        bl->setContentsMargins(4, 0, 4, 0);
        m_action = new QToolButton(bar);
        m_action->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_action->setAutoRaise(true);
        m_clear = new QToolButton(bar);
        m_clear->setText(tr7("Clear list"));
        m_clear->setAutoRaise(true);
        bl->addWidget(m_action);
        bl->addWidget(m_clear);
        bl->addStretch(1);
        // burn options: Audio CD / Data CD or DVD, eject after burning
        m_burnOpts = new QToolButton(bar);
        m_burnOpts->setAutoRaise(true);
        m_burnOpts->setIcon(Res::icon(7702));
        m_burnOpts->setToolTip(tr7("Burn options"));
        m_burnOpts->setPopupMode(QToolButton::InstantPopup);
        auto *bm = new QMenu(m_burnOpts);
        auto *grp = new QActionGroup(bm);
        auto *aAudio = bm->addAction(tr7("Audio CD"));
        auto *aData = bm->addAction(tr7("Data CD or DVD"));
        for (auto *x : {aAudio, aData}) {
            x->setCheckable(true);
            grp->addAction(x);
        }
        aAudio->setChecked(true);
        bm->addSeparator();
        auto *aEject = bm->addAction(tr7("Eject disc after burning"));
        aEject->setCheckable(true);
        aEject->setChecked(true);
        m_burnOpts->setMenu(bm);
        connect(aAudio, &QAction::triggered, this, [this] {
            m_audioBurn = true;
            m_head->update();
            refresh();
        });
        connect(aData, &QAction::triggered, this, [this] {
            m_audioBurn = false;
            m_head->update();
            refresh();
        });
        connect(aEject, &QAction::toggled, this, [this](bool on) {
            m_ejectAfter = on;
        });
        bl->addWidget(m_burnOpts);
        l->addWidget(bar);
        m_head = new QWidget(this);
        m_head->setFixedHeight(102);
        l->addWidget(m_head);
        m_head->installEventFilter(this);
        m_list = new DropList(this);
        m_list->setDragDropMode(QAbstractItemView::DragDrop);
        m_list->setDefaultDropAction(Qt::MoveAction);
        m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_list->setStyleSheet(QStringLiteral("QListWidget { background: transparent; }"));
        auto *delegate = new ListPaneDelegate(engine, m_list);
        delegate->current = [this] {
            return m_mode == PlayMode ? m_engine->index() : -1;
        };
        m_list->setItemDelegate(delegate);
        m_list->setMouseTracking(true);
        m_list->dropped = [this](const QList<QUrl> &urls) {
            addUrls(urls);
        };
        l->addWidget(m_list, 1);
        m_count = new QLabel(this);
        m_count->setContentsMargins(8, 4, 8, 6);
        l->addWidget(m_count);
        connect(engine, &Engine::listChanged, this, &ListPane::refresh);
        connect(engine, &Engine::currentChanged, this, &ListPane::refresh);
        connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
            if (m_mode == PlayMode) {
                m_engine->go(m_list->row(it));
            } else {
                m_engine->playList(m_mode == BurnMode ? m_burn : m_sync, m_list->row(it));
            }
        });
        connect(m_clear, &QToolButton::clicked, this, [this] {
            if (m_mode == PlayMode) {
                m_engine->clear();
            } else {
                (m_mode == BurnMode ? m_burn : m_sync).clear();
                refresh();
            }
        });
        connect(m_action, &QToolButton::clicked, this, [this] {
            if (m_mode == PlayMode) {
                save();
            } else if (m_mode == BurnMode) {
                startBurn();
            } else {
                startSync();
            }
        });
        auto *del = new QShortcut(QKeySequence::Delete, m_list, nullptr, nullptr, Qt::WidgetShortcut);
        connect(del, &QShortcut::activated, this, [this] {
            QList<int> rows;
            for (auto *it : m_list->selectedItems()) {
                rows << m_list->row(it);
            }
            std::sort(rows.rbegin(), rows.rend());
            for (int r : rows) {
                if (m_mode == PlayMode) {
                    m_engine->removeAt(r);
                } else {
                    (m_mode == BurnMode ? m_burn : m_sync).removeAt(r);
                }
            }
            refresh();
        });
        // reordering by dragging inside the list
        connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, [this] {
            if (m_mode != PlayMode) {
                QList<QUrl> urls;
                for (int i = 0; i < m_list->count(); ++i) {
                    urls << m_list->item(i)->data(Qt::UserRole + 2).toUrl();
                }
                (m_mode == BurnMode ? m_burn : m_sync) = urls;
            }
        });
        setMode(PlayMode);
        auto *notifier = Solid::DeviceNotifier::instance();
        connect(notifier, &Solid::DeviceNotifier::deviceAdded, this, [this] {
            QTimer::singleShot(1500, this, &ListPane::refreshDevices);
        });
        connect(notifier, &Solid::DeviceNotifier::deviceRemoved, this, [this] {
            QTimer::singleShot(500, this, &ListPane::refreshDevices);
        });
        refreshDevices();
    }

    void refreshDevices()
    {
        const auto b = Devices::burner();
        m_burnerName = b.first;
        m_burnerDev = b.second;
        const auto targets = Devices::storageTargets();
        m_target = targets.isEmpty() ? SyncTarget() : targets.first();
        if (targets.isEmpty() && !Devices::mtpPlayers().isEmpty()) {
            // a phone over MTP: mtp:/<device>/<storage>/Music
            const QString name = Devices::mtpPlayers().first();
            auto *job = KIO::listDir(QUrl(QStringLiteral("mtp:/")), KIO::HideProgressInfo);
            connect(job, &KIO::ListJob::entries, this, [this, name](KIO::Job *, const KIO::UDSEntryList &list) {
                for (const auto &e : list) {
                    const QString dev = e.stringValue(KIO::UDSEntry::UDS_NAME);
                    if (dev == QLatin1String(".") || !e.isDir()) {
                        continue;
                    }
                    const QUrl devUrl(QStringLiteral("mtp:/") + dev);
                    auto *sj = KIO::listDir(devUrl, KIO::HideProgressInfo);
                    connect(sj, &KIO::ListJob::entries, this, [this, devUrl, dev](KIO::Job *, const KIO::UDSEntryList &st) {
                        for (const auto &x : st) {
                            const QString n = x.stringValue(KIO::UDSEntry::UDS_NAME);
                            if (n == QLatin1String(".") || !x.isDir() || !m_target.name.isEmpty()) {
                                continue;
                            }
                            m_target.name = dev;
                            m_target.mtp = true;
                            m_target.folder = QUrl(devUrl.toString() + QLatin1Char('/') + n + QStringLiteral("/Music"));
                            m_head->update();
                            update();
                        }
                    });
                    break;
                }
            });
        }
        m_head->update();
        refresh();
    }

    Mode mode() const
    {
        return m_mode;
    }
    void setMode(Mode m)
    {
        m_mode = m;
        m_action->setText(m == PlayMode ? tr7("Save list") : m == BurnMode ? tr7("Start burn") : tr7("Start sync"));
        m_action->setIcon(m == PlayMode ? Res::icon(29605) : m == BurnMode ? Res::icon(29610) : Res::icon(29607));
        m_burnOpts->setVisible(m == BurnMode);
        m_list->setDragDropMode(m == PlayMode ? QAbstractItemView::DragDrop : QAbstractItemView::DragDrop);
        refresh();
    }
    void addUrls(const QList<QUrl> &urls)
    {
        if (m_mode == PlayMode) {
            m_engine->enqueue(urls);
        } else {
            (m_mode == BurnMode ? m_burn : m_sync) += urls;
            refresh();
        }
    }

protected:
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (e->mimeData()->hasUrls()) {
            e->acceptProposedAction();
        }
    }
    void dropEvent(QDropEvent *e) override
    {
        addUrls(e->mimeData()->urls());
        e->acceptProposedAction();
    }
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == m_head && e->type() == QEvent::Paint) {
            QPainter p(m_head);
            const int w = m_head->width();
            if (m_mode == PlayMode) {
                // the big "playlist" page icon, the list name as a link, and ◀ name ▶ under a line
                p.drawPixmap(16, 12, Res::icon(613).pixmap(48, 48));
                p.setPen(QColor(0x33, 0x66, 0xcc));
                p.drawText(QRect(82, 10, w - 86, 18), Qt::AlignLeft | Qt::AlignVCenter, tr7("Unsaved list"));
            } else {
                // the drive / device picture with its state
                const QPixmap pic = m_mode == BurnMode ? Res::px(5265).scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                                                       : Res::px(5515).scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                p.drawPixmap(14, 10, pic);
                p.setPen(Wmp::TEXT);
                QFont b = p.font();
                b.setBold(true);
                p.setFont(b);
                QString title, sub;
                if (m_mode == BurnMode) {
                    if (m_burnerName.isEmpty()) {
                        title = tr7("No burner found");
                        sub = tr7("Connect a burner drive.");
                    } else if (m_audioBurn) {
                        title = tr7("Audio CD");
                        const qint64 left = 80 * 60 * 1000 - burnLength();
                        auto mmss = [](qint64 ms) {
                            const qint64 sec = (ms + 500) / 1000;
                            return QStringLiteral("%1:%2").arg(sec / 60).arg(sec % 60, 2, 10, QLatin1Char('0'));
                        };
                        sub = left >= 0 ? tr7("%1 remaining of 80:00\n%2").arg(mmss(left), m_burnerName)
                                        : tr7("%1 over\n%2").arg(mmss(-left), m_burnerName);
                    } else {
                        title = tr7("Data disc");
                        sub = tr7("Insert a writable disc\n") + m_burnerName;
                    }
                } else {
                    title = m_target.name.isEmpty() ? tr7("No device connected") : m_target.name;
                    sub = m_target.name.isEmpty() ? tr7("Connect a device.")
                        : m_target.free >= 0 ? tr7("%1 free of %2").arg(formatSize(m_target.free), formatSize(m_target.total))
                                              : tr7("Ready to sync");
                }
                if (!m_status.isEmpty()) {
                    sub = m_status;
                }
                p.drawText(QRect(80, 12, w - 84, 18), Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(title, Qt::ElideRight, w - 84));
                p.setFont(font());
                p.setPen(QColor(0x6d, 0x6d, 0x6d));
                p.drawText(QRect(80, 30, w - 84, 34), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, sub);
            }
            p.setPen(QColor(0xd7, 0xe2, 0xee));
            p.drawLine(4, 76, w - 4, 76);
            p.setPen(Wmp::TEXT);
            QFont f = p.font();
            f.setItalic(true);
            p.setFont(f);
            if (m_mode == PlayMode) {
                p.drawPixmap(6, 82, Res::icon(29655).pixmap(16, 16));
                p.drawPixmap(w - 22, 82, Res::icon(29654).pixmap(16, 16));
            }
            p.drawText(QRect(24, 80, w - 48, 20), Qt::AlignCenter,
                       m_mode == PlayMode ? tr7("Unsaved list") : m_mode == BurnMode ? tr7("Burn list") : tr7("Sync list"));
            return true;
        }
        return QWidget::eventFilter(o, e);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        Wmp::paintBackground(p, rect(), this);
        p.setPen(Wmp::LINE);
        p.drawLine(0, 0, 0, height());
        if (m_list->count() == 0) {
            QFont f = font();
            f.setBold(true);
            p.setFont(f);
            p.setPen(Wmp::TEXT);
            p.drawText(QRect(0, 200, width(), 20), Qt::AlignCenter, tr7("Drag items here"));
            p.setFont(font());
            p.drawText(QRect(10, 218, width() - 20, 40), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                       m_mode == PlayMode ? tr7("to create a playlist.") : m_mode == BurnMode ? tr7("to create a burn list.") : tr7("to create a sync list."));
        }
    }

private:
    void startBurn()
    {
        if (m_burnerDev.isEmpty()) {
            QMessageBox::warning(this, APP_TITLE, tr7("Windows Media Player could not find a CD or DVD burner."));
            return;
        }
        m_audioBurn ? burnAudio() : burnData();
    }
    /** total length of the burn list (audio CD: 80 minutes) */
    qint64 burnLength() const
    {
        qint64 total = 0;
        for (const QUrl &u : m_burn) {
            if (const MediaItem *m = u.isLocalFile() ? m_lib->find(u.toLocalFile()) : nullptr) {
                total += m->duration;
            }
        }
        return total;
    }
    void setBurnStatus(const QString &s)
    {
        m_status = s;
        m_head->update();
    }
    void burnData()
    {
        if (QMessageBox::question(this, APP_TITLE, tr7("Burn %1 items to a data disc in drive %2?").arg(m_burn.size()).arg(m_burnerName),
                                  QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        QStringList args{QStringLiteral("-outdev"), m_burnerDev, QStringLiteral("-blank"), QStringLiteral("as_needed"), QStringLiteral("-joliet"), QStringLiteral("on")};
        for (const QUrl &u : std::as_const(m_burn)) {
            if (u.isLocalFile()) {
                args << QStringLiteral("-map") << u.toLocalFile() << QLatin1Char('/') + QFileInfo(u.toLocalFile()).fileName();
            }
        }
        args << QStringLiteral("-commit");
        if (m_ejectAfter) {
            args << QStringLiteral("-eject") << QStringLiteral("all");
        }
        auto *proc = new QProcess(this);
        proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(proc, &QProcess::readyRead, this, [this, proc] {
            static const QRegularExpression re(QStringLiteral("(\\d+(\\.\\d+)?)% done"));
            const auto m = re.match(QString::fromUtf8(proc->readAll()));
            if (m.hasMatch()) {
                setBurnStatus(tr7("Burning: %1%").arg(qRound(m.captured(1).toDouble())));
            }
        });
        connect(proc, &QProcess::finished, this, [this, proc](int code) {
            setBurnStatus(code == 0 ? tr7("Burn complete") : tr7("Burn error. Check that a writable disc is inserted."));
            proc->deleteLater();
        });
        setBurnStatus(tr7("Preparing to burn..."));
        proc->start(QStringLiteral("xorriso"), args);
    }
    /** audio CD: every track converted to CD audio (WAV 44.1 kHz/16 bit/stereo) with ffmpeg, then cdrskin -dao */
    void burnAudio()
    {
        QList<QUrl> tracks;
        for (const QUrl &u : std::as_const(m_burn)) {
            if (u.isLocalFile()) {
                tracks << u;
            }
        }
        if (tracks.isEmpty()) {
            return;
        }
        if (burnLength() > 80 * 60 * 1000) {
            QMessageBox::warning(this, APP_TITLE, tr7("The burn list does not fit on one audio CD (80 minutes). Remove some items from the list."));
            return;
        }
        if (QMessageBox::question(this, APP_TITLE, tr7("Burn %1 songs to an audio CD in drive %2?").arg(tracks.size()).arg(m_burnerName),
                                  QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        auto *tmp = new QTemporaryDir;
        auto wavs = std::make_shared<QStringList>();
        auto step = std::make_shared<std::function<void(int)>>();
        *step = [this, tracks, tmp, wavs, step](int i) {
            if (i < tracks.size()) {
                setBurnStatus(tr7("Preparing track %1 of %2...").arg(i + 1).arg(tracks.size()));
                const QString wav = tmp->filePath(QStringLiteral("%1.wav").arg(i + 1, 2, 10, QLatin1Char('0')));
                auto *ff = new QProcess(this);
                connect(ff, &QProcess::finished, this, [this, ff, wav, wavs, step, i](int code) {
                    ff->deleteLater();
                    if (code != 0) {
                        setBurnStatus(tr7("Could not prepare track %1").arg(i + 1));
                        return;
                    }
                    *wavs << wav;
                    (*step)(i + 1);
                });
                ff->start(QStringLiteral("ffmpeg"), {QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-i"), tracks[i].toLocalFile(),
                                                     QStringLiteral("-vn"), QStringLiteral("-ar"), QStringLiteral("44100"), QStringLiteral("-ac"), QStringLiteral("2"),
                                                     QStringLiteral("-sample_fmt"), QStringLiteral("s16"), wav});
                return;
            }
            // all converted: burn
            QStringList args{QStringLiteral("dev=") + m_burnerDev, QStringLiteral("-v"), QStringLiteral("-dao"), QStringLiteral("gracetime=0"), QStringLiteral("-audio"), QStringLiteral("-pad")};
            if (m_ejectAfter) {
                args << QStringLiteral("-eject");
            }
            args << *wavs;
            auto *proc = new QProcess(this);
            proc->setProcessChannelMode(QProcess::MergedChannels);
            connect(proc, &QProcess::readyRead, this, [this, proc] {
                // "Track 01: 12 of 40 MB written"
                static const QRegularExpression re(QStringLiteral("Track (\\d+):\\s+(\\d+) of\\s+(\\d+) MB written"));
                const QString out = QString::fromUtf8(proc->readAll());
                auto it = re.globalMatch(out);
                while (it.hasNext()) {
                    const auto m = it.next();
                    const int pct = m.captured(3).toInt() ? m.captured(2).toInt() * 100 / m.captured(3).toInt() : 0;
                    setBurnStatus(tr7("Burning track %1: %2%").arg(m.captured(1).toInt()).arg(pct));
                }
            });
            connect(proc, &QProcess::finished, this, [this, proc, tmp](int code) {
                setBurnStatus(code == 0 ? tr7("Burn complete") : tr7("Burn error. Check that a blank CD-R is inserted."));
                proc->deleteLater();
                delete tmp;
            });
            setBurnStatus(tr7("Starting burn..."));
            proc->start(QStringLiteral("cdrskin"), args);
        };
        (*step)(0);
    }
    void startSync()
    {
        if (m_target.name.isEmpty()) {
            QMessageBox::information(this, APP_TITLE, tr7("Connect a device to start syncing."));
            return;
        }
        const QList<QUrl> urls = m_sync;
        const QUrl dest = m_target.folder;
        auto *mk = KIO::mkpath(dest);
        connect(mk, &KJob::result, this, [this, urls, dest] {
            auto *job = KIO::copy(urls, dest);
            KJobWidgets::setWindow(job, window());
            job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, window()));
            m_status = tr7("Syncing...");
            m_head->update();
            connect(job, &KJob::percentChanged, this, [this](KJob *, unsigned long pct) {
                m_status = tr7("Syncing: %1%").arg(pct);
                m_head->update();
            });
            connect(job, &KJob::result, this, [this](KJob *j) {
                m_status = j->error() ? tr7("Sync did not complete") : tr7("Sync complete");
                m_head->update();
            });
        });
    }

    void refresh()
    {
        m_list->clear();
        const QList<QUrl> urls = m_mode == PlayMode ? m_engine->list() : m_mode == BurnMode ? m_burn : m_sync;
        qint64 total = 0;
        for (int i = 0; i < urls.size(); ++i) {
            const MediaItem *m = urls[i].isLocalFile() ? m_lib->find(urls[i].toLocalFile()) : nullptr;
            const QString name = m ? m->displayTitle() : (urls[i].isLocalFile() ? QFileInfo(urls[i].toLocalFile()).completeBaseName() : urls[i].toString());
            auto *it = new QListWidgetItem(name, m_list);
            it->setData(Qt::UserRole + 2, urls[i]);
            if (m && m->duration) {
                total += m->duration;
                it->setData(Qt::UserRole + 1, formatLength(m->duration));
            }
            it->setToolTip(name);
        }
        m_list->setVisible(!urls.isEmpty());
        const int n = urls.size();
        QString items = n == 1 ? tr7("1 item") : (n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 10 || n % 100 >= 20)) ? tr7("%1 items").arg(n) : tr7("%1 items", "many").arg(n);
        m_count->setText(total > 0 ? items + QStringLiteral(", ") + formatLength(total) : items);
        m_clear->setEnabled(n > 0);
        m_action->setEnabled(n > 0);
        m_head->update();
        update();
    }

public:
    void save()
    {
        if (m_engine->list().isEmpty()) {
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(this, APP_TITLE, tr7("Playlist name:"), QLineEdit::Normal, tr7("Playlist"), &ok);
        if (!ok || name.trimmed().isEmpty()) {
            return;
        }
        QDir().mkpath(playlistsDir());
        QFile f(playlistsDir() + QLatin1Char('/') + name.trimmed() + QStringLiteral(".m3u"));
        if (f.open(QIODevice::WriteOnly)) {
            QTextStream ts(&f);
            ts << "#EXTM3U\n";
            for (const QUrl &u : m_engine->list()) {
                ts << (u.isLocalFile() ? u.toLocalFile() : u.toString()) << '\n';
            }
        }
        Q_EMIT saved();
    }

Q_SIGNALS:
    void saved();

private:
    Engine *m_engine;
    Library *m_lib;
    Mode m_mode = PlayMode;
    QList<QUrl> m_burn, m_sync;
    QString m_burnerName, m_burnerDev, m_status;
    QToolButton *m_burnOpts = nullptr;
    bool m_audioBurn = true;
    bool m_ejectAfter = true;
    SyncTarget m_target;
    QToolButton *m_action, *m_clear;
    QWidget *m_head;
    DropList *m_list;
    QLabel *m_count;
};

// ----------------------------------------------------------------------------------------------------------------

/** the library page: nav pane, content views, list pane, transport */
class LibraryPage : public QWidget
{
    Q_OBJECT
public:
    enum Node { NodeLibrary, NodePlaylists, NodeMusic, NodeArtist, NodeAlbum, NodeGenre, NodeVideos, NodePictures, NodeOther, NodeYear, NodeRating, NodeComposer, NodeContrib };

    LibraryPage(Engine *engine, Library *lib, QWidget *parent)
        : QWidget(parent)
        , m_engine(engine)
        , m_lib(lib)
    {
        auto *l = new QVBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        m_band = new AddressBand(this);
        l->addWidget(m_band);
        m_cmd = new CommandBar(this);
        l->addWidget(m_cmd);

        auto *body = new QSplitter(Qt::Horizontal, this);
        body->setHandleWidth(1);
        body->setChildrenCollapsible(false);
        body->setStyleSheet(QStringLiteral("QSplitter::handle { background: #c9d7e8; }"));
        m_nav = new Transparent<Win7Branches<QTreeWidget>>(body);
        m_nav->setHeaderHidden(true);
        m_nav->setIndentation(19);
        m_nav->setRootIsDecorated(true);
        m_nav->setIconSize(QSize(16, 16));
        m_nav->setMinimumWidth(120);
        m_nav->setStyleSheet(QStringLiteral("QTreeWidget { background: transparent; padding-top: 14px; padding-left: 10px; } QTreeWidget::item { height: 20px; color: #1e395b; } QTreeWidget::branch { background: transparent; border-image: none; image: none; }"));
        buildNav();
        body->addWidget(m_nav);

        m_stack = new QStackedWidget(body);
        // music details
        m_music = new Transparent<QTreeView>(m_stack);
        m_musicModel = new MusicModel(this);
        m_music->setModel(m_musicModel);
        m_music->setHeader(new WmpHeader(m_music));
        m_musicDelegate = new MusicDelegate(m_music);
        m_music->setItemDelegate(m_musicDelegate);
        m_music->viewport()->installEventFilter(this);
        m_music->header()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_music->header(), &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
            columnMenu(m_music->header()->mapToGlobal(pos));
        });
        m_music->setRootIsDecorated(false);
        m_music->setIndentation(0);
        m_music->setUniformRowHeights(false);
        m_music->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_music->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_music->setMouseTracking(true);
        m_music->setDragEnabled(true);
        m_music->setDragDropMode(QAbstractItemView::DragOnly);
        m_music->setAllColumnsShowFocus(true);
        m_music->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_music->setContextMenuPolicy(Qt::CustomContextMenu);
        m_music->setStyleSheet(QStringLiteral("QTreeView { background: transparent; } QTreeView::item:selected { background: transparent; }"));
        m_stack->addWidget(m_music);
        // videos / pictures / albums
        m_icons = new IconList(m_stack);
        m_icons->urlsOf = [this](QListWidgetItem *it) {
            return itemUrls(it);
        };
        m_icons->setViewMode(QListView::IconMode);
        m_icons->setResizeMode(QListView::Adjust);
        m_icons->setMovement(QListView::Static);
        m_icons->setDragEnabled(true);
        m_icons->setDragDropMode(QAbstractItemView::DragOnly);
        m_icons->setSpacing(4);
        m_icons->setUniformItemSizes(true);
        m_icons->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_icons->setMouseTracking(true);
        m_icons->setContextMenuPolicy(Qt::CustomContextMenu);
        m_icons->setStyleSheet(QStringLiteral("QListWidget { background: transparent; }"));
        m_stack->addWidget(m_icons);
        body->addWidget(m_stack);

        m_listPane = new ListPane(engine, lib, body);
        connect(m_listPane, &ListPane::saved, this, [this] {
            refreshPlaylists();
            m_playlists->setExpanded(true);
        });
        body->addWidget(m_listPane);
        body->setStretchFactor(1, 1);
        body->setSizes({160, 760, 262});
        l->addWidget(body, 1);

        // line above the controls and the controls
        m_transport = new Transport(engine, Transport::Library, this);
        l->addWidget(m_transport);

        connect(m_nav, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) {
            if (it && !m_navigating) {
                pushHistory();
                show(it->data(0, Qt::UserRole).toInt(), it->data(0, Qt::UserRole + 1).toString());
            }
        });
        connect(m_music, &QTreeView::doubleClicked, this, [this](const QModelIndex &idx) {
            playFromMusic(idx);
        });
        connect(m_icons, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
            playFromIcons(it);
        });
        connect(m_music, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
            itemMenu(m_music->viewport()->mapToGlobal(pos), selectedMusicUrls());
        });
        connect(m_icons, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
            QList<QUrl> urls;
            for (auto *it : m_icons->selectedItems()) {
                urls << itemUrls(it);
            }
            itemMenu(m_icons->viewport()->mapToGlobal(pos), urls);
        });
        connect(m_band, &AddressBand::tabClicked, this, [this](int tab) {
            // a tab opens the list pane in its mode; clicking the open tab again closes the pane
            if (m_listPane->isVisible() && m_band->tab() == tab) {
                m_listPane->setVisible(false);
                m_band->setTab(-1);
            } else {
                m_listPane->setMode(ListPane::Mode(tab));
                m_listPane->setVisible(true);
                m_band->setTab(tab);
            }
        });
        connect(m_band, &AddressBand::backClicked, this, &LibraryPage::back);
        connect(m_band, &AddressBand::forwardClicked, this, &LibraryPage::forward);
        connect(m_band, &AddressBand::crumbClicked, this, [this](int i) {
            if (i == 0) {
                selectNode(NodeLibrary);
            } else if (i == 1 && isGroupNode()) {
                selectNode(NodeMusic);
            }
        });
        connect(m_cmd->search(), &QLineEdit::textChanged, this, [this] {
            refreshView();
        });
        connect(m_cmd, &CommandBar::menuRequested, this, &LibraryPage::commandMenu);
        connect(m_cmd, &CommandBar::viewMenuRequested, this, [this](const QPoint &g) {
            QMenu m(this);
            auto *grp = new QActionGroup(&m);
            for (auto [name, mode] : {std::pair{tr7("Icon"), 0}, std::pair{tr7("Tile"), 1}, std::pair{tr7("Details"), 2}}) {
                auto *a = m.addAction(name);
                a->setCheckable(true);
                a->setChecked(mode == 2);
                grp->addAction(a);
            }
            m.exec(g);
        });
        connect(lib, &Library::changed, this, &LibraryPage::refreshView);
        connect(lib, &Library::scanning, this, [this](const QString &folder) {
            m_transport->setStatus(tr7("Updating media library: ") + folder);
        });
        connect(lib, &Library::busyChanged, this, [this] {
            if (!m_lib->busy()) {
                m_transport->setStatus(QString());
            }
        });
        m_listPane->setVisible(false);
        connect(engine, &Engine::currentChanged, this, [this] {
            const QUrl u = m_engine->current();
            Wmp::playingPath = u.isLocalFile() ? u.toLocalFile() : QString();
            m_music->viewport()->update();
            m_icons->viewport()->update();
        });
        connect(engine, &Engine::listChanged, this, [this] {
            // playing something opens the Play tab with the list
            if (!m_engine->list().isEmpty() && (!m_listPane->isVisible() || m_listPane->mode() != ListPane::PlayMode)) {
                m_listPane->setMode(ListPane::PlayMode);
                m_listPane->setVisible(true);
                m_band->setTab(0);
            }
        });
        selectNode(NodeMusic);
    }

    Transport *transport() const
    {
        return m_transport;
    }

    void selectNode(int node, const QString &arg = {})
    {
        for (QTreeWidgetItemIterator it(m_nav); *it; ++it) {
            if ((*it)->data(0, Qt::UserRole).toInt() == node) {
                m_nav->setCurrentItem(*it);
                break;
            }
        }
        show(node, arg);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        Wmp::paintBackground(p, rect(), this);
    }
    bool eventFilter(QObject *o, QEvent *e) override
    {
        // click-to-rate in the Rating column (5 stars of 13 px)
        if (o == m_music->viewport() && (e->type() == QEvent::MouseMove || e->type() == QEvent::MouseButtonPress || e->type() == QEvent::Leave)) {
            int row = -1, stars = 0;
            if (e->type() != QEvent::Leave) {
                const QPoint pos = static_cast<QMouseEvent *>(e)->position().toPoint();
                const QModelIndex idx = m_music->indexAt(pos);
                if (idx.isValid() && idx.column() == 4 && idx.data(MusicDelegate::RatingRole).isValid()) {
                    const QRect r = m_music->visualRect(idx);
                    const int x = pos.x() - r.x() - 4;
                    if (x >= 0 && x < 5 * 13) {
                        row = idx.row();
                        stars = x / 13 + 1;
                    }
                }
            }
            if (row != m_musicDelegate->hoverRow || stars != m_musicDelegate->hoverStars) {
                m_musicDelegate->hoverRow = row;
                m_musicDelegate->hoverStars = stars;
                m_music->viewport()->update();
            }
            if (e->type() == QEvent::MouseButtonPress && row >= 0 && static_cast<QMouseEvent *>(e)->button() == Qt::LeftButton) {
                const QString path = m_musicModel->item(row, 0)->data(MusicDelegate::PathRole).toString();
                const int old = m_musicModel->item(row, 4)->data(MusicDelegate::RatingRole).toInt();
                const int rating = old == stars ? 0 : stars; // clicking the same rating again clears it
                m_musicModel->item(row, 4)->setData(rating, MusicDelegate::RatingRole);
                m_lib->setRatingQuiet(path, rating);
                return true;
            }
        }
        return QWidget::eventFilter(o, e);
    }

private:
    void columnMenu(const QPoint &global)
    {
        // right click on the column header: the columns to show (Album and Title always stay)
        QMenu m(this);
        auto *hdr = m_music->header();
        for (int c = 0; c < m_musicModel->columnCount(); ++c) {
            auto *a = m.addAction(m_musicModel->headerData(c, Qt::Horizontal).toString());
            a->setCheckable(true);
            a->setChecked(!hdr->isSectionHidden(c));
            a->setEnabled(c != 0 && c != 2);
            a->setData(c);
        }
        m.addSeparator();
        auto *choose = m.addAction(tr7("Choose columns..."));
        QAction *a = m.exec(global);
        if (a == choose) {
            chooseColumns();
            return;
        }
        if (a && a->isCheckable()) {
            hdr->setSectionHidden(a->data().toInt(), !a->isChecked());
            QStringList hidden;
            for (int c = 0; c < m_musicModel->columnCount(); ++c) {
                if (hdr->isSectionHidden(c)) {
                    hidden << QString::number(c);
                }
            }
            QSettings().setValue(QStringLiteral("hiddenColumns"), hidden);
        }
    }

    void buildNav()
    {
        auto add = [this](QTreeWidgetItem *parent, const QString &name, int icon, int node) {
            QTreeWidgetItem *it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_nav);
            it->setText(0, name);
            it->setIcon(0, Res::icon(icon));
            it->setData(0, Qt::UserRole, node);
            return it;
        };
        add(nullptr, tr7("Library"), 29513, NodeLibrary);
        m_playlists = add(nullptr, tr7("Playlists"), 29522, NodePlaylists);
        auto *music = add(nullptr, tr7("Music"), 29520, NodeMusic);
        m_music_ = music;
        for (const auto &n : navMusicNodes()) {
            add(music, n.name, n.icon, n.node)->setHidden(!navShown(n.key, n.def));
        }
        music->setExpanded(true);
        add(nullptr, tr7("Videos"), 29538, NodeVideos);
        add(nullptr, tr7("Pictures"), 29531, NodePictures);
        auto *spacer = new QTreeWidgetItem(m_nav);
        spacer->setFlags(Qt::NoItemFlags);
        spacer->setSizeHint(0, QSize(10, 12));
        add(nullptr, tr7("Other Libraries"), 29546, NodeOther);
        refreshPlaylists();
    }
public:
    struct NavNode {
        QString key;
        QString name;
        int icon;
        int node;
        bool def;
    };
    static QList<NavNode> navMusicNodes()
    {
        return {{QStringLiteral("artist"), tr7("Artist"), 29512, NodeArtist, true},
                {QStringLiteral("album"), tr7("Album"), 29510, NodeAlbum, true},
                {QStringLiteral("genre"), tr7("Genre"), 29515, NodeGenre, true},
                {QStringLiteral("year"), tr7("Year"), 29530, NodeYear, false},
                {QStringLiteral("rating"), tr7("Rating"), 29526, NodeRating, false},
                {QStringLiteral("contrib"), tr7("Contributing Artist"), 29537, NodeContrib, false},
                {QStringLiteral("composer"), tr7("Composer"), 29546, NodeComposer, false}};
    }
    static bool navShown(const QString &key, bool def)
    {
        return QSettings().value(QStringLiteral("nav/") + key, def).toBool();
    }
    /** Organize > Customize navigation pane */
    void customizeNav()
    {
        QDialog d(this);
        d.setWindowTitle(tr7("Customize Navigation Pane"));
        auto *l = new QVBoxLayout(&d);
        auto *head = new QLabel(tr7("Select the categories that appear in the navigation pane for each library."), &d);
        head->setWordWrap(true);
        l->addWidget(head);
        auto *lib = new QComboBox(&d);
        lib->addItem(tr7("Library"));
        l->addWidget(lib);
        auto *g = new QGroupBox(tr7("Music"), &d);
        auto *gl = new QVBoxLayout(g);
        QList<QPair<QString, QCheckBox *>> boxes;
        for (const auto &n : navMusicNodes()) {
            auto *c = new QCheckBox(n.name, g);
            c->setChecked(navShown(n.key, n.def));
            gl->addWidget(c);
            boxes << qMakePair(n.key, c);
        }
        l->addWidget(g);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
        bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("OK"));
        bb->button(QDialogButtonBox::Cancel)->setText(tr7("Cancel"));
        auto *restore = bb->addButton(tr7("Restore Defaults"), QDialogButtonBox::ResetRole);
        connect(restore, &QPushButton::clicked, &d, [&] {
            for (int i = 0; i < boxes.size(); ++i) {
                boxes[i].second->setChecked(navMusicNodes()[i].def);
            }
        });
        l->addWidget(bb);
        connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        if (d.exec() != QDialog::Accepted) {
            return;
        }
        QSettings s;
        for (const auto &b : boxes) {
            s.setValue(QStringLiteral("nav/") + b.first, b.second->isChecked());
        }
        for (int i = 0; i < m_music_->childCount(); ++i) {
            for (const auto &b : boxes) {
                if (navMusicNodes()[boxes.indexOf(b)].node == m_music_->child(i)->data(0, Qt::UserRole).toInt()) {
                    m_music_->child(i)->setHidden(!b.second->isChecked());
                }
            }
        }
    }

    /** Choose columns: which columns of the music view are shown and in which order */
    void chooseColumns()
    {
        auto *hdr = m_music->header();
        QDialog d(this);
        d.setWindowTitle(tr7("Choose Columns"));
        auto *l = new QVBoxLayout(&d);
        l->addWidget(new QLabel(tr7("Select the columns you want to display in the library."), &d));
        auto *row = new QHBoxLayout;
        auto *list = new QListWidget(&d);
        for (int v = 0; v < hdr->count(); ++v) {
            const int c = hdr->logicalIndex(v);
            auto *it = new QListWidgetItem(m_musicModel->headerData(c, Qt::Horizontal).toString(), list);
            it->setData(Qt::UserRole, c);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(hdr->isSectionHidden(c) ? Qt::Unchecked : Qt::Checked);
            if (c == 0 || c == 2) {
                it->setFlags(it->flags() & ~Qt::ItemIsUserCheckable);
            }
        }
        row->addWidget(list, 1);
        auto *btns = new QVBoxLayout;
        auto *up = new QPushButton(tr7("Move Up"), &d);
        auto *down = new QPushButton(tr7("Move Down"), &d);
        btns->addWidget(up);
        btns->addWidget(down);
        btns->addStretch(1);
        row->addLayout(btns);
        l->addLayout(row);
        connect(up, &QPushButton::clicked, &d, [list] {
            const int r = list->currentRow();
            if (r > 0) {
                list->insertItem(r - 1, list->takeItem(r));
                list->setCurrentRow(r - 1);
            }
        });
        connect(down, &QPushButton::clicked, &d, [list] {
            const int r = list->currentRow();
            if (r >= 0 && r < list->count() - 1) {
                list->insertItem(r + 1, list->takeItem(r));
                list->setCurrentRow(r + 1);
            }
        });
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
        bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("OK"));
        bb->button(QDialogButtonBox::Cancel)->setText(tr7("Cancel"));
        l->addWidget(bb);
        connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        d.resize(340, 380);
        if (d.exec() != QDialog::Accepted) {
            return;
        }
        QStringList hidden, order;
        for (int i = 0; i < list->count(); ++i) {
            const int c = list->item(i)->data(Qt::UserRole).toInt();
            order << QString::number(c);
            if (list->item(i)->checkState() == Qt::Unchecked) {
                hidden << QString::number(c);
            }
        }
        QSettings().setValue(QStringLiteral("hiddenColumns"), hidden);
        QSettings().setValue(QStringLiteral("columnOrder"), order);
        applyColumns();
    }
    void applyColumns()
    {
        auto *hdr = m_music->header();
        const QStringList hidden = QSettings().value(QStringLiteral("hiddenColumns")).toStringList();
        const QStringList order = QSettings().value(QStringLiteral("columnOrder")).toStringList();
        for (int c = 0; c < m_musicModel->columnCount(); ++c) {
            hdr->setSectionHidden(c, hidden.contains(QString::number(c)));
        }
        for (int v = 0; v < order.size(); ++v) {
            const int c = order[v].toInt();
            if (c < hdr->count()) {
                hdr->moveSection(hdr->visualIndex(c), v);
            }
        }
    }

private:
    void refreshPlaylists()
    {
        qDeleteAll(m_playlists->takeChildren());
        const auto files = QDir(playlistsDir()).entryInfoList({QStringLiteral("*.m3u"), QStringLiteral("*.m3u8"), QStringLiteral("*.wpl"), QStringLiteral("*.pls"), QStringLiteral("*.autopl")}, QDir::Files, QDir::Name);
        for (const auto &fi : files) {
            auto *it = new QTreeWidgetItem(m_playlists);
            it->setText(0, fi.completeBaseName());
            it->setIcon(0, Res::icon(fi.suffix() == QLatin1String("autopl") ? 29572 : 29522));
            it->setData(0, Qt::UserRole, NodePlaylists);
            it->setData(0, Qt::UserRole + 1, fi.absoluteFilePath());
        }
    }

    void pushHistory()
    {
        if (m_node >= 0) {
            m_back << qMakePair(m_node, m_arg);
            m_forward.clear();
        }
        m_band->setNav(!m_back.isEmpty(), !m_forward.isEmpty());
    }
    void back()
    {
        if (m_back.isEmpty()) {
            return;
        }
        m_forward << qMakePair(m_node, m_arg);
        const auto t = m_back.takeLast();
        navigateTo(t.first, t.second);
    }
    void forward()
    {
        if (m_forward.isEmpty()) {
            return;
        }
        m_back << qMakePair(m_node, m_arg);
        const auto t = m_forward.takeLast();
        navigateTo(t.first, t.second);
    }
    void navigateTo(int node, const QString &arg)
    {
        m_navigating = true;
        for (QTreeWidgetItemIterator it(m_nav); *it; ++it) {
            if ((*it)->data(0, Qt::UserRole).toInt() == node && (*it)->data(0, Qt::UserRole + 1).toString() == (node == NodePlaylists ? arg : QString())) {
                m_nav->setCurrentItem(*it);
                break;
            }
        }
        m_navigating = false;
        show(node, arg);
        m_band->setNav(!m_back.isEmpty(), !m_forward.isEmpty());
    }

    void show(int node, const QString &arg)
    {
        m_node = node;
        m_arg = arg;
        QStringList crumbs{tr7("Library")};
        switch (node) {
        case NodeLibrary:
            crumbs << tr7("Music") << tr7("All music");
            break;
        case NodePlaylists:
            crumbs << tr7("Playlists") << (arg.isEmpty() ? tr7("All playlists") : QFileInfo(arg).completeBaseName());
            break;
        case NodeMusic:
            crumbs << tr7("Music") << tr7("All music");
            break;
        case NodeArtist:
            crumbs << tr7("Music") << (arg.isEmpty() ? tr7("Artist") : arg);
            break;
        case NodeAlbum:
            crumbs << tr7("Music") << (arg.isEmpty() ? tr7("Album") : arg);
            break;
        case NodeGenre:
            crumbs << tr7("Music") << (arg.isEmpty() ? tr7("Genre") : arg);
            break;
        case NodeYear:
            crumbs << tr7("Music") << (arg.isEmpty() ? tr7("Release Year") : arg);
            break;
        case NodeRating:
            crumbs << tr7("Music") << (arg.isEmpty() ? tr7("Rating") : arg);
            break;
        case NodeComposer:
            crumbs << tr7("Music") << (arg.isEmpty() ? tr7("Composer") : arg);
            break;
        case NodeContrib:
            crumbs << tr7("Music") << (arg.isEmpty() ? tr7("Contributing Artist") : arg);
            break;
        case NodeVideos:
            crumbs << tr7("Videos") << tr7("All video");
            break;
        case NodePictures:
            crumbs << tr7("Pictures") << tr7("All pictures");
            break;
        default:
            crumbs = QStringList{tr7("Other Libraries")};
        }
        m_band->setCrumbs(crumbs);
        m_band->setNav(!m_back.isEmpty(), !m_forward.isEmpty());
        refreshView();
    }

    bool isGroupNode() const
    {
        return m_node == NodeArtist || m_node == NodeAlbum || m_node == NodeGenre || m_node == NodeYear || m_node == NodeRating || m_node == NodeComposer
            || m_node == NodeContrib;
    }
    /** the group of a track in the Artist / Album / Genre / Year / Rating / Composer views */
    QString groupKey(const MediaItem &m) const
    {
        switch (m_node) {
        case NodeArtist:
            return m.displayAlbumArtist();
        case NodeAlbum:
            return m.displayAlbum();
        case NodeGenre:
            return m.displayGenre();
        case NodeYear:
            return m.year ? QString::number(m.year) : tr7("Unknown Year");
        case NodeRating:
            return m.rating ? QStringLiteral("%1 ★").arg(m.rating) : tr7("Unrated");
        case NodeComposer:
            return m.composer.isEmpty() ? tr7("Unknown Composer") : m.composer;
        case NodeContrib:
            return m.displayArtist();
        default:
            return {};
        }
    }
    bool matches(const MediaItem &m) const
    {
        const QString q = m_cmd->search()->text().trimmed();
        if (q.isEmpty()) {
            return true;
        }
        for (const QString &f : {m.displayTitle(), m.artist, m.album, m.genre, m.composer, QFileInfo(m.path).fileName()}) {
            if (f.contains(q, Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    }

    void refreshView()
    {
        switch (m_node) {
        case NodeVideos:
            fillThumbs(MediaItem::Video);
            break;
        case NodePictures:
            fillThumbs(MediaItem::Picture);
            break;
        case NodeArtist:
        case NodeAlbum:
        case NodeGenre:
        case NodeYear:
        case NodeRating:
        case NodeComposer:
        case NodeContrib:
            if (m_arg.isEmpty()) {
                fillTiles();
            } else {
                fillMusic();
            }
            break;
        case NodePlaylists:
            if (m_arg.isEmpty()) {
                fillPlaylistTiles();
            } else {
                fillMusic();
            }
            break;
        case NodeOther:
            m_icons->clear();
            m_stack->setCurrentWidget(m_icons);
            break;
        default:
            fillMusic();
        }
    }

    QList<MediaItem> currentTracks() const
    {
        QList<MediaItem> tracks;
        if (m_node == NodePlaylists && !m_arg.isEmpty()) {
            for (const QUrl &u : readPlaylist(m_arg)) {
                if (const MediaItem *m = m_lib->find(u.toLocalFile())) {
                    tracks << *m;
                } else {
                    MediaItem x;
                    x.path = u.toLocalFile();
                    tracks << x;
                }
            }
            return tracks;
        }
        for (const auto &m : m_lib->items(MediaItem::Audio)) {
            if (!matches(m)) {
                continue;
            }
            if (isGroupNode() && !m_arg.isEmpty() && groupKey(m) != m_arg) {
                continue;
            }
            tracks << m;
        }
        return tracks;
    }

    void fillMusic()
    {
        m_stack->setCurrentWidget(m_music);
        m_musicModel->clear();
        const QStringList cols{tr7("Album"), QStringLiteral("№"), tr7("Title"), tr7("Length"), tr7("Rating"),
                               tr7("Contributing Artist"), tr7("Composer"), tr7("Size"), tr7("Parental Rating")};
        m_musicModel->setHorizontalHeaderLabels(cols);
        QList<MediaItem> tracks = currentTracks();
        const bool keepOrder = m_node == NodePlaylists;
        if (!keepOrder) {
            QCollator col;
            col.setNumericMode(true);
            col.setCaseSensitivity(Qt::CaseInsensitive);
            std::stable_sort(tracks.begin(), tracks.end(), [&](const MediaItem &a, const MediaItem &b) {
                if (int c = col.compare(a.displayAlbumArtist(), b.displayAlbumArtist())) {
                    return c < 0;
                }
                if (int c = col.compare(a.displayAlbum(), b.displayAlbum())) {
                    return c < 0;
                }
                if (a.track != b.track) {
                    return a.track < b.track;
                }
                return col.compare(a.displayTitle(), b.displayTitle()) < 0;
            });
        }
        auto row = [&](int kind) {
            QList<QStandardItem *> r;
            for (int c = 0; c < cols.size(); ++c) {
                auto *it = new QStandardItem;
                it->setData(kind, MusicDelegate::KindRole);
                it->setEditable(false);
                r << it;
            }
            return r;
        };
        int i = 0;
        while (i < tracks.size()) {
            // one group per album (WMP: album artist as the group title)
            const QString artist = tracks[i].displayAlbumArtist();
            const QString album = tracks[i].displayAlbum();
            int j = i;
            while (j < tracks.size() && (keepOrder ? j == i : (tracks[j].displayAlbumArtist() == artist && tracks[j].displayAlbum() == album))) {
                ++j;
            }
            if (!keepOrder || i == 0) {
                auto g = row(MusicDelegate::GroupRow);
                g[0]->setText(keepOrder ? QFileInfo(m_arg).completeBaseName() : artist);
                g[0]->setSelectable(false);
                for (auto *x : g) {
                    x->setFlags(Qt::ItemIsEnabled);
                }
                m_musicModel->appendRow(g);
            }
            const QStringList info{album, artist, tracks[i].displayGenre(), tracks[i].year ? QString::number(tracks[i].year) : QString()};
            const int rows = keepOrder ? j - i : qMax(j - i, 6);
            for (int k = 0; k < rows; ++k) {
                const bool real = i + k < j;
                auto r = row(real ? MusicDelegate::TrackRow : MusicDelegate::PadRow);
                if (!keepOrder) {
                    r[0]->setData(k, MusicDelegate::LineRole);
                    r[0]->setData(tracks[i].path, MusicDelegate::FirstPathRole);
                    if (k < 4) {
                        r[0]->setData(info[k], MusicDelegate::InfoRole);
                    }
                }
                if (real) {
                    const MediaItem &m = tracks[i + k];
                    r[0]->setData(m.path, MusicDelegate::PathRole);
                    r[1]->setText(m.track ? QString::number(m.track) : QString());
                    r[2]->setText(m.displayTitle());
                    r[3]->setText(m.duration ? formatLength(m.duration) : QString());
                    r[4]->setData(m.rating, MusicDelegate::RatingRole);
                    r[5]->setText(m.displayArtist());
                    r[6]->setText(m.composer);
                    r[7]->setText(formatSize(m.size));
                    r[7]->setData(int(Qt::AlignRight), Qt::TextAlignmentRole);
                } else {
                    for (auto *x : r) {
                        x->setFlags(Qt::ItemIsEnabled);
                    }
                }
                m_musicModel->appendRow(r);
            }
            if (!keepOrder) {
                // gap row between albums
                auto r = row(MusicDelegate::PadRow);
                for (auto *x : r) {
                    x->setFlags(Qt::ItemIsEnabled);
                }
                m_musicModel->appendRow(r);
            }
            i = j;
        }
        for (int r = 0; r < m_musicModel->rowCount(); ++r) {
            if (m_musicModel->item(r, 0)->data(MusicDelegate::KindRole).toInt() == MusicDelegate::GroupRow) {
                m_music->setFirstColumnSpanned(r, QModelIndex(), true);
            }
        }
        const QList<int> widths{235, 40, 150, 60, 75, 150, 150, 85, 150};
        for (int c = 0; c < widths.size(); ++c) {
            m_music->setColumnWidth(c, widths[c]);
        }
        applyColumns();
    }

    void fillThumbs(MediaItem::Kind kind)
    {
        m_stack->setCurrentWidget(m_icons);
        if (m_previewJob) {
            m_previewJob->kill();
        }
        m_icons->clear();
        m_icons->setItemDelegate(new ThumbDelegate(kind == MediaItem::Video, m_icons));
        m_icons->setGridSize(kind == MediaItem::Video ? QSize(254, 80) : QSize(134, 114));
        KFileItemList files;
        auto items = m_lib->items(kind);
        QCollator col;
        col.setNumericMode(true);
        const int sortBy = QSettings().value(QStringLiteral("sortBy"), 0).toInt();
        std::sort(items.begin(), items.end(), [&](const MediaItem &a, const MediaItem &b) {
            switch (sortBy) {
            case 3:
                return a.year > b.year;
            case 4:
                return a.mtime > b.mtime;
            case 5:
                return a.rating > b.rating;
            case 6:
                return a.duration > b.duration;
            default:
                return col.compare(a.displayTitle(), b.displayTitle()) < 0;
            }
        });
        for (const auto &m : items) {
            if (!matches(m)) {
                continue;
            }
            auto *it = new QListWidgetItem(m.displayTitle(), m_icons);
            it->setData(Qt::UserRole + 1, m.path);
            it->setData(Qt::UserRole + 2, m.duration ? formatDurationWords(m.duration) : QString());
            it->setData(Qt::UserRole + 3, m.rating);
            files << KFileItem(m.url());
        }
        if (files.isEmpty()) {
            return;
        }
        auto *job = KIO::filePreview(files, QSize(256, 256));
        m_previewJob = job;
        connect(job, &KIO::PreviewJob::gotPreview, m_icons, [this](const KFileItem &fi, const QPixmap &pm) {
            // look the item up now: the view may have been refilled meanwhile
            for (int i = 0; i < m_icons->count(); ++i) {
                if (m_icons->item(i)->data(Qt::UserRole + 1).toString() == fi.localPath()) {
                    m_icons->item(i)->setData(Qt::DecorationRole, pm);
                }
            }
        });
    }
    static QString formatDurationWords(qint64 ms)
    {
        const qint64 s = (ms + 500) / 1000;
        if (s < 60) {
            return tr7("%1 s").arg(s);
        }
        return formatLength(ms);
    }

    void fillTiles()
    {
        m_stack->setCurrentWidget(m_icons);
        m_icons->clear();
        m_icons->setItemDelegate(new TileDelegate(m_icons));
        m_icons->setGridSize(QSize(136, 174));
        struct Group {
            QString first;
            QString second;
            int count = 0;
            qint64 length = 0;
        };
        QMap<QString, Group> groups;
        for (const auto &m : m_lib->items(MediaItem::Audio)) {
            if (!matches(m)) {
                continue;
            }
            const QString key = groupKey(m);
            Group &g = groups[key];
            if (g.first.isEmpty()) {
                g.first = m.path;
                g.second = m_node == NodeAlbum ? m.displayAlbumArtist() : QString();
            }
            ++g.count;
            g.length += m.duration;
        }
        for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
            auto *item = new QListWidgetItem(it.key(), m_icons);
            item->setData(Qt::UserRole + 1, it->first);
            item->setData(Qt::UserRole + 2, m_node == NodeAlbum ? it->second : QStringLiteral("%1 %2").arg(it->count).arg(it->count == 1 ? tr7("song") : (it->count % 10 >= 2 && it->count % 10 <= 4 && (it->count % 100 < 10 || it->count % 100 >= 20)) ? tr7("songs") : tr7("songs", "many")));
            item->setData(Qt::UserRole + 3, m_node == NodeAlbum ? QString() : formatLength(it->length));
            item->setData(Qt::UserRole + 4, m_node != NodeAlbum);
            item->setData(Qt::UserRole + 5, it.key());
        }
    }
    void fillPlaylistTiles()
    {
        refreshPlaylists();
        m_stack->setCurrentWidget(m_icons);
        m_icons->clear();
        m_icons->setItemDelegate(new TileDelegate(m_icons));
        m_icons->setGridSize(QSize(136, 174));
        for (int i = 0; i < m_playlists->childCount(); ++i) {
            const QString file = m_playlists->child(i)->data(0, Qt::UserRole + 1).toString();
            const auto urls = readPlaylist(file);
            auto *item = new QListWidgetItem(QFileInfo(file).completeBaseName(), m_icons);
            item->setData(Qt::UserRole + 1, urls.isEmpty() ? QString() : urls.first().toLocalFile());
            item->setData(Qt::UserRole + 2, tr7("%1 items", "short").arg(urls.size()));
            item->setData(Qt::UserRole + 6, file);
        }
    }

    QList<QUrl> itemUrls(QListWidgetItem *it) const
    {
        if (m_node == NodeVideos || m_node == NodePictures) {
            return {QUrl::fromLocalFile(it->data(Qt::UserRole + 1).toString())};
        }
        if (m_node == NodePlaylists) {
            return readPlaylist(it->data(Qt::UserRole + 6).toString());
        }
        const QString key = it->data(Qt::UserRole + 5).toString();
        QList<QUrl> urls;
        for (const auto &m : m_lib->items(MediaItem::Audio)) {
            if (groupKey(m) == key) {
                urls << m.url();
            }
        }
        return urls;
    }

    QList<QUrl> selectedMusicUrls() const
    {
        QList<QUrl> urls;
        for (const QModelIndex &i : m_music->selectionModel()->selectedRows(0)) {
            const QString p = i.data(MusicDelegate::PathRole).toString();
            if (!p.isEmpty()) {
                urls << QUrl::fromLocalFile(p);
            }
        }
        return urls;
    }

    void playFromMusic(const QModelIndex &idx)
    {
        const QString path = m_musicModel->index(idx.row(), 0).data(MusicDelegate::PathRole).toString();
        if (path.isEmpty()) {
            return;
        }
        QList<QUrl> urls;
        int start = 0;
        for (int r = 0; r < m_musicModel->rowCount(); ++r) {
            const QString p = m_musicModel->item(r, 0)->data(MusicDelegate::PathRole).toString();
            if (!p.isEmpty()) {
                if (p == path) {
                    start = urls.size();
                }
                urls << QUrl::fromLocalFile(p);
            }
        }
        m_engine->playList(urls, start);
    }
    void playFromIcons(QListWidgetItem *it)
    {
        if (m_node == NodeVideos) {
            QList<QUrl> urls;
            int start = 0;
            for (int i = 0; i < m_icons->count(); ++i) {
                if (m_icons->item(i) == it) {
                    start = urls.size();
                }
                urls << QUrl::fromLocalFile(m_icons->item(i)->data(Qt::UserRole + 1).toString());
            }
            m_engine->playList(urls, start);
            Q_EMIT videoStarted();
            return;
        }
        if (m_node == NodePictures) {
            QProcess::startDetached(QStringLiteral("seven-photoviewer"), {it->data(Qt::UserRole + 1).toString()});
            return;
        }
        if (m_node == NodePlaylists) {
            // open the playlist
            const QString file = it->data(Qt::UserRole + 6).toString();
            for (int i = 0; i < m_playlists->childCount(); ++i) {
                if (m_playlists->child(i)->data(0, Qt::UserRole + 1).toString() == file) {
                    pushHistory();
                    m_navigating = true;
                    m_nav->setCurrentItem(m_playlists->child(i));
                    m_navigating = false;
                    show(NodePlaylists, file);
                }
            }
            return;
        }
        // album / artist / genre tile: open it
        pushHistory();
        show(m_node, it->data(Qt::UserRole + 5).toString());
    }

    void itemMenu(const QPoint &global, const QList<QUrl> &urls)
    {
        if (urls.isEmpty()) {
            return;
        }
        QMenu m(this);
        QFont bold = m.font();
        bold.setBold(true);
        auto *play = m.addAction(tr7("Play"));
        play->setFont(bold);
        auto *playAll = m.addAction(tr7("Play all"));
        auto *addTo = m.addAction(m_listPane->isVisible() && m_listPane->mode() == ListPane::BurnMode ? tr7("Add to burn list")
                                  : m_listPane->isVisible() && m_listPane->mode() == ListPane::SyncMode ? tr7("Add to sync list")
                                                                                                         : tr7("Add to playlist"));
        m.addSeparator();
        QMenu *rate = m.addMenu(tr7("Rating"));
        for (int i = 5; i >= 0; --i) {
            auto *a = rate->addAction(i ? QStringLiteral("%1 %2").arg(i).arg(i == 1 ? tr7("star") : i < 5 ? tr7("stars") : tr7("stars", "many")) : tr7("Delete rating"));
            a->setData(i);
        }
        m.addSeparator();
        auto *open = m.addAction(tr7("Open file location"));
        m.addSeparator();
        auto *props = m.addAction(tr7("Properties"));
        QAction *chosen = m.exec(global);
        if (!chosen) {
            return;
        }
        if (chosen == play) {
            m_engine->playList(urls);
            if (m_node == NodeVideos) {
                Q_EMIT videoStarted();
            }
        } else if (chosen == playAll) {
            QList<QUrl> all;
            for (int r = 0; r < m_musicModel->rowCount(); ++r) {
                const QString p = m_musicModel->item(r, 0)->data(MusicDelegate::PathRole).toString();
                if (!p.isEmpty()) {
                    all << QUrl::fromLocalFile(p);
                }
            }
            m_engine->playList(m_stack->currentWidget() == m_music ? all : urls);
        } else if (chosen == addTo) {
            m_listPane->addUrls(urls);
            m_listPane->setVisible(true);
            m_band->setTab(m_listPane->mode());
        } else if (chosen == open) {
            QProcess::startDetached(QStringLiteral("dolphin"), {QStringLiteral("--select"), urls.first().toLocalFile()});
        } else if (chosen == props) {
            QProcess::startDetached(QStringLiteral("seven-control"), {QStringLiteral("--properties"), urls.first().toLocalFile()});
        } else if (chosen->parent() == rate) {
            for (const QUrl &u : urls) {
                m_lib->setRating(u.toLocalFile(), chosen->data().toInt());
            }
        }
    }

    void commandMenu(int index, const QPoint &global)
    {
        QMenu m(this);
        if (index == 0) {
            QMenu *manage = m.addMenu(tr7("Manage libraries"));
            auto *music = manage->addAction(tr7("Music"));
            auto *videos = manage->addAction(tr7("Videos"));
            auto *pictures = manage->addAction(tr7("Pictures"));
            auto *apply = m.addAction(tr7("Apply media information changes"));
            m.addSeparator();
            QMenu *sort = m.addMenu(tr7("Sort by"));
            const QStringList sorts{tr7("Title"), tr7("Artist"), tr7("Album"), tr7("Release date"),
                                    tr7("Date added"), tr7("Rating"), tr7("Length")};
            const int cur = QSettings().value(QStringLiteral("sortBy"), 0).toInt();
            for (int i = 0; i < sorts.size(); ++i) {
                auto *sa = sort->addAction(sorts[i]);
                sa->setCheckable(true);
                sa->setChecked(i == cur);
                sa->setData(i);
            }
            auto *custNav = m.addAction(tr7("Customize navigation pane..."));
            m.addSeparator();
            QMenu *layout = m.addMenu(tr7("Layout"));
            auto *showList = layout->addAction(tr7("Show list"));
            showList->setCheckable(true);
            showList->setChecked(m_listPane->isVisible());
            auto *showNav = layout->addAction(tr7("Show navigation pane"));
            showNav->setCheckable(true);
            showNav->setChecked(m_nav->isVisible());
            auto *showMenu = layout->addAction(tr7("Show menu bar"));
            showMenu->setCheckable(true);
            showMenu->setChecked(window()->findChild<QMenuBar *>() && window()->findChild<QMenuBar *>()->isVisible());
            auto *opts = m.addAction(tr7("Options..."));
            QAction *a = m.exec(global);
            if (a && a->parent() == sort) {
                QSettings().setValue(QStringLiteral("sortBy"), a->data().toInt());
                refreshView();
            } else if (a == custNav) {
                customizeNav();
            } else if (a == apply) {
                m_lib->rescan();
            } else if (a == music || a == videos || a == pictures) {
                Q_EMIT manageWanted(a == music ? MediaItem::Audio : a == videos ? MediaItem::Video : MediaItem::Picture);
            } else if (a == showList) {
                m_listPane->setVisible(!m_listPane->isVisible());
                m_band->setTab(m_listPane->isVisible() ? 0 : -1);
            } else if (a == showNav) {
                m_nav->setVisible(!m_nav->isVisible());
            } else if (a == showMenu) {
                Q_EMIT menuBarToggled();
            } else if (a == opts) {
                Q_EMIT optionsWanted();
            }
        } else if (index == 1) {
            auto *a1 = m.addAction(tr7("Allow Internet access to home media..."));
            auto *a2 = m.addAction(tr7("Allow remote control of Player..."));
            auto *a3 = m.addAction(tr7("Automatically allow devices to play my media..."));
            m.addSeparator();
            auto *a4 = m.addAction(tr7("More streaming options..."));
            for (auto *a : {a1, a2, a3, a4}) {
                a->setEnabled(false);
            }
            m.exec(global);
        } else {
            auto *pl = m.addAction(tr7("Create playlist"));
            auto *auto_ = m.addAction(tr7("Create auto playlist"));
            QAction *chosen = m.exec(global);
            if (chosen == pl) {
                createPlaylist();
            } else if (chosen == auto_) {
                createAutoPlaylist();
            }
        }
    }

Q_SIGNALS:
    void videoStarted();
    void optionsWanted();
    void manageWanted(MediaItem::Kind kind);
    void menuBarToggled();

public:
    void createPlaylist()
    {
        bool ok = false;
        const QString name = QInputDialog::getText(this, APP_TITLE, tr7("Playlist name:"), QLineEdit::Normal, tr7("Playlist"), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            QDir().mkpath(playlistsDir());
            QFile f(playlistsDir() + QLatin1Char('/') + name.trimmed() + QStringLiteral(".m3u"));
            if (f.open(QIODevice::WriteOnly)) {
                f.write("#EXTM3U\n");
            }
            refreshPlaylists();
            m_playlists->setExpanded(true);
        }
    }
    void createAutoPlaylist()
    {
        AutoPl::Dialog d(this);
        if (d.exec() != QDialog::Accepted || d.name().isEmpty()) {
            return;
        }
        QDir().mkpath(playlistsDir());
        const QString file = playlistsDir() + QLatin1Char('/') + d.name() + QStringLiteral(".autopl");
        AutoPl::save(file, d.rules());
        refreshPlaylists();
        m_playlists->setExpanded(true);
        pushHistory();
        show(NodePlaylists, file);
    }
    void saveNowPlaying()
    {
        m_listPane->save();
    }
    void toggleListPane()
    {
        m_listPane->setVisible(!m_listPane->isVisible());
        m_band->setTab(m_listPane->isVisible() ? 0 : -1);
    }
    void openTab(int tab)
    {
        Q_EMIT m_band->tabClicked(tab);
    }
    void toggleNavPane()
    {
        m_nav->setVisible(!m_nav->isVisible());
    }
    void focusSearch()
    {
        m_cmd->search()->setFocus();
        m_cmd->search()->selectAll();
    }

private:
    Engine *m_engine;
    Library *m_lib;
    AddressBand *m_band;
    CommandBar *m_cmd;
    QTreeWidget *m_nav;
    QTreeWidgetItem *m_playlists = nullptr;
    QTreeWidgetItem *m_music_ = nullptr;
    QStackedWidget *m_stack;
    QTreeView *m_music;
    MusicModel *m_musicModel;
    MusicDelegate *m_musicDelegate;
    IconList *m_icons;
    ListPane *m_listPane;
    Transport *m_transport;
    QPointer<KIO::PreviewJob> m_previewJob;
    int m_node = -1;
    QString m_arg;
    bool m_navigating = false;
    QList<QPair<int, QString>> m_back, m_forward;
};

// ----------------------------------------------------------------------------------------------------------------

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow()
    {
        setWindowTitle(APP_TITLE);
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-wmp")));
        setAcceptDrops(true);
        m_lib = new Library(this);
        g_library = m_lib;
        m_engine = new Engine(this);
        m_stack = new QStackedWidget(this);
        setCentralWidget(m_stack);
        m_library = new LibraryPage(m_engine, m_lib, m_stack);
        m_np = new NowPlaying(m_engine, m_stack);
        m_stack->addWidget(m_library);
        m_stack->addWidget(m_np);

        connect(m_library->transport(), &Transport::switchClicked, this, &MainWindow::showNowPlaying);
        connect(m_library, &LibraryPage::videoStarted, this, &MainWindow::showNowPlaying);
        connect(m_library, &LibraryPage::optionsWanted, this, &MainWindow::options);
        connect(m_library, &LibraryPage::manageWanted, this, &MainWindow::manageLibrary);
        connect(m_library, &LibraryPage::menuBarToggled, this, [this] {
            setMenuBarShown(!menuBar()->isVisible());
        });
        connect(m_np, &NowPlaying::switchClicked, this, &MainWindow::showLibrary);
        connect(m_np, &NowPlaying::fullScreenClicked, this, &MainWindow::toggleFullScreen);
        connect(m_np, &NowPlaying::contextMenuWanted, this, &MainWindow::nowPlayingMenu);
        connect(m_engine, &Engine::changed, this, &MainWindow::updateTitle);
        if (!qEnvironmentVariableIsSet("WIN7_GRAB")) {
            registerMpris(new QObject(this), this, m_engine, m_lib);
        }
        m_eq = new Equalizer(m_engine, this);
        connect(m_engine, &Engine::changed, this, &MainWindow::updateInhibit);

        buildMenus();

        QSettings s;
        if (!restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray())) {
            resize(1080, 700);
        }
        m_libraryGeometry = saveGeometry();
        updateTitle();
        if (m_lib->all().isEmpty()) {
            m_lib->rescan();
        } else {
            QTimer::singleShot(1500, m_lib, &Library::rescan);
        }
    }

    void openArgs(const QStringList &args)
    {
        QList<QUrl> urls;
        for (const QString &a : args) {
            const QUrl u = QUrl::fromUserInput(a, QDir::currentPath(), QUrl::AssumeLocalFile);
            const QString lower = a.toLower();
            if (u.isLocalFile() && (lower.endsWith(QLatin1String(".m3u")) || lower.endsWith(QLatin1String(".m3u8")) || lower.endsWith(QLatin1String(".wpl")))) {
                urls << readPlaylist(u.toLocalFile());
            } else {
                urls << u;
            }
        }
        if (urls.isEmpty()) {
            return;
        }
        m_engine->playList(urls);
        // a file opened from Explorer plays in Now Playing (WMP opens the compact mode for it)
        showNowPlaying();
        raise();
        activateWindow();
    }

protected:
    void closeEvent(QCloseEvent *e) override
    {
        QSettings s;
        s.setValue(QStringLiteral("geometry"), m_stack->currentWidget() == m_library ? saveGeometry() : m_libraryGeometry);
        m_engine->stop();
        e->accept();
    }
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (e->mimeData()->hasUrls()) {
            e->acceptProposedAction();
        }
    }
    void dropEvent(QDropEvent *e) override
    {
        QStringList args;
        for (const QUrl &u : e->mimeData()->urls()) {
            args << (u.isLocalFile() ? u.toLocalFile() : u.toString());
        }
        openArgs(args);
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        QMainWindow::mouseDoubleClickEvent(e);
    }

private:
    void changeVolume(double d)
    {
        m_engine->audio()->setVolume(qBound(0.0, m_engine->audio()->volume() + d, 1.0));
    }
    void seek(qint64 d)
    {
        auto *p = m_engine->player();
        p->setPosition(qBound<qint64>(0, p->position() + d, p->duration()));
    }
    void updateTitle()
    {
        // Now Playing: no caption text (like WMP); the library: "Програвач Windows Media"
        if (m_stack->currentWidget() == m_np) {
            setWindowTitle(APP_TITLE);
        } else {
            setWindowTitle(APP_TITLE);
        }
    }
    void showNowPlaying()
    {
        if (m_stack->currentWidget() == m_np) {
            return;
        }
        m_libraryGeometry = saveGeometry();
        m_stack->setCurrentWidget(m_np);
        // Now Playing uses a smaller window sized to the video
        if (!isMaximized() && !isFullScreen()) {
            QSize s(800, 560);
            const auto res = m_engine->player()->metaData().value(QMediaMetaData::Resolution).toSize();
            if (res.isValid() && res.width() > 0) {
                const QSize avail = screen()->availableGeometry().size() * 0.8;
                QSize v = res.scaled(QSize(qMin(res.width(), avail.width()), qMin(res.height(), avail.height() - 117)), Qt::KeepAspectRatio);
                s = QSize(v.width(), v.height() + 117);
            }
            resize(s);
        }
        updateTitle();
        applyOptions();
    }
public:
    void showLibrary()
    {
        if (isFullScreen()) {
            toggleFullScreen();
        }
        m_stack->setCurrentWidget(m_library);
        restoreGeometry(m_libraryGeometry);
        updateTitle();
        applyOptions();
    }
private:
    void toggleFullScreen()
    {
        if (isFullScreen()) {
            showNormal();
            if (m_wasMaximized) {
                showMaximized();
            }
            m_np->setControlsShown(true);
            m_np->setCursor(Qt::ArrowCursor);
        } else {
            if (m_stack->currentWidget() != m_np) {
                showNowPlaying();
            }
            m_wasMaximized = isMaximized();
            showFullScreen();
            m_np->activity();
        }
    }
    void nowPlayingMenu(const QPoint &global)
    {
        QMenu m(this);
        auto *showList = m.addAction(m_np->listShown() ? tr7("Hide list") : tr7("Show list", "button"));
        m.addSeparator();
        auto *full = m.addAction(isFullScreen() ? tr7("Exit full-screen mode") : tr7("Full screen"));
        full->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Return));
        m.addSeparator();
        auto *shuffle = m.addAction(tr7("Shuffle"));
        shuffle->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
        shuffle->setCheckable(true);
        shuffle->setChecked(m_engine->shuffle());
        auto *repeat = m.addAction(tr7("Repeat"));
        repeat->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
        repeat->setCheckable(true);
        repeat->setChecked(m_engine->repeat());
        m.addSeparator();
        QMenu *viz = m.addMenu(tr7("Visualizations"));
        viz->setEnabled(!m_engine->player()->hasVideo());
        auto *vArt = viz->addAction(tr7("Album Art"));
        QMenu *bw = viz->addMenu(tr7("Bars and Waves"));
        auto *vBars = bw->addAction(tr7("Bars"));
        auto *vScope = bw->addAction(tr7("Wave"));
        for (auto *a : {vArt, vBars, vScope}) {
            a->setCheckable(true);
        }
        vArt->setChecked(m_np->art()->viz() == ArtView::Art);
        vBars->setChecked(m_np->art()->viz() == ArtView::Bars);
        vScope->setChecked(m_np->art()->viz() == ArtView::Scope);
        QMenu *video = m.addMenu(tr7("Videos"));
        auto *fit = video->addAction(tr7("Fit video to player on resize"));
        fit->setCheckable(true);
        fit->setChecked(true);
        auto *x100 = video->addAction(tr7("Zoom 100%"));
        video->addSeparator();
        auto *x50 = video->addAction(QStringLiteral("50%"));
        auto *x200 = video->addAction(QStringLiteral("200%"));
        QMenu *enh = m.addMenu(tr7("Enhancements"));
        auto *eqAct = enh->addAction(tr7("Graphic equalizer"));
        auto *speed = enh->addMenu(tr7("Play speed settings"));
        for (double r : {0.5, 1.0, 1.4, 2.0}) {
            auto *a = speed->addAction(r == 1.0 ? tr7("Normal") : r < 1 ? tr7("Slow (0.5x)") : tr7("Fast (%1x)").arg(QString::number(r).replace(QLatin1Char('.'), QLatin1Char(','))));
            a->setCheckable(true);
            a->setChecked(qFuzzyCompare(m_engine->player()->playbackRate(), r));
            a->setData(r);
        }
        QMenu *subs = m.addMenu(tr7("Lyrics, captions, and subtitles"));
        auto *subsOn = subs->addAction(tr7("On if available"));
        subsOn->setCheckable(true);
        subsOn->setChecked(m_engine->player()->activeSubtitleTrack() >= 0);
        auto *subsOff = subs->addAction(tr7("Turn off"));
        subsOff->setCheckable(true);
        subsOff->setChecked(m_engine->player()->activeSubtitleTrack() < 0);
        m.addSeparator();
        auto *onTop = m.addAction(tr7("Always show Now Playing on top"));
        onTop->setCheckable(true);
        onTop->setChecked(Win7KWin::keepAbove(this));
        m.addSeparator();
        auto *more = m.addAction(tr7("More options..."));
        m.addSeparator();
        auto *help = m.addAction(tr7("Help with playback..."));
        Q_UNUSED(help)
        QAction *a = m.exec(global);
        if (!a) {
            return;
        }
        if (a == eqAct) {
            showEq();
        } else if (a == showList) {
            m_np->setListShown(!m_np->listShown());
        } else if (a == vArt || a == vBars || a == vScope) {
            m_np->art()->setViz(a == vArt ? ArtView::Art : a == vBars ? ArtView::Bars : ArtView::Scope);
        } else if (a == full) {
            toggleFullScreen();
        } else if (a == shuffle) {
            m_engine->setShuffle(!m_engine->shuffle());
        } else if (a == repeat) {
            m_engine->setRepeat(!m_engine->repeat());
        } else if (a == x100 || a == x50 || a == x200) {
            const auto res = m_engine->player()->metaData().value(QMediaMetaData::Resolution).toSize();
            if (res.isValid() && !isFullScreen()) {
                const double k = a == x50 ? 0.5 : a == x200 ? 2.0 : 1.0;
                showNormal();
                resize(int(res.width() * k), int(res.height() * k) + 117);
            }
        } else if (a->parent() == speed) {
            m_engine->player()->setPlaybackRate(a->data().toDouble());
        } else if (a == subsOn) {
            if (!m_engine->player()->subtitleTracks().isEmpty()) {
                m_engine->player()->setActiveSubtitleTrack(0);
            }
        } else if (a == subsOff) {
            m_engine->player()->setActiveSubtitleTrack(-1);
        } else if (a == onTop) {
            Win7KWin::setKeepAbove(this, !Win7KWin::keepAbove(this));
        } else if (a == more) {
            options();
        }
    }
    void openFiles()
    {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr7("Open"), QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
                                                                tr7("Media files (*.mp3 *.wma *.wav *.flac *.ogg *.m4a *.aac *.opus *.mp4 *.mkv *.avi *.wmv *.mov *.webm *.mpg *.mpeg *.m3u *.wpl);;All Files (*)"));
        if (!files.isEmpty()) {
            openArgs(files);
        }
    }
    void options()
    {
        OptionsDialog d(this);
        d.manageLibrary = [this](MediaItem::Kind k) {
            manageLibrary(k);
        };
        d.onApply = [this] {
            applyOptions();
        };
        d.exec();
    }
    void manageLibrary(MediaItem::Kind kind)
    {
        LocationsDialog d(kind, this);
        if (d.exec() == QDialog::Accepted) {
            m_lib->rescan();
        }
    }
    /** no screen saver while a video plays (Options > Player: "Дозволити заставку під час відтворення") */
    void updateInhibit()
    {
        const bool want = m_engine->player()->playbackState() == QMediaPlayer::PlayingState && m_engine->player()->hasVideo()
            && !Opt::value(QStringLiteral("screensaver"), false);
        QDBusInterface ss(QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/org/freedesktop/ScreenSaver"), QStringLiteral("org.freedesktop.ScreenSaver"));
        if (want && !m_inhibit) {
            QDBusReply<uint> r = ss.call(QStringLiteral("Inhibit"), QStringLiteral("seven-wmp"), tr7("Video playback"));
            if (r.isValid()) {
                m_inhibit = r.value();
            }
        } else if (!want && m_inhibit) {
            ss.call(QStringLiteral("UnInhibit"), m_inhibit);
            m_inhibit = 0;
        }
    }
    void applyOptions()
    {
        const bool top = Opt::value(QStringLiteral("onTop"), false) && m_stack->currentWidget() == m_np;
        if (Win7KWin::keepAbove(this) != top) {
            Win7KWin::setKeepAbove(this, top);
        }
    }

    void setMenuBarShown(bool on)
    {
        menuBar()->setVisible(on);
        m_menuBarPinned = on;
        QSettings().setValue(QStringLiteral("menuBar"), on);
    }

    /** the classic menus (hidden like in WMP 12: Alt shows them, Ctrl+M keeps them) */
    void buildMenus()
    {
        QMenuBar *mb = menuBar();
        auto act = [this](QMenu *m, const QString &text, const QKeySequence &key, auto fn) {
            QAction *a = m->addAction(text);
            if (!key.isEmpty()) {
                a->setShortcut(key);
                a->setShortcutContext(Qt::WindowShortcut);
                addAction(a); // shortcuts work while the menu bar is hidden
            }
            connect(a, &QAction::triggered, this, fn);
            return a;
        };
        QMenu *file = mb->addMenu(tr7("&File"));
        act(file, tr7("&Open..."), QKeySequence(Qt::CTRL | Qt::Key_O), [this] { openFiles(); });
        act(file, tr7("Open &URL..."), QKeySequence(Qt::CTRL | Qt::Key_U), [this] { openUrl(); });
        act(file, tr7("&Save As..."), {}, [] {})->setEnabled(false);
        act(file, tr7("&Close"), QKeySequence(Qt::CTRL | Qt::Key_W), [this] { m_engine->clear(); });
        file->addSeparator();
        act(file, tr7("Create &playlist"), QKeySequence(Qt::CTRL | Qt::Key_N), [this] { m_library->createPlaylist(); });
        act(file, tr7("Create &auto playlist"), {}, [this] { m_library->createAutoPlaylist(); });
        act(file, tr7("Save &now playing list"), {}, [this] { m_library->saveNowPlaying(); });
        file->addSeparator();
        QMenu *manage = file->addMenu(tr7("&Manage libraries"));
        act(manage, tr7("&Music"), {}, [this] { manageLibrary(MediaItem::Audio); });
        act(manage, tr7("&Videos"), {}, [this] { manageLibrary(MediaItem::Video); });
        act(manage, tr7("&Pictures"), {}, [this] { manageLibrary(MediaItem::Picture); });
        act(file, tr7("&Apply media information changes"), {}, [this] { m_lib->rescan(); });
        act(file, tr7("&Add to library..."), {}, [this] { manageLibrary(MediaItem::Audio); });
        file->addSeparator();
        act(file, tr7("Work &Offline"), {}, [] {})->setCheckable(true);
        file->addSeparator();
        act(file, tr7("E&xit"), {}, [this] { close(); });

        QMenu *view = mb->addMenu(tr7("&View"));
        act(view, tr7("&Library"), QKeySequence(Qt::CTRL | Qt::Key_1), [this] { showLibrary(); });
        act(view, tr7("&Skin chooser"), QKeySequence(Qt::CTRL | Qt::Key_2), [this] { showSkin(); });
        act(view, tr7("&Now Playing"), QKeySequence(Qt::CTRL | Qt::Key_3), [this] { showNowPlaying(); });
        act(view, tr7("&Skin Chooser"), {}, [this] { skinChooser(); });
        view->addSeparator();
        act(view, tr7("&Navigation pane"), {}, [this] { m_library->toggleNavPane(); });
        act(view, tr7("&List pane"), {}, [this] { m_library->toggleListPane(); });
        act(view, tr7("&Menu bar"), QKeySequence(Qt::CTRL | Qt::Key_M), [this] { setMenuBarShown(!m_menuBarPinned); });
        view->addSeparator();
        act(view, tr7("&Full screen"), QKeySequence(Qt::ALT | Qt::Key_Return), [this] { toggleFullScreen(); });
        act(view, tr7("&Refresh"), QKeySequence(Qt::Key_F5), [this] { m_lib->rescan(); });
        act(view, tr7("S&earch"), QKeySequence(Qt::CTRL | Qt::Key_E), [this] {
            showLibrary();
            m_library->focusSearch();
        });

        QMenu *play = mb->addMenu(tr7("&Play"));
        act(play, tr7("&Play/Pause"), QKeySequence(Qt::CTRL | Qt::Key_P), [this] { m_engine->playPause(); });
        act(play, tr7("&Stop"), QKeySequence(Qt::CTRL | Qt::Key_S), [this] { m_engine->stop(); });
        play->addSeparator();
        QMenu *speed = play->addMenu(tr7("Play &speed"));
        for (auto [name, rate] : {std::pair{tr7("&Slow"), 0.5}, std::pair{tr7("&Normal"), 1.0}, std::pair{tr7("&Fast"), 1.4}}) {
            act(speed, name, {}, [this, rate] { m_engine->player()->setPlaybackRate(rate); });
        }
        play->addSeparator();
        act(play, tr7("P&revious"), QKeySequence(Qt::CTRL | Qt::Key_B), [this] { m_engine->previous(); });
        act(play, tr7("&Next"), QKeySequence(Qt::CTRL | Qt::Key_F), [this] { m_engine->next(); });
        act(play, tr7("Re&wind"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B), [this] { seek(-10000); });
        act(play, tr7("Fast &Forward"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), [this] { seek(+10000); });
        play->addSeparator();
        m_shuffleAct = act(play, tr7("S&huffle"), QKeySequence(Qt::CTRL | Qt::Key_H), [this] { m_engine->setShuffle(!m_engine->shuffle()); });
        m_repeatAct = act(play, tr7("R&epeat"), QKeySequence(Qt::CTRL | Qt::Key_T), [this] { m_engine->setRepeat(!m_engine->repeat()); });
        m_shuffleAct->setCheckable(true);
        m_repeatAct->setCheckable(true);
        play->addSeparator();
        QMenu *vol = play->addMenu(tr7("&Volume"));
        act(vol, tr7("&Up"), QKeySequence(Qt::Key_F9), [this] { changeVolume(+0.05); });
        act(vol, tr7("&Down"), QKeySequence(Qt::Key_F8), [this] { changeVolume(-0.05); });
        act(vol, tr7("&Mute"), QKeySequence(Qt::Key_F7), [this] { m_engine->audio()->setMuted(!m_engine->audio()->isMuted()); });
        act(play, tr7("&Eject"), QKeySequence(Qt::CTRL | Qt::Key_J), [] {})->setEnabled(false);
        connect(play, &QMenu::aboutToShow, this, [this] {
            m_shuffleAct->setChecked(m_engine->shuffle());
            m_repeatAct->setChecked(m_engine->repeat());
        });

        QMenu *tools = mb->addMenu(tr7("&Tools"));
        QMenu *dl = tools->addMenu(tr7("&Download"));
        dl->setEnabled(false);
        QMenu *plug = tools->addMenu(tr7("&Plug-ins"));
        plug->setEnabled(false);
        tools->addSeparator();
        act(tools, tr7("&Options..."), {}, [this] { options(); });
        QMenu *adv = tools->addMenu(tr7("&Advanced"));
        act(adv, tr7("&Restore media library"), {}, [this] { m_lib->rescan(); });

        QMenu *help = mb->addMenu(tr7("&Help"));
        act(help, tr7("Windows Media Player &Help"), QKeySequence(Qt::Key_F1), [] {})->setEnabled(false);
        help->addSeparator();
        act(help, tr7("&About Windows Media Player"), {}, [this] { about(); });

        // shortcuts that are not in the menus
        auto sc = [this](const QKeySequence &k, auto fn) {
            auto *s = new QShortcut(k, this);
            s->setContext(Qt::WindowShortcut);
            connect(s, &QShortcut::activated, this, fn);
        };
        sc(QKeySequence(Qt::Key_MediaPlay), [this] { m_engine->playPause(); });
        sc(QKeySequence(Qt::Key_MediaTogglePlayPause), [this] { m_engine->playPause(); });
        sc(QKeySequence(Qt::Key_MediaStop), [this] { m_engine->stop(); });
        sc(QKeySequence(Qt::Key_MediaPrevious), [this] { m_engine->previous(); });
        sc(QKeySequence(Qt::Key_MediaNext), [this] { m_engine->next(); });
        sc(QKeySequence(Qt::Key_Escape), [this] {
            if (isFullScreen()) {
                toggleFullScreen();
            }
        });
        m_menuBarPinned = QSettings().value(QStringLiteral("menuBar"), false).toBool();
        mb->setVisible(m_menuBarPinned);
        // a lone Alt shows the menu bar until a menu closes (WMP 12)
        qApp->installEventFilter(this);
    }
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::KeyPress) {
            auto *k = static_cast<QKeyEvent *>(e);
            m_altAlone = k->key() == Qt::Key_Alt && !k->isAutoRepeat();
        } else if (e->type() == QEvent::KeyRelease) {
            auto *k = static_cast<QKeyEvent *>(e);
            if (k->key() == Qt::Key_Alt && m_altAlone && isActiveWindow() && !isFullScreen()) {
                m_altAlone = false;
                if (!m_menuBarPinned) {
                    // WMP 12: Alt pops up the classic menus as one menu at the top left, plus "Show menu bar"
                    QMenu pop(this);
                    for (QAction *a : menuBar()->actions()) {
                        pop.addAction(a);
                    }
                    pop.addSeparator();
                    QAction *show = pop.addAction(tr7("Show menu bar"));
                    show->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
                    if (pop.exec(mapToGlobal(QPoint(16, 34))) == show) {
                        setMenuBarShown(true);
                    }
                } else {
                    menuBar()->setActiveAction(menuBar()->actions().first());
                }
                return true;
            }
        } else if (e->type() == QEvent::MouseButtonPress) {
            m_altAlone = false;
        }
        return QMainWindow::eventFilter(o, e);
    }
    QList<QMenu *> classicMenus() const
    {
        QList<QMenu *> out;
        for (QAction *a : menuBar()->actions()) {
            if (a->menu()) {
                out << a->menu();
            }
        }
        return out;
    }
    QString currentSkin() const
    {
        return QSettings().value(QStringLiteral("skin"), QStringLiteral("corporate")).toString();
    }
    void showSkin()
    {
        if (isFullScreen()) {
            toggleFullScreen();
        }
        const bool corporate = currentSkin() == QLatin1String("corporate");
        if (corporate && !m_corp) {
            m_corp = new CorporateWindow(m_engine, classicMenus());
            connect(m_corp, &CorporateWindow::returnToLibrary, this, &MainWindow::leaveSkin);
            connect(m_corp, &CorporateWindow::closing, this, [this] {
                QTimer::singleShot(0, this, [this] {
                    if (!isVisible()) {
                        m_engine->stop();
                        qApp->quit();
                    }
                });
            });
            auto *back = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_1), m_corp);
            connect(back, &QShortcut::activated, this, &MainWindow::leaveSkin);
            m_corp->move(geometry().center() - QPoint(150, 138));
        }
        if (!corporate && !m_skin) {
            m_skin = new SkinWindow(m_engine, m_lib);
            connect(m_skin, &SkinWindow::returnToLibrary, this, &MainWindow::leaveSkin);
            connect(m_skin, &SkinWindow::menuWanted, this, &MainWindow::nowPlayingMenu);
            connect(m_skin, &SkinWindow::eqWanted, this, &MainWindow::showEq);
            connect(m_skin, &SkinWindow::closeWanted, this, [this] {
                leaveSkin();
                close();
            });
            auto *back = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_1), m_skin);
            connect(back, &QShortcut::activated, this, &MainWindow::leaveSkin);
            m_skin->move(geometry().center() - QPoint(128, 65));
        }
        hide();
        QWidget *w = corporate ? static_cast<QWidget *>(m_corp) : static_cast<QWidget *>(m_skin);
        m_engine->player()->setVideoOutput(corporate ? m_corp->videoWidget() : m_skin->videoWidget());
        w->show();
        w->raise();
        w->activateWindow();
    }
    void leaveSkin()
    {
        bool was = false;
        for (QWidget *w : {static_cast<QWidget *>(m_skin), static_cast<QWidget *>(m_corp)}) {
            if (w && w->isVisible()) {
                w->hide();
                was = true;
            }
        }
        if (!was) {
            return;
        }
        m_engine->player()->setVideoOutput(m_np->videoWidget());
        show();
        raise();
        activateWindow();
    }
    /** View > Skin chooser */
    void skinChooser()
    {
        QDialog d(this);
        d.setWindowTitle(tr7("Skin Chooser"));
        auto *l = new QHBoxLayout(&d);
        auto *list = new QListWidget(&d);
        list->setFixedWidth(150);
        list->addItem(QStringLiteral("Corporate"));
        list->addItem(QStringLiteral("Revert"));
        list->setCurrentRow(currentSkin() == QLatin1String("revert") ? 1 : 0);
        l->addWidget(list);
        auto *right = new QVBoxLayout;
        auto *preview = new QLabel(&d);
        preview->setAlignment(Qt::AlignCenter);
        preview->setMinimumSize(320, 280);
        preview->setStyleSheet(QStringLiteral("background: #e8e8e8;"));
        auto *info = new QLabel(&d);
        info->setAlignment(Qt::AlignCenter);
        right->addWidget(preview, 1);
        right->addWidget(info);
        auto *bb = new QDialogButtonBox(&d);
        auto *apply = bb->addButton(tr7("Apply Skin"), QDialogButtonBox::AcceptRole);
        bb->addButton(tr7("Cancel"), QDialogButtonBox::RejectRole);
        Q_UNUSED(apply)
        right->addWidget(bb);
        l->addLayout(right, 1);
        auto update = [&] {
            const bool corp = list->currentRow() == 0;
            QPixmap pm;
            if (corp) {
                // a little Corporate window: the top strip, the screen, the control strip
                pm = QPixmap(300, 230);
                pm.fill(Qt::black);
                QPainter p(&pm);
                const QString dir = QDir::homePath() + QStringLiteral("/.local/share/seven-wmp/skins/corporate/");
                p.drawPixmap(QRect(0, 0, 200, 13), QPixmap(dir + QStringLiteral("1801.png")));
                p.drawPixmap(198, 0, QPixmap(dir + QStringLiteral("1803.png")));
                p.drawPixmap(QRect(0, 171, 300, 59), QPixmap(dir + QStringLiteral("1802.png")));
                p.drawPixmap(2, 193, QPixmap(dir + QStringLiteral("1815.png")));
                p.drawPixmap(33, 199, QPixmap(dir + QStringLiteral("1821.png")));
                p.drawPixmap(55, 211, QPixmap(dir + QStringLiteral("1825.png")));
                p.drawPixmap(111, 211, QPixmap(dir + QStringLiteral("1840.png")));
                p.drawPixmap(139, 211, QPixmap(dir + QStringLiteral("1843.png")));
            } else {
                pm = QPixmap(QDir::homePath() + QStringLiteral("/.local/share/seven-wmp/skins/revert/player_up.png"));
            }
            preview->setPixmap(pm);
            info->setText(tr7("Name: %1\nAuthor: Microsoft Corporation\n© Microsoft Corporation. All rights reserved.").arg(corp ? QStringLiteral("Corporate") : QStringLiteral("Revert")));
        };
        connect(list, &QListWidget::currentRowChanged, &d, update);
        connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        update();
        if (d.exec() == QDialog::Accepted) {
            QSettings().setValue(QStringLiteral("skin"), list->currentRow() == 0 ? QStringLiteral("corporate") : QStringLiteral("revert"));
            showSkin();
        }
    }
public Q_SLOTS:
    void showEqSlot()
    {
        showEq();
    }

private:
    void showEq()
    {
        if (!m_eqWin) {
            m_eqWin = new EqWindow(m_eq, this);
        }
        // above the player window like WMP's enhancement panes
        QWidget *ref = isVisible() ? static_cast<QWidget *>(this) : m_skin && m_skin->isVisible() ? static_cast<QWidget *>(m_skin) : static_cast<QWidget *>(m_corp);
        if (ref) {
            const QRect g = ref->frameGeometry();
            m_eqWin->move(g.x(), qMax(0, g.y() - m_eqWin->height()));
        }
        m_eqWin->show();
        m_eqWin->raise();
    }
    void openUrl()
    {
        bool ok = false;
        const QString u = QInputDialog::getText(this, tr7("Open URL"), tr7("Enter the URL or path to a media file on the Internet, your network, or your computer that you want to play."),
                                                QLineEdit::Normal, QStringLiteral("http://"), &ok);
        if (ok && !u.trimmed().isEmpty()) {
            openArgs({u.trimmed()});
        }
    }
    void about()
    {
        QMessageBox box(this);
        box.setWindowTitle(tr7("About Windows Media Player"));
        box.setIconPixmap(QIcon::fromTheme(QStringLiteral("seven-wmp")).pixmap(48, 48));
        box.setText(tr7("<b>Windows Media Player</b><br>Version 12.0.7601.17514"));
        box.setInformativeText(tr7("© 2009 Microsoft Corporation. All rights reserved.<br><br>Playback: Qt Multimedia (FFmpeg)."));
        box.exec();
    }

    Engine *m_engine;
    Library *m_lib;
    QStackedWidget *m_stack;
    LibraryPage *m_library;
    NowPlaying *m_np;
    QByteArray m_libraryGeometry;
    bool m_wasMaximized = false;
    bool m_menuBarPinned = false;
    uint m_inhibit = 0;
    SkinWindow *m_skin = nullptr;
    CorporateWindow *m_corp = nullptr;
    Equalizer *m_eq = nullptr;
    EqWindow *m_eqWin = nullptr;
    bool m_altAlone = false;
    QAction *m_shuffleAct = nullptr;
    QAction *m_repeatAct = nullptr;
};

int main(int argc, char **argv)
{
    // Qt's PipeWire audio backend lists only hardware sinks (no Bluetooth headphones, no filter sinks such as our
    // equalizer) and pins its stream to one device; the PulseAudio backend follows the default sink like WMP
    if (!qEnvironmentVariableIsSet("QT_AUDIO_BACKEND")) {
        qputenv("QT_AUDIO_BACKEND", "pulseaudio");
    }
    // name and icon of our stream in the volume mixer
    qputenv("PULSE_PROP", "application.name='Windows Media Player' application.icon_name='seven-wmp' media.role='music'");
    QApplication app(argc, argv);
    Win7I18n::install(&app);
    app.setOrganizationName(QStringLiteral("win7"));
    app.setApplicationName(QStringLiteral("seven-wmp"));
    app.setApplicationDisplayName(APP_TITLE);
    app.setDesktopFileName(QStringLiteral("seven-wmp"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-wmp")));

    QStringList files = app.arguments().mid(1);

    // one player: a second start hands its files to the running one
    const QString serverName = QStringLiteral("seven-wmp-") + qEnvironmentVariable("USER");
    const bool testRun = qEnvironmentVariableIsSet("WIN7_GRAB"); // test grabs never talk to the user's player
    if (!testRun) {
        QLocalSocket sock;
        sock.connectToServer(serverName);
        if (sock.waitForConnected(300)) {
            sock.write(files.join(QLatin1Char('\n')).toUtf8() + "\n\x04");
            sock.waitForBytesWritten(1000);
            sock.disconnectFromServer();
            return 0;
        }
    }
    QLocalServer server;
    if (!testRun) {
        QLocalServer::removeServer(serverName);
        server.listen(serverName);
    }

    MainWindow w;
    QObject::connect(&server, &QLocalServer::newConnection, &w, [&] {
        QLocalSocket *s = server.nextPendingConnection();
        auto buf = std::make_shared<QByteArray>();
        QObject::connect(s, &QLocalSocket::readyRead, &w, [s, buf, &w] {
            *buf += s->readAll();
            if (buf->endsWith('\x04')) {
                QStringList args = QString::fromUtf8(buf->chopped(1)).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                w.openArgs(args);
                if (args.isEmpty()) {
                    w.showNormal();
                    w.raise();
                    w.activateWindow();
                }
                s->deleteLater();
            }
        });
    });
    w.show();
    if (!files.isEmpty()) {
        w.openArgs(files);
    }
    // test hook: WIN7_GRAB=<png> [WIN7_GRAB_WAIT=ms] [WMP_NODE=n] grabs the window and quits
    if (qEnvironmentVariableIsSet("WMP_SHOWLIB")) {
        w.showLibrary();
    }
    if (qEnvironmentVariableIsSet("WMP_NODE")) {
        w.findChild<LibraryPage *>()->selectNode(qEnvironmentVariableIntValue("WMP_NODE"));
    }
    if (qEnvironmentVariableIsSet("WMP_TAB")) {
        w.findChild<LibraryPage *>()->openTab(qEnvironmentVariableIntValue("WMP_TAB"));
    }
    if (qEnvironmentVariable("WMP_TEST") == QLatin1String("eq")) {
        QTimer::singleShot(800, &w, [&w] {
            QMetaObject::invokeMethod(&w, "showEqSlot");
        });
        QTimer::singleShot(3000, &w, [] {
            for (QWidget *t : QApplication::topLevelWidgets()) {
                if (qobject_cast<EqWindow *>(t) && t->isVisible()) {
                    t->grab().save(qEnvironmentVariable("WIN7_GRAB"));
                }
            }
            qApp->quit();
        });
        return app.exec();
    }
    if (qEnvironmentVariable("WMP_TEST") == QLatin1String("skin")) {
        QTimer::singleShot(500, &w, [&w] {
            w.findChild<QAction *>(QString(), Qt::FindChildrenRecursively);
            for (QAction *a : w.actions()) {
                if (a->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_2)) {
                    a->trigger();
                }
            }
        });
        QTimer::singleShot(3500, &w, [] {
            for (QWidget *t : QApplication::topLevelWidgets()) {
                if ((qobject_cast<SkinWindow *>(t) || qobject_cast<CorporateWindow *>(t) || qobject_cast<EqWindow *>(t)) && t->isVisible()) {
                    t->grab().save(qEnvironmentVariable("WIN7_GRAB"));
                }
            }
            qApp->quit();
        });
        return app.exec();
    }
    if (qEnvironmentVariable("WMP_TEST") == QLatin1String("menu")) {
        w.menuBar()->setVisible(true);
    }
    if (qEnvironmentVariable("WMP_TEST") == QLatin1String("options")) {
        auto *d = new OptionsDialog(&w);
        d->show();
        QTimer::singleShot(2000, d, [d] {
            d->grab().save(qEnvironmentVariable("WIN7_GRAB"));
            qApp->quit();
        });
        return app.exec();
    }
    if (qEnvironmentVariableIsSet("WIN7_GRAB")) {
        const int wait = qEnvironmentVariableIsSet("WIN7_GRAB_WAIT") ? qEnvironmentVariableIntValue("WIN7_GRAB_WAIT") : 2500;
        QTimer::singleShot(wait, &w, [&w] {
            w.grab().save(qEnvironmentVariable("WIN7_GRAB"));
            qApp->quit();
        });
    }
    return app.exec();
}

#include "main.moc"

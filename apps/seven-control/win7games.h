/*
 * The Windows 7 "Games" folder (Games Explorer, gameux.dll): the games as tiles with their publisher, a preview
 * pane with the game's picture and its Windows Experience Index requirements against this computer's score,
 * and the details bar at the bottom.
 *
 *   Microsoft games -> ~/.local/share/seven-control/games.json (from each game's __GDF_XML resource:
 *                      publisher, developer, genre, WindowsSystemPerformanceRating), launched by their
 *                      seven_*.desktop entries
 *   other games     -> desktop entries in the Game category (Steam games...), tools left out
 * "Game Providers" (More Games from Microsoft, an online service that is gone) is left out.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "win7devices.h"
#include "win7perf.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Win7Games
{
enum { GamesView = 51 };

inline bool isGamesView(int v)
{
    return v == GamesView;
}

struct Game {
    QString desktopId; // launched with gtk-launch
    QString name;
    QString description;
    QString publisher;
    QString developer;
    QString genre;
    QString icon;
    double minRating = 0;
    double recRating = 0;
};

/** desktop entries of the Game category that are tools or launchers, not games */
inline bool isTool(const QString &id, const QString &name)
{
    static const QStringList ids{QStringLiteral("steam"), QStringLiteral("rtb-toolbar"), QStringLiteral("content-manager")};
    static const QStringList names{QStringLiteral("Blender"), QStringLiteral("Wallpaper Engine"), QStringLiteral("Race Track Builder"),
                                   QStringLiteral("Content Manager"), QStringLiteral("Steam")};
    return ids.contains(id) || names.contains(name) || id.startsWith(QLatin1String("seven_"));
}

inline QList<Game> games()
{
    QList<Game> list;
    QFile f(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/seven-control/games.json"));
    if (f.open(QIODevice::ReadOnly)) {
        for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array()) {
            const QJsonObject o = v.toObject();
            const QString id = o.value(QStringLiteral("desktop")).toString();
            if (QStandardPaths::locate(QStandardPaths::ApplicationsLocation, id + QStringLiteral(".desktop")).isEmpty()) {
                continue; // not installed
            }
            list.append({id, o.value(QStringLiteral("name")).toString(), o.value(QStringLiteral("description")).toString(),
                         o.value(QStringLiteral("publisher")).toString(), o.value(QStringLiteral("developer")).toString(),
                         o.value(QStringLiteral("genre")).toString(), o.value(QStringLiteral("icon")).toString(),
                         o.value(QStringLiteral("min")).toDouble(), o.value(QStringLiteral("rec")).toDouble()});
        }
    }
    // other games: the Game category of the menu (first file of an id wins, like the menu does)
    QSet<QString> seen;
    QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    dirs << QStringLiteral("/var/lib/snapd/desktop/applications");
    for (const QString &dir : std::as_const(dirs)) {
        for (const QString &file : QDir(dir).entryList({QStringLiteral("*.desktop")}, QDir::Files)) {
            const QString id = file.chopped(8);
            if (seen.contains(id)) {
                continue;
            }
            seen.insert(id);
            QSettings d(dir + QLatin1Char('/') + file, QSettings::IniFormat);
            d.beginGroup(QStringLiteral("Desktop Entry"));
            const QStringList cats = d.value(QStringLiteral("Categories")).toString().split(QLatin1Char(';'), Qt::SkipEmptyParts);
            const QString name = d.value(QStringLiteral("Name[uk]"), d.value(QStringLiteral("Name"))).toString();
            if (!cats.contains(QLatin1String("Game")) || d.value(QStringLiteral("NoDisplay")).toString() == QLatin1String("true") || isTool(id, name)) {
                continue;
            }
            const bool steam = d.value(QStringLiteral("Exec")).toString().contains(QLatin1String("steam://"));
            list.append({id, name, d.value(QStringLiteral("Comment")).toString(), steam ? QStringLiteral("Steam") : QString(), QString(), QString(),
                         d.value(QStringLiteral("Icon")).toString(), 0, 0});
        }
    }
    std::sort(list.begin(), list.end(), [](const Game &a, const Game &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return list;
}

/** this computer's base score (the lowest subscore), 0 when it has not been rated */
inline double baseScore()
{
    QSettings &s = Win7Perf::store();
    if (!s.contains(QStringLiteral("LastUpdate"))) {
        return 0;
    }
    double base = 7.9;
    for (const char *k : {"Processor", "Memory", "Graphics", "Gaming", "Disk"}) {
        base = qMin(base, s.value(QStringLiteral("Scores/") + QLatin1String(k)).toDouble());
    }
    return base;
}

inline QIcon gameIcon(const Game &g)
{
    QIcon i = g.icon.startsWith(QLatin1Char('/')) ? QIcon(g.icon) : QIcon::fromTheme(g.icon);
    return i.isNull() ? QIcon::fromTheme(QStringLiteral("seven-games-unknown")) : i;
}

/** a game in the Tiles view: 48 px icon, name and publisher */
class GameTile : public QWidget
{
public:
    GameTile(const Game &game, QWidget *parent)
        : QWidget(parent)
        , g(game)
    {
        setAttribute(Qt::WA_Hover);
        setFixedSize(236, 54);
        m_icon = gameIcon(g).pixmap(48, 48);
    }
    Game g;
    bool selected = false;
    std::function<void(GameTile *)> onClick, onDoubleClick;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        Win7Dev::paintSelection(p, QRectF(0.5, 0.5, width() - 1, height() - 1), selected, underMouse(), window()->isActiveWindow());
        p.drawPixmap(3, (height() - 48) / 2, m_icon);
        const QFontMetrics fm(font());
        const int x = 58, w = width() - x - 4;
        const int top = (height() - 2 * fm.height()) / 2;
        p.setPen(Qt::black);
        p.drawText(QRect(x, top, w, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(g.name, Qt::ElideRight, w));
        p.setPen(QColor(0x6d, 0x6d, 0x6d));
        p.drawText(QRect(x, top + fm.height(), w, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(g.publisher, Qt::ElideRight, w));
    }
    void mousePressEvent(QMouseEvent *) override
    {
        if (onClick) {
            onClick(this);
        }
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && onDoubleClick) {
            onDoubleClick(this);
        }
    }

private:
    QPixmap m_icon;
};

/** right side: the game's picture, then the "Performance" tab with the rating requirements */
class PreviewPane : public QWidget
{
public:
    explicit PreviewPane(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedWidth(272);
        setAutoFillBackground(false);
    }
    void showGame(const Game *g)
    {
        m_game = g ? std::make_unique<Game>(*g) : nullptr;
        m_picture = (g ? gameIcon(*g) : QIcon::fromTheme(QStringLiteral("seven-games"))).pixmap(256, 256);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(rect(), Qt::white);
        p.setPen(QColor(0xd5, 0xdf, 0xe5));
        p.drawLine(0, 0, 0, height());
        // the picture
        const int side = 176;
        p.drawPixmap(QRect((width() - side) / 2, 28, side, side), m_picture);
        // the tab with the performance icon
        const int top = 228;
        const QRect tab(12, top, 46, 26), box(12, top + 25, width() - 24, 200);
        p.setPen(QColor(0xd9, 0xd9, 0xd9));
        p.setBrush(QColor(0xfc, 0xfc, 0xfc));
        p.drawRect(box);
        p.setBrush(Qt::white);
        p.drawRect(tab);
        p.setPen(Qt::white);
        p.drawLine(tab.left() + 1, tab.bottom(), tab.right() - 1, tab.bottom());
        p.drawPixmap(tab.left() + 15, tab.top() + 5, QIcon::fromTheme(QStringLiteral("seven-games-perf")).pixmap(16, 16));
        // its text
        const double score = baseScore();
        const QLocale uk(QLocale::Ukrainian);
        QFont f = font();
        p.setFont(f);
        const QRect text = box.adjusted(12, 12, -12, -12);
        p.setPen(Qt::black);
        if (score <= 0) {
            p.drawText(text, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                       QStringLiteral("Відомості про продуктивність цього комп'ютера ще не створено."));
            m_rateLink = QRect(text.left(), text.top() + 48, text.width(), 18);
            p.setPen(QColor(0x00, 0x66, 0xcc));
            p.drawText(m_rateLink, Qt::AlignHCenter, QStringLiteral("Оцінити цей комп'ютер"));
            return;
        }
        m_rateLink = QRect();
        auto badge = [&](int y, double v) {
            p.drawPixmap(text.right() - 32, y - 2, Win7Perf::badgePixmap(v, false));
        };
        int y = text.top();
        if (m_game && m_game->recRating > 0) {
            const bool ok = score >= m_game->recRating;
            p.drawText(QRect(text.left(), y, text.width() - 40, 40), Qt::TextWordWrap,
                       ok ? QStringLiteral("Цей комп'ютер відповідає рекомендованим вимогам гри.")
                          : score >= m_game->minRating ? QStringLiteral("Цей комп'ютер відповідає мінімальним вимогам гри.")
                                                        : QStringLiteral("Цей комп'ютер не відповідає мінімальним вимогам гри."));
            y += 50;
            for (const auto &[label, v] : {std::pair{QStringLiteral("Необхідна оцінка:"), m_game->minRating},
                                           std::pair{QStringLiteral("Рекомендована оцінка:"), m_game->recRating},
                                           std::pair{QStringLiteral("Оцінка вашої системи:"), score}}) {
                p.drawText(QRect(text.left(), y, text.width() - 40, 20), Qt::AlignLeft | Qt::AlignVCenter, label);
                badge(y, v);
                y += 30;
            }
        } else {
            p.drawText(QRect(text.left(), y, text.width() - 40, 40), Qt::TextWordWrap, QStringLiteral("Індекс продуктивності Windows цього комп'ютера:"));
            badge(y + 4, score);
            y += 50;
            if (m_game) {
                p.setPen(QColor(0x6d, 0x6d, 0x6d));
                p.drawText(QRect(text.left(), y, text.width(), 60), Qt::TextWordWrap,
                           QStringLiteral("Гра не повідомляє своїх вимог до продуктивності."));
            }
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (m_rateLink.contains(e->pos()) && Win7Perf::rate(window())) {
            update();
        }
    }

private:
    std::unique_ptr<Game> m_game;
    QPixmap m_picture;
    QRect m_rateLink;
};

/** the details bar at the bottom: the item count, or the selected game's properties */
class DetailsBar : public QWidget
{
public:
    explicit DetailsBar(int count, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_count(count)
    {
        setFixedHeight(58);
    }
    void showGame(const Game *g)
    {
        m_game = g ? std::make_unique<Game>(*g) : nullptr;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QLinearGradient bg(0, 0, 0, height());
        bg.setColorAt(0, QColor(0xf1, 0xf5, 0xfb));
        bg.setColorAt(1, QColor(0xe4, 0xec, 0xf7));
        p.fillRect(rect(), bg);
        p.setPen(QColor(0xc5, 0xd2, 0xe6));
        p.drawLine(0, 0, width(), 0);
        const QIcon icon = m_game ? gameIcon(*m_game) : QIcon::fromTheme(QStringLiteral("seven-games"));
        p.drawPixmap(14, (height() - 40) / 2, icon.pixmap(40, 40));
        const QFontMetrics fm(font());
        p.setPen(Qt::black);
        if (!m_game) {
            p.drawText(QRect(68, 0, width() - 80, height()), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Елементів: %1").arg(m_count));
            return;
        }
        QFont b = font();
        b.setPixelSize(b.pixelSize() > 0 ? b.pixelSize() + 3 : 15);
        p.setFont(b);
        p.drawText(QRect(68, 6, 300, 22), Qt::AlignLeft | Qt::AlignVCenter, m_game->name);
        p.setFont(font());
        const QList<std::pair<QString, QString>> rows{{QStringLiteral("Видавець:"), m_game->publisher},
                                                       {QStringLiteral("Розробник:"), m_game->developer},
                                                       {QStringLiteral("Жанр:"), m_game->genre}};
        int x = 68, y = 30;
        for (const auto &[label, value] : rows) {
            if (value.isEmpty()) {
                continue;
            }
            p.setPen(QColor(0x5a, 0x67, 0x79));
            p.drawText(x, y + fm.ascent(), label);
            const int lx = x + fm.horizontalAdvance(label) + 6;
            p.setPen(Qt::black);
            p.drawText(lx, y + fm.ascent(), value);
            x = lx + fm.horizontalAdvance(value) + 24;
        }
    }

private:
    int m_count;
    std::unique_ptr<Game> m_game;
};

inline QWidget *buildGamesPage()
{
    auto *root = new QWidget;
    root->setAutoFillBackground(true);
    QPalette pal = root->palette();
    pal.setColor(QPalette::Window, Qt::white);
    root->setPalette(pal);
    auto *outer = new QVBoxLayout(root);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    outer->addLayout(row, 1);

    const QList<Game> list = games();
    auto *left = new QWidget;
    auto *lv = new QVBoxLayout(left);
    lv->setContentsMargins(12, 10, 12, 10);
    lv->setSpacing(4);
    auto *header = new Win7Dev::GroupHeader(QStringLiteral("Ігри (%1)").arg(list.size()), left);
    lv->addWidget(header);
    auto *gridHost = new QWidget;
    auto *grid = new QGridLayout(gridHost);
    grid->setContentsMargins(6, 4, 0, 0);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(2);
    lv->addWidget(gridHost);
    lv->addStretch(1);
    row->addWidget(left, 1);
    auto *preview = new PreviewPane;
    row->addWidget(preview);
    auto *details = new DetailsBar(list.size());
    outer->addWidget(details);
    preview->showGame(nullptr);

    header->toggled = [header, gridHost] {
        gridHost->setVisible(!header->collapsed);
    };
    auto tiles = std::make_shared<QList<GameTile *>>();
    const int columns = 3;
    for (int i = 0; i < list.size(); ++i) {
        auto *t = new GameTile(list.at(i), gridHost);
        grid->addWidget(t, i / columns, i % columns, Qt::AlignLeft | Qt::AlignTop);
        tiles->append(t);
        t->onClick = [tiles, preview, details](GameTile *t) {
            for (GameTile *o : std::as_const(*tiles)) {
                o->selected = o == t;
                o->update();
            }
            preview->showGame(&t->g);
            details->showGame(&t->g);
        };
        t->onDoubleClick = [](GameTile *t) {
            QProcess::startDetached(QStringLiteral("gtk-launch"), {t->g.desktopId});
        };
    }
    grid->setColumnStretch(columns, 1);
    return root;
}
} // namespace Win7Games

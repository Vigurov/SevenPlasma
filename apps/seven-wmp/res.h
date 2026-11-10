#pragma once
/*
 * Pictures of Windows Media Player 12 (wmploc.dll, extracted to ~/.local/share/seven-wmp/<id>.png and
 * ~/.local/share/seven-wmp/ico/<group>_<size>.png).
 */
#include <QDir>
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

namespace Res
{
inline QString dir()
{
    return QDir::homePath() + QStringLiteral("/.local/share/seven-wmp/");
}

inline const QPixmap &px(const QString &name)
{
    static QHash<QString, QPixmap> cache;
    auto it = cache.find(name);
    if (it == cache.end()) {
        it = cache.insert(name, QPixmap(dir() + name + QStringLiteral(".png")));
    }
    return *it;
}
inline const QPixmap &px(int id)
{
    return px(QString::number(id));
}

/** a small icon from an icon group of wmploc.dll */
inline QIcon icon(int group)
{
    QIcon i;
    for (int s : {16, 24, 32, 48, 256}) {
        const QString f = dir() + QStringLiteral("ico/%1_%2.png").arg(group).arg(s);
        if (QFile::exists(f)) {
            i.addFile(f, QSize(s, s));
        }
    }
    return i;
}

/** draws a picture stretched horizontally, keeping `l`/`r` pixels of its ends */
inline void hslice(QPainter &p, const QRect &target, const QPixmap &pm, int l, int r)
{
    if (pm.isNull() || target.width() <= 0) {
        return;
    }
    const int h = pm.height();
    const int y = target.y() + (target.height() - h) / 2;
    l = qMin(l, target.width() / 2);
    r = qMin(r, target.width() - l);
    p.drawPixmap(QRect(target.x(), y, l, h), pm, QRect(0, 0, l, h));
    p.drawPixmap(QRect(target.right() - r + 1, y, r, h), pm, QRect(pm.width() - r, 0, r, h));
    const int mid = target.width() - l - r;
    if (mid > 0) {
        p.drawPixmap(QRect(target.x() + l, y, mid, h), pm, QRect(l, 0, pm.width() - l - r, h));
    }
}
}

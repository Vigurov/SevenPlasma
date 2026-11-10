#!/usr/bin/env python3
"""apps/seven-folderthumb: read the Windows folder pictures from <data>/sevenplasma/folderthumb/ instead of
compiling them in (res.qrc holds Windows 7's imageres pictures, which SevenPlasma does not ship).
Idempotent; run by tools/sync-from-dev.sh after every refresh."""
import os, sys
d = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'apps', 'seven-folderthumb')
p = os.path.join(d, 'compose.cpp'); s = open(p).read()
if 'static QString asset(' not in s:
    s = s.replace('namespace Win7Folder\n{\n', '''namespace Win7Folder
{

/* The folder pictures are Windows 7's own (imageres.dll), so they are not part of the source: install.sh extracts
   them from the user's Windows 7 ISO into <data>/sevenplasma/folderthumb/ (see ASSETS.txt for the list). */
static QString asset(const QString &name)
{
    static QHash<QString, QString> cache;
    auto it = cache.constFind(name);
    if (it != cache.constEnd()) {
        return *it;
    }
    const QString path = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("sevenplasma/folderthumb/") + name);
    cache.insert(name, path);
    return path;
}

''', 1)
    s = s.replace('QImage img(QStringLiteral(":/win7folder/%1-%2.png").arg(name).arg(s));', 'QImage img(asset(QStringLiteral("%1-%2.png").arg(name).arg(s)));')
    s = s.replace('QImage(QStringLiteral(":/win7folder/page-text.png"))', 'QImage(asset(QStringLiteral("page-text.png")))')
    s = s.replace('QImage(QStringLiteral(":/win7folder/folder-folders.png"))', 'QImage(asset(QStringLiteral("folder-folders.png")))')
    s = s.replace('#include <QFileInfo>\n', '#include <QFileInfo>\n#include <QHash>\n', 1)
    assert ':/win7folder' not in s, 'compose.cpp: unknown :/win7folder use'
    open(p, 'w').write(s)
p = os.path.join(d, 'CMakeLists.txt'); s = open(p).read()
s = s.replace('set(CMAKE_AUTORCC ON)\n', '').replace(' res.qrc', '').replace('win7folder-preview', 'seven-folder-preview')
open(p, 'w').write(s)
for f in ('res.qrc',):
    if os.path.exists(os.path.join(d, f)):
        os.remove(os.path.join(d, f))
print('folderthumb: pictures from <data>/sevenplasma/folderthumb')

#!/bin/bash
# One-time conversion of seven-control to English code + i18n/uk.ts (kept for re-runs from a clean copy).
set -e
cd "$(dirname "$0")/../../seven-control"
python3 ../seven-common/i18n/i18n.py apply . i18n/strings.json
# the startup sound's autostart file is data, not text
python3 - <<'PY'
p='win7sound.h'; s=open(p,encoding='utf-8').read()
s=s.replace('f.write(tr7("[Desktop Entry]\\nType=Application\\nName=Windows startup sound\\nName[uk]=','f.write(QStringLiteral("[Desktop Entry]\\nType=Application\\nName=Windows startup sound\\nName[uk]=',1)
open(p,'w',encoding='utf-8').write(s)
PY
# tables: their strings are translated where they are shown
sed -i 's|offsetText(off) + QLatin1Char('"' '"') + QString::fromUtf8(e.second)}});|offsetText(off) + QLatin1Char('"' '"') + tr7(e.second)}});|; s|// Windows time zone id -> the cities Windows 7 shows (Ukrainian)|// Windows time zone id -> the cities Windows 7 shows (translated with tr7)|' win7datetime.h
sed -i 's|node(libs, QString::fromUtf8(l.name),|node(libs, tr7(l.name),|' win7browsefolder.h
sed -i 's|        t.name = QString::fromUtf8(d.name);|        t.name = tr7(d.name);|' win7display.h
sed -i 's|            f.scripts << QString::fromUtf8(si.name);|            f.scripts << tr7(si.name);|' win7fontdata.h
sed -i 's|QString::fromUtf8(units\[u\])|tr7(units[u])|; s|.arg(QString::fromUtf8(months\[d.month() - 1\]))|.arg(tr7(months[d.month() - 1]))|' win7properties.h
sed -i 's|auto \*btn = win7Button(QString::fromUtf8(b));|auto *btn = win7Button(tr7(b));|' win7network.h
sed -i 's|setToolTip(QString::fromUtf8(presets().at(i - 1).name));|setToolTip(tr7(presets().at(i - 1).name));|; s|m_item->addItem(QString::fromUtf8(it));|m_item->addItem(tr7(it));|; s|? QString::fromUtf8(presets().at(m_now.accent - 1).name) :|? tr7(presets().at(m_now.accent - 1).name) :|' win7windowcolor.h
sed -i 's|m_saver->addItem(QString::fromUtf8(k.name), QLatin1String(k.id));|m_saver->addItem(tr7(k.name), QLatin1String(k.id));|' win7screensaver.h
# numbers, dates and sorting follow the user's locale, not Ukrainian
sed -i 's/QLocale(QLocale::Ukrainian, QLocale::Ukraine)/QLocale{}/g; s/QLocale(QLocale::Ukrainian)/QLocale{}/g; s/const QLocale uk(QLocale::Ukrainian);/const QLocale uk;/g' *.h *.cpp
# build: the translations go into the program
python3 - <<'PY'
p='CMakeLists.txt'; s=open(p).read()
if 'LinguistTools' not in s:
    s=s.replace('find_package(Qt6 REQUIRED COMPONENTS Widgets Network DBus Svg PrintSupport UiPlugin)','find_package(Qt6 REQUIRED COMPONENTS Widgets Network DBus Svg PrintSupport UiPlugin LinguistTools)')
    s+='\n# translations: the code is English, i18n/<lang>.ts are built into the program as :/i18n/<lang>.qm\nqt_add_translations(seven-control TS_FILES i18n/uk.ts RESOURCE_PREFIX /i18n LUPDATE_TARGET seven-control_lupdate_unused)\n'
    open(p,'w').write(s)
p='main.cpp'; s=open(p,encoding='utf-8').read()
if 'Win7I18n::install' not in s:
    s=s.replace('    QApplication app(argc, argv);\n','    QApplication app(argc, argv);\n    Win7I18n::install(&app);\n',1)
    open(p,'w',encoding='utf-8').write(s)
PY
python3 ../seven-common/i18n/i18n.py check . i18n/strings.json | tail -1

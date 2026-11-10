#!/bin/bash
# Convert one program to English code + i18n/uk.ts:  convert-app.sh seven-calc
# (apply the translations of i18n/strings.json, build the .ts into the program, install the translator in main())
set -e
APP=$1
cd "$(dirname "$0")/../../$APP"
TARGET=$(grep -oP 'add_executable\(\s*\K[^ )]+' CMakeLists.txt | head -1)
python3 ../seven-common/i18n/i18n.py apply . i18n/strings.json > i18n/apply.log; grep -v '^plain literals' i18n/apply.log || true; grep -o '[a-z0-9_]*\.[hcp]*:[0-9]*' i18n/apply.log | tr '\n' ' ' | sed 's/^./tables: &/'; echo
sed -i 's/QLocale(QLocale::Ukrainian, QLocale::Ukraine)/QLocale{}/g; s/QLocale(QLocale::Ukrainian)/QLocale{}/g; s/const QLocale uk(QLocale::Ukrainian);/const QLocale uk;/g' *.h *.cpp 2>/dev/null || true
python3 - "$TARGET" <<'PY'
import re, sys, glob
target = sys.argv[1]
p = 'CMakeLists.txt'; s = open(p).read()
if 'LinguistTools' not in s:
    s = re.sub(r'(find_package\(Qt6 REQUIRED COMPONENTS [^)]*)\)', r'\1 LinguistTools)', s, count=1)
if 'seven-common' not in s:
    s += '\ntarget_include_directories(%s PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../seven-common)\n' % target
if 'qt_add_translations' not in s:
    s += ('\n# translations: the code is English, i18n/<lang>.ts are built into the program as :/i18n/<lang>.qm\n'
          'qt_add_translations(%s TS_FILES i18n/uk.ts RESOURCE_PREFIX /i18n LUPDATE_TARGET %s_lupdate_unused)\n' % (target, target))
open(p, 'w').write(s)
for f in glob.glob('*.cpp'):
    s = open(f, encoding='utf-8').read()
    if 'Win7I18n::install' in s:
        continue
    m = re.search(r'\n(\s*)Q(?:Core)?Application (\w+)\(argc, argv\);\n', s)
    if m:
        s = s[:m.end()] + '%sWin7I18n::install(&%s);\n' % (m.group(1), m.group(2)) + s[m.end():]
        if 'win7i18n.h' not in s:
            s = '#include "win7i18n.h"\n' + s
        open(f, 'w', encoding='utf-8').write(s)
        print('translator installed in', f)
PY
python3 ../seven-common/i18n/i18n.py check . i18n/strings.json | tail -1

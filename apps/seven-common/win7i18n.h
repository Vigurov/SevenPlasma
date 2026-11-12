#pragma once
/*
 * Translations of our programs: the code is in English, other languages are Qt translation files
 * (<app>/i18n/<lang>.ts, built into the program as :/i18n/<lang>.qm). The language is the user's UI language
 * (LANGUAGE, else the system locale), like the display language of Windows 7.
 *
 *   tr7("Control Panel")                    -> translated QString
 *   tr7("Open", "verb in a menu")           -> the note tells two meanings of the same English text apart
 *   Win7I18n::install(app)                  -> in main(), before any window is made
 */
#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QTranslator>

namespace Win7I18n
{
inline QString tr7(const char *en, const char *note = nullptr)
{
    return QCoreApplication::translate("Win7", en, note);
}

/** the UI languages in order of preference: LANGUAGE (uk:en), then the locale (uk_UA) */
inline QStringList languages()
{
    QStringList out;
    for (const QString &l : qEnvironmentVariable("LANGUAGE").split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        out << l;
    }
    out << QLocale::system().uiLanguages();
    return out;
}

/** the [xx] suffixes of localized keys to try before the plain one (Name[uk_UA], Name[uk] for Ukrainian; none for
 *  English, whose text is the plain key) */
inline QStringList keySuffixes()
{
    QStringList out;
    for (QString l : languages()) {
        l.replace(QLatin1Char('-'), QLatin1Char('_'));
        if (l.startsWith(QLatin1String("en")) || l == QLatin1String("C")) {
            break;
        }
        const QString full = l.section(QLatin1Char('.'), 0, 0);
        for (const QString &x : {full, full.section(QLatin1Char('_'), 0, 0)}) {
            if (!out.contains(x)) {
                out << x;
            }
        }
    }
    return out;
}

/** key in the UI language from anything with value(QString) (QSettings, QJsonObject...) */
template<class Map>
inline QString localizedValue(const Map &m, const QString &key)
{
    for (const QString &x : keySuffixes()) {
        const QString v = m.value(key + QLatin1Char('[') + x + QLatin1Char(']')).toString();
        if (!v.isEmpty()) {
            return v;
        }
    }
    return m.value(key).toString();
}

/** :/i18n/<lang>.qm and the files of parts built into this program (:/i18n/<part>_<lang>.qm) */
inline void install(QCoreApplication *app)
{
    for (QString l : languages()) {
        l.replace(QLatin1Char('-'), QLatin1Char('_'));
        if (l.startsWith(QLatin1String("en")) || l == QLatin1String("C")) {
            return; // English: the code itself
        }
        const QString lang = l.section(QLatin1Char('_'), 0, 0);
        bool found = false;
        const QStringList files = QDir(QStringLiteral(":/i18n")).entryList({lang + QStringLiteral(".qm"), QStringLiteral("*_") + lang + QStringLiteral(".qm")});
        for (const QString &f : files) {
            auto *t = new QTranslator(app);
            if (t->load(QStringLiteral(":/i18n/") + f)) {
                app->installTranslator(t);
                found = true;
            } else {
                delete t;
            }
        }
        if (found) {
            return;
        }
    }
}
}

using Win7I18n::tr7;

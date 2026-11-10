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

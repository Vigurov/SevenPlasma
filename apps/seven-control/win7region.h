/*
 * "Region and Language" of Windows 7 with "Customize Format", "Text Services and Input Languages",
 * "Add Input Language", "Advanced Key Settings", the welcome screen / new user settings and the
 * system language window.
 *
 *   Formats            -> Plasma's formats locale (plasma-localerc) and the taskbar clock's formats
 *   Keyboards          -> kxkbrc (layouts, switching keys), the language bar widget on the taskbar
 *   Welcome screen etc -> localectl (system locale and keyboard), asks for the password
 * Windows' "Location" tab and the number/currency overrides have no counterpart on Linux and are left out.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7datetime.h"

#include <QCollator>
#include <QFile>
#include <QHeaderView>
#include <QRadioButton>
#include <QSettings>
#include <QTreeWidget>
#include <QXmlStreamReader>
#include <QtEndian>

namespace Win7Region
{
using Win7Time::fixedDialog;
using Win7Time::run;
using Win7Time::shieldButton;

/* ---------------- names in Ukrainian (iso-codes and xkeyboard-config translations) ---------------- */

/** Reads a gettext .mo catalogue */
inline QHash<QString, QString> readMo(const QString &path)
{
    QHash<QString, QString> map;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return map;
    }
    const QByteArray d = f.readAll();
    if (d.size() < 28) {
        return map;
    }
    const bool le = qFromLittleEndian<quint32>(d.constData()) == 0x950412de;
    auto u32 = [&](int off) -> quint32 {
        return le ? qFromLittleEndian<quint32>(d.constData() + off) : qFromBigEndian<quint32>(d.constData() + off);
    };
    const quint32 n = u32(8), orig = u32(12), trans = u32(16);
    for (quint32 i = 0; i < n; ++i) {
        const quint32 ol = u32(orig + i * 8), oo = u32(orig + i * 8 + 4);
        const quint32 tl = u32(trans + i * 8), to = u32(trans + i * 8 + 4);
        if (oo + ol > quint32(d.size()) || to + tl > quint32(d.size())) {
            continue;
        }
        QByteArray key = d.mid(oo, ol);
        const int ctx = key.indexOf('\x04');
        if (ctx >= 0) {
            key = key.mid(ctx + 1);
        }
        map.insert(QString::fromUtf8(key), QString::fromUtf8(d.mid(to, tl)).section(QLatin1Char('\0'), 0, 0));
    }
    return map;
}

inline QString moPath(const QString &domain)
{
    for (const QString &base : {QStringLiteral("/usr/share/locale-langpack/uk/LC_MESSAGES/"), QStringLiteral("/usr/share/locale/uk/LC_MESSAGES/")}) {
        if (QFile::exists(base + domain + QStringLiteral(".mo"))) {
            return base + domain + QStringLiteral(".mo");
        }
    }
    return {};
}

inline QString capital(QString s)
{
    if (!s.isEmpty()) {
        s[0] = s.at(0).toUpper();
    }
    return s;
}

/** "uk" / "ukr" -> "українська" */
inline QString languageName(const QString &code)
{
    static QHash<QString, QString> names;
    if (names.isEmpty()) {
        const QHash<QString, QString> tr = readMo(moPath(QStringLiteral("iso_639-3")));
        QFile f(QStringLiteral("/usr/share/iso-codes/json/iso_639-3.json"));
        if (f.open(QIODevice::ReadOnly)) {
            for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("639-3")).toArray()) {
                const QJsonObject o = v.toObject();
                const QString en = o.value(QLatin1String("name")).toString();
                const QString name = tr.value(en, en);
                names.insert(o.value(QLatin1String("alpha_3")).toString(), name);
                if (o.contains(QLatin1String("alpha_2"))) {
                    names.insert(o.value(QLatin1String("alpha_2")).toString(), name);
                }
            }
        }
    }
    return names.value(code, code);
}

/** "UA" -> "Україна" */
inline QString territoryName(const QString &code)
{
    static QHash<QString, QString> names;
    if (names.isEmpty()) {
        const QHash<QString, QString> tr = readMo(moPath(QStringLiteral("iso_3166-1")));
        QFile f(QStringLiteral("/usr/share/iso-codes/json/iso_3166-1.json"));
        if (f.open(QIODevice::ReadOnly)) {
            for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("3166-1")).toArray()) {
                const QJsonObject o = v.toObject();
                const QString en = o.value(QLatin1String("common_name")).toString(o.value(QLatin1String("name")).toString());
                names.insert(o.value(QLatin1String("alpha_2")).toString(), tr.value(en, tr.value(o.value(QLatin1String("name")).toString(), en)));
            }
        }
        names.insert(QStringLiteral("US"), tr7("US"));
        names.insert(QStringLiteral("GB"), tr7("United Kingdom"));
    }
    return names.value(code, code);
}

/** "uk_UA.UTF-8" -> "Українська (Україна)" */
inline QString localeName(const QString &locale)
{
    const QString base = locale.section(QLatin1Char('.'), 0, 0).section(QLatin1Char('@'), 0, 0);
    const QString lang = base.section(QLatin1Char('_'), 0, 0);
    const QString terr = base.section(QLatin1Char('_'), 1, 1);
    if (lang == QLatin1String("C") || lang == QLatin1String("POSIX")) {
        return tr7("Standard (C)");
    }
    return terr.isEmpty() ? capital(languageName(lang)) : QStringLiteral("%1 (%2)").arg(capital(languageName(lang)), territoryName(terr));
}

/** UTF-8 locales glibc can provide, "xx_YY.UTF-8" */
inline QStringList supportedLocales()
{
    QStringList list;
    QFile f(QStringLiteral("/usr/share/i18n/SUPPORTED"));
    if (f.open(QIODevice::ReadOnly)) {
        for (const QByteArray &line : f.readAll().split('\n')) {
            const QString l = QString::fromUtf8(line).trimmed();
            if (l.endsWith(QLatin1String(" UTF-8")) && !l.contains(QLatin1Char('@'))) {
                const QString name = l.section(QLatin1Char(' '), 0, 0);
                if (name.contains(QLatin1Char('_')) && !list.contains(name)) {
                    list << name;
                }
            }
        }
    }
    return list;
}

inline void fillLocales(QComboBox *box, const QString &select)
{
    QList<QPair<QString, QString>> items;
    for (const QString &l : supportedLocales()) {
        items.append({localeName(l), l});
    }
    QCollator c(QLocale{});
    std::sort(items.begin(), items.end(), [&c](const auto &a, const auto &b) {
        return c.compare(a.first, b.first) < 0;
    });
    for (const auto &it : std::as_const(items)) {
        box->addItem(it.first, it.second);
    }
    const QString want = select.section(QLatin1Char('.'), 0, 0);
    for (int i = 0; i < box->count(); ++i) {
        if (box->itemData(i).toString().section(QLatin1Char('.'), 0, 0) == want) {
            box->setCurrentIndex(i);
        }
    }
    box->setMaxVisibleItems(30);
}

/** glibc only knows generated locales; generate the chosen one first (asks for the password) */
inline bool ensureLocale(const QString &locale)
{
    const QString want = locale.section(QLatin1Char('.'), 0, 0).toLower() + QStringLiteral(".utf8");
    if (run(QStringLiteral("locale"), {QStringLiteral("-a")}).toLower().split(QLatin1Char('\n')).contains(want)) {
        return true;
    }
    int code = 0;
    run(QStringLiteral("pkexec"), {QStringLiteral("locale-gen"), locale}, &code);
    return code == 0;
}

/* ---------------- keyboard layouts (evdev.xml) ---------------- */

struct Layout {
    QString layout;  // "us"
    QString variant; // "" or "intl"
    QString description;
    QString language; // iso639 code
    QString country;  // iso3166 code
};

inline const QList<Layout> &allLayouts()
{
    static QList<Layout> list;
    if (!list.isEmpty()) {
        return list;
    }
    const QHash<QString, QString> tr = readMo(moPath(QStringLiteral("xkeyboard-config")));
    QFile f(QStringLiteral("/usr/share/X11/xkb/rules/evdev.xml"));
    if (!f.open(QIODevice::ReadOnly)) {
        return list;
    }
    QXmlStreamReader x(&f);
    QStringList path;
    Layout base, cur;
    bool inVariant = false;
    while (!x.atEnd()) {
        x.readNext();
        if (x.isStartElement()) {
            path << x.name().toString();
            if (x.name() == QLatin1String("layout")) {
                base = Layout();
                inVariant = false;
            } else if (x.name() == QLatin1String("variant")) {
                cur = base;
                cur.variant.clear();
                inVariant = true;
            } else if (path.contains(QLatin1String("layoutList")) && !path.contains(QLatin1String("optionList"))) {
                Layout &t = inVariant ? cur : base;
                const QString name = x.name().toString();
                if (name == QLatin1String("name") || name == QLatin1String("description") || name == QLatin1String("iso639Id") || name == QLatin1String("iso3166Id")) {
                    const QString text = x.readElementText();
                    path.removeLast();
                    if (name == QLatin1String("name")) {
                        (inVariant ? t.variant : t.layout) = text;
                    } else if (name == QLatin1String("description")) {
                        t.description = tr.value(text, text);
                    } else if (name == QLatin1String("iso639Id") && t.language.isEmpty()) {
                        t.language = text;
                    } else if (name == QLatin1String("iso3166Id") && t.country.isEmpty()) {
                        t.country = text;
                    }
                }
            }
        } else if (x.isEndElement()) {
            if (x.name() == QLatin1String("configItem") && path.contains(QLatin1String("layoutList"))) {
                if (path.size() >= 2 && path.at(path.size() - 2) == QLatin1String("layout")) {
                    list.append(base);
                } else if (inVariant && path.size() >= 2 && path.at(path.size() - 2) == QLatin1String("variant")) {
                    if (cur.language.isEmpty()) {
                        cur.language = base.language;
                    }
                    if (cur.country.isEmpty()) {
                        cur.country = base.country;
                    }
                    list.append(cur);
                }
            }
            if (!path.isEmpty()) {
                path.removeLast();
            }
        }
    }
    return list;
}

inline Layout findLayout(const QString &layout, const QString &variant)
{
    for (const Layout &l : allLayouts()) {
        if (l.layout == layout && l.variant == variant) {
            return l;
        }
    }
    return {layout, variant, layout, {}, {}};
}

/** "Українська (Україна)" for the input language of a layout */
inline QString inputLanguage(const Layout &l)
{
    const QString lang = l.language.isEmpty() ? l.layout : l.language;
    QString country = l.country;
    if (country.isEmpty() && l.layout.size() == 2) {
        country = l.layout.toUpper();
    }
    if (l.layout == QLatin1String("ua")) {
        country = QStringLiteral("UA");
    }
    return country.isEmpty() ? capital(languageName(lang)) : QStringLiteral("%1 (%2)").arg(capital(languageName(lang)), territoryName(country));
}

/** The keyboard name Windows shows under the language: "США", "Українська", "Українська (фонетична)" */
inline QString keyboardName(const Layout &l)
{
    if (l.layout == QLatin1String("us") && l.variant.isEmpty()) {
        return tr7("US");
    }
    return l.description;
}

struct KeyboardConfig {
    QStringList layouts, variants, names;
    QStringList options;
};

inline QString kxkbrc()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/kxkbrc");
}

inline KeyboardConfig readKeyboard()
{
    KeyboardConfig k;
    QSettings s(kxkbrc(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("Layout"));
    auto list = [&](const char *key) {
        const QVariant v = s.value(QLatin1String(key));
        return v.typeId() == QMetaType::QStringList ? v.toStringList() : v.toString().split(QLatin1Char(','));
    };
    k.layouts = list("LayoutList");
    k.layouts.removeAll(QString());
    k.variants = list("VariantList");
    k.names = list("DisplayNames");
    const QString opts = s.value(QStringLiteral("Options")).toStringList().join(QLatin1Char(','));
    if (s.contains(QStringLiteral("Options"))) {
        k.options = opts.split(QLatin1Char(','), Qt::SkipEmptyParts);
    } else { // the system default (localectl) is in effect
        const QString status = run(QStringLiteral("localectl"), {QStringLiteral("status")});
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("X11 Options:\\s*(\\S+)")).match(status);
        if (m.hasMatch()) {
            k.options = m.captured(1).split(QLatin1Char(','), Qt::SkipEmptyParts);
        }
    }
    while (k.variants.size() < k.layouts.size()) {
        k.variants << QString();
    }
    while (k.names.size() < k.layouts.size()) {
        k.names << QString();
    }
    return k;
}

inline void writeKeyboard(const KeyboardConfig &k)
{
    {
        QSettings s(kxkbrc(), QSettings::IniFormat);
        s.beginGroup(QStringLiteral("Layout"));
        s.setValue(QStringLiteral("LayoutList"), k.layouts.join(QLatin1Char(',')));
        s.setValue(QStringLiteral("VariantList"), k.variants.mid(0, k.layouts.size()).join(QLatin1Char(',')));
        s.setValue(QStringLiteral("DisplayNames"), k.names.mid(0, k.layouts.size()).join(QLatin1Char(',')));
        s.setValue(QStringLiteral("Options"), k.options.join(QLatin1Char(',')));
        s.setValue(QStringLiteral("ResetOldOptions"), true);
        s.setValue(QStringLiteral("Use"), true);
    }
    // the keyboard daemon re-reads kxkbrc on this signal (what System Settings sends)
    QDBusConnection::sessionBus().send(QDBusMessage::createSignal(QStringLiteral("/Layouts"), QStringLiteral("org.kde.keyboard"), QStringLiteral("reloadConfig")));
}

inline QString badge(const KeyboardConfig &k, int i)
{
    const QString n = k.names.value(i);
    return (n.isEmpty() ? k.layouts.value(i) : n).toUpper().left(3);
}

/** A small blue square with the language code, like the Windows 7 language list */
inline QIcon badgeIcon(const QString &code)
{
    QPixmap pm(20, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.fillRect(QRect(0, 1, 18, 14), QColor(0x3b, 0x8c, 0xd7));
    QFont f;
    f.setPixelSize(9);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRect(0, 1, 18, 14), Qt::AlignCenter, code.left(2));
    return QIcon(pm);
}

/* ---------------- Change Key Sequence ---------------- */

inline const QList<QPair<QString, QString>> &switchKeys()
{
    static const QList<QPair<QString, QString>> keys{{QString(), tr7("Not scheduled")},
                                                     {QStringLiteral("grp:ctrl_shift_toggle"), QStringLiteral("CTRL + SHIFT")},
                                                     {QStringLiteral("grp:alt_shift_toggle"), tr7("Left ALT + SHIFT")},
                                                     {QStringLiteral("grp:grave_toggle"), tr7("Grave Accent (`)")}};
    return keys;
}

inline int switchKeyIndex(const QStringList &options)
{
    for (const QString &o : options) {
        if (o == QLatin1String("grp:lalt_lshift_toggle")) {
            return 2;
        }
        for (int i = 1; i < switchKeys().size(); ++i) {
            if (switchKeys().at(i).first == o) {
                return i;
            }
        }
    }
    return 0;
}

class KeySequenceDialog : public QDialog
{
public:
    int choice;

    KeySequenceDialog(int current, QWidget *parent)
        : QDialog(parent)
        , choice(current)
    {
        fixedDialog(this, tr7("Change Key Sequence"), QStringLiteral("seven-textservices"));
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(12, 12, 12, 12);
        auto *box = new QGroupBox(tr7("Switch Input Language"));
        auto *bv = new QVBoxLayout(box);
        auto *group = new QButtonGroup(this);
        for (int i = 0; i < switchKeys().size(); ++i) {
            auto *r = new QRadioButton(switchKeys().at(i).second);
            r->setChecked(i == current);
            group->addButton(r, i);
            bv->addWidget(r);
        }
        h->addWidget(box);
        auto *btns = new QVBoxLayout;
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        ok->setMinimumWidth(75);
        ok->setDefault(true);
        btns->addSpacing(14);
        btns->addWidget(ok);
        btns->addWidget(cancel);
        btns->addStretch(1);
        h->addLayout(btns);
        setFixedSize(sizeHint());
        connect(ok, &QPushButton::clicked, this, [this, group] {
            choice = group->checkedId();
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }
};

/* ---------------- Add Input Language ---------------- */

class AddLanguageDialog : public QDialog
{
public:
    QList<Layout> chosen;

    AddLanguageDialog(const KeyboardConfig &k, QWidget *parent)
        : QDialog(parent)
    {
        fixedDialog(this, tr7("Add Input Language"), QStringLiteral("seven-textservices"));
        setFixedSize(440, 420);
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(12, 12, 12, 12);
        auto *left = new QVBoxLayout;
        auto *intro = new QLabel(tr7("Use the checkbox to select the languages to add."));
        intro->setWordWrap(true);
        left->addWidget(intro);
        m_tree = new QTreeWidget;
        m_tree->setHeaderHidden(true);
        m_tree->setRootIsDecorated(true);
        left->addWidget(m_tree, 1);
        h->addLayout(left, 1);
        auto *btns = new QVBoxLayout;
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        ok->setMinimumWidth(75);
        ok->setDefault(true);
        btns->addSpacing(22);
        btns->addWidget(ok);
        btns->addWidget(cancel);
        btns->addStretch(1);
        h->addLayout(btns);

        // language -> keyboards, sorted like Windows
        QMap<QString, QList<Layout>> byLanguage;
        for (const Layout &l : allLayouts()) {
            byLanguage[inputLanguage(l)].append(l);
        }
        QStringList languages = byLanguage.keys();
        QCollator c(QLocale{});
        std::sort(languages.begin(), languages.end(), [&c](const QString &a, const QString &b) {
            return c.compare(a, b) < 0;
        });
        for (const QString &lang : std::as_const(languages)) {
            auto *li = new QTreeWidgetItem(m_tree, {lang});
            auto *kb = new QTreeWidgetItem(li, {tr7("Keyboard")});
            kb->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-keyboard")));
            for (const Layout &l : byLanguage.value(lang)) {
                auto *it = new QTreeWidgetItem(kb, {keyboardName(l)});
                bool installed = false;
                for (int i = 0; i < k.layouts.size(); ++i) {
                    installed = installed || (k.layouts.at(i) == l.layout && k.variants.value(i) == l.variant);
                }
                it->setFlags(installed ? Qt::ItemIsUserCheckable : (Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable));
                it->setCheckState(0, installed ? Qt::Checked : Qt::Unchecked);
                it->setData(0, Qt::UserRole, l.layout);
                it->setData(0, Qt::UserRole + 1, l.variant);
                it->setData(0, Qt::UserRole + 2, installed);
            }
        }
        connect(ok, &QPushButton::clicked, this, [this] {
            for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
                if ((*it)->checkState(0) == Qt::Checked && !(*it)->data(0, Qt::UserRole + 2).toBool() && !(*it)->data(0, Qt::UserRole).isNull()) {
                    chosen.append(findLayout((*it)->data(0, Qt::UserRole).toString(), (*it)->data(0, Qt::UserRole + 1).toString()));
                }
            }
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

private:
    QTreeWidget *m_tree;
};

/* ---------------- Text Services and Input Languages ---------------- */

inline const QString kLanguageBar = QStringLiteral("io.gitgud.wackyideas.keyboardlayout");

class TextServicesDialog : public QDialog
{
public:
    explicit TextServicesDialog(int tab, QWidget *parent = nullptr)
        : QDialog(parent)
        , m_k(readKeyboard())
        , m_old(m_k)
    {
        fixedDialog(this, tr7("Text Services and Input Languages"), QStringLiteral("seven-textservices"));
        setFixedWidth(430);
        readLanguageBar();
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 9, 7, 9);
        auto *tabs = new QTabWidget;
        tabs->addTab(generalTab(), tr7("General"));
        tabs->addTab(barTab(), tr7("Language Bar"));
        tabs->addTab(switchTab(), tr7("Advanced Key Settings"));
        tabs->setCurrentIndex(tab);
        v->addWidget(tabs, 1);
        auto *btns = new QHBoxLayout;
        btns->setContentsMargins(0, 4, 0, 0);
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        m_apply = new QPushButton(tr7("Apply"));
        for (QPushButton *b : {ok, cancel, m_apply}) {
            b->setMinimumWidth(75);
            btns->addWidget(b);
        }
        ok->setDefault(true);
        m_apply->setEnabled(false);
        v->addLayout(btns);
        setFixedHeight(sizeHint().height());
        connect(ok, &QPushButton::clicked, this, [this] {
            apply();
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_apply, &QPushButton::clicked, this, [this] {
            apply();
        });
    }

private:
    static QLabel *text(const QString &t)
    {
        auto *l = new QLabel(t);
        l->setWordWrap(true);
        return l;
    }

    void changed()
    {
        m_apply->setEnabled(true);
    }

    QString fullName(int i) const
    {
        const Layout l = findLayout(m_k.layouts.value(i), m_k.variants.value(i));
        return inputLanguage(l) + QStringLiteral(" - ") + keyboardName(l);
    }

    void fillGeneral()
    {
        m_default->blockSignals(true);
        m_default->clear();
        for (int i = 0; i < m_k.layouts.size(); ++i) {
            m_default->addItem(fullName(i));
        }
        m_default->setCurrentIndex(0);
        m_default->blockSignals(false);

        m_tree->clear();
        QTreeWidgetItem *lastLang = nullptr;
        QString lastName;
        for (int i = 0; i < m_k.layouts.size(); ++i) {
            const Layout l = findLayout(m_k.layouts.at(i), m_k.variants.value(i));
            const QString lang = inputLanguage(l);
            if (!lastLang || lang != lastName) { // neighbours with the same language share a node
                lastLang = new QTreeWidgetItem(m_tree, {lang});
                lastLang->setIcon(0, badgeIcon(badge(m_k, i)));
                auto *kb = new QTreeWidgetItem(lastLang, {tr7("Keyboard")});
                kb->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-keyboard")));
                lastName = lang;
            }
            auto *leaf = new QTreeWidgetItem(lastLang->child(0), {QStringLiteral("• ") + keyboardName(l)});
            leaf->setData(0, Qt::UserRole, i);
        }
        m_tree->expandAll();
        updateButtons();
    }

    int selectedLayout() const
    {
        const QTreeWidgetItem *it = m_tree->currentItem();
        return it && !it->data(0, Qt::UserRole).isNull() ? it->data(0, Qt::UserRole).toInt() : -1;
    }

    void updateButtons()
    {
        const int i = selectedLayout();
        m_remove->setEnabled(i >= 0 && m_k.layouts.size() > 1);
        m_up->setEnabled(i > 0);
        m_down->setEnabled(i >= 0 && i < m_k.layouts.size() - 1);
    }

    void move(int from, int to)
    {
        for (QStringList *l : {&m_k.layouts, &m_k.variants, &m_k.names}) {
            l->move(from, to);
        }
        fillGeneral();
        for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
            if ((*it)->data(0, Qt::UserRole).toInt() == to && !(*it)->data(0, Qt::UserRole).isNull()) {
                m_tree->setCurrentItem(*it);
            }
        }
        changed();
    }

    QWidget *generalTab()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(10, 10, 10, 10);
        auto *def = new QGroupBox(tr7("Default input language"));
        auto *dv = new QVBoxLayout(def);
        dv->addWidget(text(tr7("Select one of the installed input languages to use as the default for all input fields.")));
        m_default = new QComboBox;
        dv->addWidget(m_default);
        v->addWidget(def);
        auto *inst = new QGroupBox(tr7("Installed services"));
        auto *iv = new QVBoxLayout(inst);
        iv->addWidget(text(tr7("Select the services that you want for each input language shown in the list. Use the Add and Remove buttons to modify this list.")));
        auto *row = new QHBoxLayout;
        m_tree = new QTreeWidget;
        m_tree->setHeaderHidden(true);
        m_tree->setIndentation(16);
        m_tree->setMinimumHeight(170);
        row->addWidget(m_tree, 1);
        auto *col = new QVBoxLayout;
        auto *add = new QPushButton(tr7("Add..."));
        m_remove = new QPushButton(tr7("Delete"));
        auto *props = new QPushButton(tr7("Properties..."));
        props->setEnabled(false);
        m_up = new QPushButton(tr7("Move Up"));
        m_down = new QPushButton(tr7("Move Down"));
        col->addSpacing(50);
        for (QPushButton *b : {add, m_remove, props, m_up, m_down}) {
            b->setMinimumWidth(96);
            col->addWidget(b);
        }
        col->addStretch(1);
        row->addLayout(col);
        iv->addLayout(row);
        v->addWidget(inst, 1);
        fillGeneral();
        connect(m_tree, &QTreeWidget::currentItemChanged, this, [this] {
            updateButtons();
        });
        connect(m_default, &QComboBox::currentIndexChanged, this, [this](int i) {
            if (i > 0) {
                move(i, 0);
            }
        });
        connect(add, &QPushButton::clicked, this, [this] {
            AddLanguageDialog dlg(m_k, this);
            if (dlg.exec() == QDialog::Accepted && !dlg.chosen.isEmpty()) {
                for (const Layout &l : dlg.chosen) {
                    m_k.layouts << l.layout;
                    m_k.variants << l.variant;
                    m_k.names << QString();
                }
                fillGeneral();
                changed();
            }
        });
        connect(m_remove, &QPushButton::clicked, this, [this] {
            const int i = selectedLayout();
            if (i < 0 || m_k.layouts.size() < 2) {
                return;
            }
            m_k.layouts.removeAt(i);
            m_k.variants.removeAt(i);
            m_k.names.removeAt(i);
            fillGeneral();
            changed();
        });
        connect(m_up, &QPushButton::clicked, this, [this] {
            const int i = selectedLayout();
            if (i > 0) {
                move(i, i - 1);
            }
        });
        connect(m_down, &QPushButton::clicked, this, [this] {
            const int i = selectedLayout();
            if (i >= 0 && i < m_k.layouts.size() - 1) {
                move(i, i + 1);
            }
        });
        return page;
    }

    void readLanguageBar()
    {
        const QString out = Win7Tray::runScript(
            Win7Tray::findTray()
            + QStringLiteral(" var b = p ? p.widgets('%1') : []; if (b.length) { b[0].currentConfigGroup = ['General']; print('on:' + String(b[0].readConfig('displayStyle'))); } else print('off');")
                  .arg(kLanguageBar));
        m_barShown = out.startsWith(QLatin1String("on"));
        m_barLabels = !out.startsWith(QLatin1String("on:1"));
        m_oldBarShown = m_barShown;
        m_oldBarLabels = m_barLabels;
    }

    QWidget *barTab()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(10, 10, 10, 10);
        auto *box = new QGroupBox(tr7("Language Bar"));
        auto *bv = new QVBoxLayout(box);
        auto *docked = new QRadioButton(tr7("Docked in the taskbar"));
        auto *hidden = new QRadioButton(tr7("Hidden", "language bar"));
        (m_barShown ? docked : hidden)->setChecked(true);
        bv->addWidget(docked);
        bv->addWidget(hidden);
        v->addWidget(box);
        v->addSpacing(6);
        auto *labels = new QCheckBox(tr7("Show text labels on the Language bar"));
        labels->setChecked(m_barLabels);
        v->addWidget(labels);
        v->addStretch(1);
        connect(docked, &QRadioButton::toggled, this, [this](bool on) {
            m_barShown = on;
            changed();
        });
        connect(labels, &QCheckBox::toggled, this, [this](bool on) {
            m_barLabels = on;
            changed();
        });
        return page;
    }

    void fillSwitchList()
    {
        m_keys->clear();
        auto *it = new QTreeWidgetItem(m_keys, {tr7("Switch input language"), switchKeyIndex(m_k.options) == 0 ? tr7("(None)") : switchKeys().at(switchKeyIndex(m_k.options)).second});
        it->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        m_keys->setCurrentItem(it);
    }

    QWidget *switchTab()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(10, 10, 10, 10);
        auto *caps = new QGroupBox(tr7("To turn off Caps Lock"));
        auto *ch = new QHBoxLayout(caps);
        auto *byCaps = new QRadioButton(tr7("Press the CAPS LOCK key"));
        auto *byShift = new QRadioButton(tr7("Press the SHIFT key"));
        (m_k.options.contains(QStringLiteral("shift:breaks_caps")) ? byShift : byCaps)->setChecked(true);
        ch->addWidget(byCaps);
        ch->addWidget(byShift);
        v->addWidget(caps);
        auto *keys = new QGroupBox(tr7("Hot keys for input languages"));
        auto *kv = new QVBoxLayout(keys);
        m_keys = new QTreeWidget;
        m_keys->setColumnCount(2);
        m_keys->setHeaderLabels({tr7("Action"), tr7("Keyboard shortcuts")});
        m_keys->setRootIsDecorated(false);
        m_keys->header()->setStretchLastSection(false);
        m_keys->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_keys->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_keys->setMinimumHeight(200);
        kv->addWidget(m_keys, 1);
        auto *change = new QPushButton(tr7("Change Key Sequence..."));
        kv->addWidget(change, 0, Qt::AlignRight);
        v->addWidget(keys, 1);
        fillSwitchList();
        connect(byShift, &QRadioButton::toggled, this, [this](bool on) {
            m_k.options.removeAll(QStringLiteral("shift:breaks_caps"));
            if (on) {
                m_k.options << QStringLiteral("shift:breaks_caps");
            }
            changed();
        });
        connect(change, &QPushButton::clicked, this, [this] {
            KeySequenceDialog dlg(switchKeyIndex(m_k.options), this);
            if (dlg.exec() == QDialog::Accepted) {
                m_k.options.erase(std::remove_if(m_k.options.begin(), m_k.options.end(),
                                                 [](const QString &o) {
                                                     return o.startsWith(QLatin1String("grp:"));
                                                 }),
                                  m_k.options.end());
                if (dlg.choice > 0) {
                    m_k.options << switchKeys().at(dlg.choice).first;
                }
                fillSwitchList();
                changed();
            }
        });
        return page;
    }

    void apply()
    {
        if (!m_apply->isEnabled()) {
            return;
        }
        if (m_k.layouts != m_old.layouts || m_k.variants != m_old.variants || m_k.options != m_old.options || m_k.names != m_old.names) {
            writeKeyboard(m_k);
            m_old = m_k;
        }
        if (m_barShown != m_oldBarShown || m_barLabels != m_oldBarLabels) {
            QString js = Win7Tray::findTray() + QStringLiteral(" if (p) { var b = p.widgets('%1');").arg(kLanguageBar);
            if (!m_barShown) {
                js += QStringLiteral(" for (var i = 0; i < b.length; ++i) b[i].remove();");
            } else {
                js += QStringLiteral(" var w = b.length ? b[0] : p.addWidget('%1', (p.widgets('%2').length ? p.widgets('%2')[0].geometry.x + 2 : 100000), 2, 24, 36);"
                                     " w.currentConfigGroup = ['General']; w.writeConfig('displayStyle', %3); w.reloadConfig();")
                          .arg(kLanguageBar, Win7Tray::kTray)
                          .arg(m_barLabels ? 0 : 1);
            }
            js += QStringLiteral(" }");
            Win7Tray::runScript(js);
            m_oldBarShown = m_barShown;
            m_oldBarLabels = m_barLabels;
        }
        m_apply->setEnabled(false);
    }

    KeyboardConfig m_k;
    KeyboardConfig m_old;
    bool m_barShown = true, m_barLabels = true, m_oldBarShown = true, m_oldBarLabels = true;
    QPushButton *m_apply;
    QComboBox *m_default;
    QTreeWidget *m_tree;
    QPushButton *m_remove;
    QPushButton *m_up;
    QPushButton *m_down;
    QTreeWidget *m_keys;
};

/* ---------------- formats ---------------- */

inline QString localerc()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/plasma-localerc");
}

inline QString formatsLocale()
{
    QSettings s(localerc(), QSettings::IniFormat);
    const QString l = s.value(QStringLiteral("Formats/LANG")).toString();
    return l.isEmpty() ? QString::fromLocal8Bit(qgetenv("LANG")) : l;
}

/** Windows pattern ("tt") <-> Qt pattern ("AP") */
inline QString toQt(QString f)
{
    return f.replace(QStringLiteral("tt"), QStringLiteral("AP"));
}
inline QString fromQt(QString f)
{
    return f.replace(QStringLiteral("AP"), QStringLiteral("tt")).replace(QStringLiteral("ap"), QStringLiteral("tt"));
}

struct Formats {
    QString locale; // xx_YY.UTF-8
    QString shortDate, longDate, shortTime, longTime, am, pm;
    int firstDay = 1; // 0 = Sunday ... 6 = Saturday (QML Locale)

    bool operator==(const Formats &o) const
    {
        return locale == o.locale && shortDate == o.shortDate && longDate == o.longDate && shortTime == o.shortTime && longTime == o.longTime && am == o.am && pm == o.pm
            && firstDay == o.firstDay;
    }
};

inline Formats defaultsFor(const QString &locale)
{
    const QLocale l(locale.section(QLatin1Char('.'), 0, 0));
    Formats f;
    f.locale = locale;
    f.shortDate = fromQt(l.dateFormat(QLocale::ShortFormat));
    f.longDate = fromQt(l.dateFormat(QLocale::LongFormat)).remove(QRegularExpression(QStringLiteral("^dddd,?\\s*|,?\\s*dddd$")));
    f.shortTime = fromQt(l.timeFormat(QLocale::ShortFormat));
    f.longTime = fromQt(l.timeFormat(QLocale::LongFormat)).remove(QRegularExpression(QStringLiteral("\\s*t+$")));
    f.am = l.amText();
    f.pm = l.pmText();
    f.firstDay = l.firstDayOfWeek() % 7;
    return f;
}

inline Formats readFormats()
{
    Formats f = defaultsFor(formatsLocale());
    const QString out = Win7Tray::runScript(Win7Time::findClock()
                                            + QStringLiteral(" if (c) print(JSON.stringify({ df: String(c.readConfig('dateFormat')), cf: String(c.readConfig('customFormat')),"
                                                             " ld: String(c.readConfig('win7LongDate') || ''), st: String(c.readConfig('win7ShortTime') || ''),"
                                                             " lt: String(c.readConfig('win7LongTime') || ''), am: String(c.readConfig('win7AmSymbol') || ''),"
                                                             " pm: String(c.readConfig('win7PmSymbol') || ''), fd: String(c.readConfig('firstDayOfWeek')) }));"));
    const QJsonObject o = QJsonDocument::fromJson(out.toUtf8()).object();
    auto str = [&](const char *k) {
        return o.value(QLatin1String(k)).toString();
    };
    if (str("df") == QLatin1String("customDate")) { // the clock's own default is dd.MM.yyyy
        f.shortDate = fromQt(str("cf").isEmpty() || str("cf") == QLatin1String("undefined") ? QStringLiteral("dd.MM.yyyy") : str("cf"));
    }
    if (!str("ld").isEmpty()) {
        f.longDate = str("ld");
    }
    if (!str("st").isEmpty()) {
        f.shortTime = str("st");
    }
    if (!str("lt").isEmpty()) {
        f.longTime = str("lt");
    }
    if (!str("am").isEmpty()) {
        f.am = str("am");
    }
    if (!str("pm").isEmpty()) {
        f.pm = str("pm");
    }
    if (!str("fd").isEmpty() && str("fd") != QLatin1String("-1")) {
        f.firstDay = str("fd").toInt();
    }
    return f;
}

inline QString jsString(const QString &s)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}

inline void writeFormats(const Formats &f, const Formats &old)
{
    if (f.locale != old.locale) {
        ensureLocale(f.locale);
        QSettings s(localerc(), QSettings::IniFormat);
        s.setValue(QStringLiteral("Formats/LANG"), f.locale);
    }
    // the taskbar clock shows the date and time in these formats
    Win7Tray::runScript(Win7Time::findClock()
                        + QStringLiteral(" if (c) { c.writeConfig('dateFormat', 'customDate'); c.writeConfig('customFormat', %1); c.writeConfig('win7LongDate', %2);"
                                         " c.writeConfig('win7ShortTime', %3); c.writeConfig('win7LongTime', %4); c.writeConfig('win7AmSymbol', %5);"
                                         " c.writeConfig('win7PmSymbol', %6); c.writeConfig('firstDayOfWeek', %7); c.reloadConfig(); }")
                              .arg(jsString(toQt(f.shortDate)), jsString(f.longDate), jsString(f.shortTime), jsString(f.longTime), jsString(f.am), jsString(f.pm))
                              .arg(f.firstDay));
}

inline QString sample(const Formats &f, const QString &pattern, bool time)
{
    const QLocale l(f.locale.section(QLatin1Char('.'), 0, 0));
    const QDateTime now = QDateTime::currentDateTime();
    QString s = time ? l.toString(now.time(), toQt(pattern)) : l.toString(now.date(), toQt(pattern));
    if (time && pattern.contains(QLatin1String("tt"))) {
        s.replace(l.amText(), f.am).replace(l.pmText(), f.pm);
    }
    return s;
}

/* ---------------- Customize Format ---------------- */

class CustomizeFormat : public QDialog
{
public:
    Formats result;

    CustomizeFormat(const Formats &f, int tab, QWidget *parent)
        : QDialog(parent)
        , result(f)
    {
        fixedDialog(this, tr7("Customize Format"), QStringLiteral("seven-region"));
        setFixedWidth(470);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 9, 7, 9);
        auto *tabs = new QTabWidget;
        tabs->addTab(timeTab(), tr7("Time"));
        tabs->addTab(dateTab(), tr7("Date"));
        tabs->setCurrentIndex(tab);
        v->addWidget(tabs, 1);
        auto *btns = new QHBoxLayout;
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        auto *apply = new QPushButton(tr7("Apply"));
        apply->setEnabled(false);
        for (QPushButton *b : {ok, cancel, apply}) {
            b->setMinimumWidth(75);
            btns->addWidget(b);
        }
        ok->setDefault(true);
        v->addLayout(btns);
        setFixedHeight(sizeHint().height());
        connect(ok, &QPushButton::clicked, this, [this] {
            collect();
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

private:
    static QComboBox *combo(const QStringList &items, const QString &current)
    {
        auto *c = new QComboBox;
        c->setEditable(true);
        c->addItems(items);
        if (c->findText(current) < 0) {
            c->insertItem(0, current);
        }
        c->setCurrentText(current);
        c->setFixedWidth(150);
        return c;
    }

    static QLineEdit *sampleBox()
    {
        auto *e = new QLineEdit;
        e->setReadOnly(true);
        e->setFixedWidth(150);
        return e;
    }

    QWidget *resetRow(std::function<void()> reset)
    {
        auto *w = new QWidget;
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        auto *t = new QLabel(tr7("Click Reset to restore the system default settings."));
        t->setWordWrap(true);
        h->addWidget(t, 1);
        auto *b = new QPushButton(tr7("Reset"));
        b->setMinimumWidth(85);
        h->addWidget(b, 0, Qt::AlignBottom);
        connect(b, &QPushButton::clicked, this, std::move(reset));
        return w;
    }

    QWidget *timeTab()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(10, 10, 10, 10);
        auto *ex = new QGroupBox(tr7("Example"));
        auto *eg = new QGridLayout(ex);
        m_shortTimeSample = sampleBox();
        m_longTimeSample = sampleBox();
        eg->addWidget(new QLabel(tr7("Short time:")), 0, 0);
        eg->addWidget(m_shortTimeSample, 0, 1);
        eg->addWidget(new QLabel(tr7("Long time:")), 1, 0);
        eg->addWidget(m_longTimeSample, 1, 1);
        eg->setColumnStretch(2, 1);
        v->addWidget(ex);
        auto *fm = new QGroupBox(tr7("Time formats"));
        auto *fg = new QGridLayout(fm);
        m_shortTime = combo({QStringLiteral("H:mm"), QStringLiteral("HH:mm"), QStringLiteral("h:mm tt"), QStringLiteral("hh:mm tt")}, result.shortTime);
        m_longTime = combo({QStringLiteral("H:mm:ss"), QStringLiteral("HH:mm:ss"), QStringLiteral("h:mm:ss tt"), QStringLiteral("hh:mm:ss tt")}, result.longTime);
        m_am = combo({result.am, QStringLiteral("AM")}, result.am);
        m_pm = combo({result.pm, QStringLiteral("PM")}, result.pm);
        auto label = [](const QString &t) {
            auto *l = new QLabel(t);
            l->setWordWrap(true);
            return l;
        };
        fg->addWidget(label(tr7("Short time:")), 0, 0);
        fg->addWidget(m_shortTime, 0, 1);
        fg->addWidget(label(tr7("Long time:")), 1, 0);
        fg->addWidget(m_longTime, 1, 1);
        fg->addWidget(label(tr7("AM symbol:")), 2, 0);
        fg->addWidget(m_am, 2, 1);
        fg->addWidget(label(tr7("PM symbol:")), 3, 0);
        fg->addWidget(m_pm, 3, 1);
        fg->setColumnStretch(0, 1);
        v->addWidget(fm);
        auto *legend = new QLabel(tr7("What the notations mean:\n\nh = hour   m = minute   s = second (long time only)\ntt = A.M. or P.M.\n\nh/H = 12/24 hour\n\nhh, mm, ss = display leading zero\nh, m, s = do not display leading zero"));
        v->addWidget(legend);
        v->addStretch(1);
        v->addWidget(resetRow([this] {
            const Formats d = defaultsFor(result.locale);
            m_shortTime->setCurrentText(d.shortTime);
            m_longTime->setCurrentText(d.longTime);
            m_am->setCurrentText(d.am);
            m_pm->setCurrentText(d.pm);
        }));
        for (QComboBox *c : {m_shortTime, m_longTime, m_am, m_pm}) {
            connect(c, &QComboBox::currentTextChanged, this, [this] {
                updateSamples();
            });
        }
        updateSamples();
        return page;
    }

    QWidget *dateTab()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(10, 10, 10, 10);
        auto *ex = new QGroupBox(tr7("Example"));
        auto *eg = new QGridLayout(ex);
        m_shortDateSample = sampleBox();
        m_longDateSample = sampleBox();
        m_longDateSample->setFixedWidth(220);
        eg->addWidget(new QLabel(tr7("Short date:")), 0, 0);
        eg->addWidget(m_shortDateSample, 0, 1);
        eg->addWidget(new QLabel(tr7("Long date:")), 1, 0);
        eg->addWidget(m_longDateSample, 1, 1);
        eg->setColumnStretch(2, 1);
        v->addWidget(ex);
        auto *fm = new QGroupBox(tr7("Date formats"));
        auto *fg = new QGridLayout(fm);
        m_shortDate = combo({QStringLiteral("dd.MM.yyyy"), QStringLiteral("dd.MM.yy"), QStringLiteral("d.M.yy"), QStringLiteral("d.MM.yy"), QStringLiteral("yyyy-MM-dd")}, result.shortDate);
        m_longDate = combo({tr7("MMMM d, yyyy"), tr7("dddd, MMMM d, yyyy"), QStringLiteral("d MMMM yyyy")}, result.longDate);
        m_longDate->setFixedWidth(200);
        fg->addWidget(new QLabel(tr7("Short date:")), 0, 0);
        fg->addWidget(m_shortDate, 0, 1);
        fg->addWidget(new QLabel(tr7("Long date:")), 1, 0);
        fg->addWidget(m_longDate, 1, 1);
        fg->setColumnStretch(0, 1);
        v->addWidget(fm);
        auto *legend = new QLabel(tr7("What the notations mean:\n\nd, dd = day;  ddd, dddd = day of week;  M = month;  y = year"));
        v->addWidget(legend);
        v->addStretch(1);
        v->addWidget(resetRow([this] {
            const Formats d = defaultsFor(result.locale);
            m_shortDate->setCurrentText(d.shortDate);
            m_longDate->setCurrentText(d.longDate);
        }));
        for (QComboBox *c : {m_shortDate, m_longDate}) {
            connect(c, &QComboBox::currentTextChanged, this, [this] {
                updateSamples();
            });
        }
        updateSamples();
        return page;
    }

    void collect()
    {
        if (m_shortTime) {
            result.shortTime = m_shortTime->currentText();
            result.longTime = m_longTime->currentText();
            result.am = m_am->currentText();
            result.pm = m_pm->currentText();
        }
        if (m_shortDate) {
            result.shortDate = m_shortDate->currentText();
            result.longDate = m_longDate->currentText();
        }
    }

    void updateSamples()
    {
        Formats f = result;
        if (m_shortTime && m_longTime && m_am && m_pm) {
            f.shortTime = m_shortTime->currentText();
            f.longTime = m_longTime->currentText();
            f.am = m_am->currentText();
            f.pm = m_pm->currentText();
            m_shortTimeSample->setText(sample(f, f.shortTime, true));
            m_longTimeSample->setText(sample(f, f.longTime, true));
        }
        if (m_shortDate && m_longDate) {
            m_shortDateSample->setText(sample(f, m_shortDate->currentText(), false));
            m_longDateSample->setText(sample(f, m_longDate->currentText(), false));
        }
    }

    QComboBox *m_shortTime = nullptr, *m_longTime = nullptr, *m_am = nullptr, *m_pm = nullptr, *m_shortDate = nullptr, *m_longDate = nullptr;
    QLineEdit *m_shortTimeSample = nullptr, *m_longTimeSample = nullptr, *m_shortDateSample = nullptr, *m_longDateSample = nullptr;
};

/* ---------------- welcome screen / new users, system language ---------------- */

struct SystemLocale {
    QString lang;
    QStringList layouts, variants;
};

inline SystemLocale systemLocale()
{
    SystemLocale s;
    const QString status = run(QStringLiteral("localectl"), {QStringLiteral("status")});
    QRegularExpressionMatch m = QRegularExpression(QStringLiteral("System Locale:\\s*LANG=(\\S+)")).match(status);
    s.lang = m.hasMatch() ? m.captured(1) : QStringLiteral("C.UTF-8");
    m = QRegularExpression(QStringLiteral("X11 Layout:\\s*(\\S+)")).match(status);
    if (m.hasMatch()) {
        s.layouts = m.captured(1).split(QLatin1Char(','));
    }
    m = QRegularExpression(QStringLiteral("X11 Variant:\\s*(\\S+)")).match(status);
    if (m.hasMatch()) {
        s.variants = m.captured(1).split(QLatin1Char(','));
    }
    return s;
}

inline QString displayLanguage()
{
    QSettings s(localerc(), QSettings::IniFormat);
    QString lang = s.value(QStringLiteral("Translations/LANGUAGE")).toString().section(QLatin1Char(':'), 0, 0);
    if (lang.isEmpty()) {
        lang = formatsLocale();
    }
    return languageName(lang.section(QLatin1Char('_'), 0, 0).section(QLatin1Char('.'), 0, 0));
}

class CopySettingsDialog : public QDialog
{
public:
    explicit CopySettingsDialog(QWidget *parent)
        : QDialog(parent)
    {
        fixedDialog(this, tr7("Welcome screen and new user accounts settings"), QStringLiteral("seven-region"));
        setFixedWidth(500);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(12, 12, 12, 12);
        auto *intro = new QLabel(tr7("Display the current settings of the current user, the welcome screen (system accounts) and new user accounts."));
        intro->setWordWrap(true);
        v->addWidget(intro);
        auto *box = new QFrame;
        box->setObjectName(QStringLiteral("win7copybox"));
        box->setStyleSheet(QStringLiteral("#win7copybox { border: 1px solid #a0a0a0; background: white; }"));
        auto *grid = new QGridLayout(box);
        grid->setContentsMargins(8, 8, 8, 8);
        grid->setVerticalSpacing(6);
        const KeyboardConfig k = readKeyboard();
        const SystemLocale sys = systemLocale();
        const Layout userLayout = findLayout(k.layouts.value(0), k.variants.value(0));
        const Layout sysLayout = findLayout(sys.layouts.value(0), sys.variants.value(0));
        int row = 0;
        auto section = [&](const QString &title, const QString &ui, const QString &input, const QString &format) {
            auto *h = new QLabel(title);
            h->setStyleSheet(QStringLiteral("color: #1e3287; border-bottom: 1px solid #d5dfe5;"));
            grid->addWidget(h, row++, 0, 1, 2);
            const QList<QPair<QString, QString>> lines{{tr7("Display language:"), ui}, {tr7("Input language:"), input}, {tr7("Format:"), format}};
            for (const auto &l : lines) {
                grid->addWidget(new QLabel(l.first), row, 0);
                grid->addWidget(new QLabel(l.second), row++, 1);
            }
        };
        section(tr7("Current user"), displayLanguage(), inputLanguage(userLayout) + QStringLiteral(" - ") + keyboardName(userLayout), localeName(formatsLocale()));
        const QString sysLang = languageName(sys.lang.section(QLatin1Char('_'), 0, 0));
        const QString sysInput = inputLanguage(sysLayout) + QStringLiteral(" - ") + keyboardName(sysLayout);
        section(tr7("Welcome screen"), sysLang, sysInput, localeName(sys.lang));
        section(tr7("New user accounts"), sysLang, sysInput, localeName(sys.lang));
        grid->setColumnStretch(1, 1);
        grid->setColumnMinimumWidth(0, 140);
        v->addWidget(box);
        v->addSpacing(10);
        v->addWidget(new QLabel(tr7("Copy your current settings to:")));
        auto *welcome = new QCheckBox(tr7("Welcome screen and system accounts"));
        auto *newUsers = new QCheckBox(tr7("New user accounts"));
        auto *wrap = new QVBoxLayout;
        wrap->setContentsMargins(16, 0, 0, 0);
        wrap->addWidget(welcome);
        wrap->addWidget(newUsers);
        v->addLayout(wrap);
        auto *note = new QLabel(tr7("On Linux the welcome screen and new accounts use the same system settings."));
        note->setWordWrap(true);
        note->setStyleSheet(QStringLiteral("color: #555555;"));
        v->addWidget(note);
        v->addSpacing(8);
        auto *btns = new QHBoxLayout;
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        ok->setMinimumWidth(75);
        cancel->setMinimumWidth(75);
        ok->setDefault(true);
        btns->addWidget(ok);
        btns->addWidget(cancel);
        v->addLayout(btns);
        setFixedSize(sizeHint());
        // one system setting behind both boxes
        connect(welcome, &QCheckBox::toggled, newUsers, &QCheckBox::setChecked);
        connect(newUsers, &QCheckBox::toggled, welcome, &QCheckBox::setChecked);
        connect(ok, &QPushButton::clicked, this, [this, welcome, k] {
            if (welcome->isChecked()) {
                const QString lang = formatsLocale();
                ensureLocale(lang);
                int code = 0;
                run(QStringLiteral("localectl"), {QStringLiteral("set-locale"), QStringLiteral("LANG=") + lang}, &code);
                if (code == 0) {
                    QStringList grp;
                    for (const QString &o : k.options) {
                        if (o.startsWith(QLatin1String("grp:"))) {
                            grp << o;
                        }
                    }
                    run(QStringLiteral("localectl"),
                        {QStringLiteral("set-x11-keymap"), k.layouts.join(QLatin1Char(',')), QStringLiteral("pc105"), k.variants.mid(0, k.layouts.size()).join(QLatin1Char(',')), grp.join(QLatin1Char(','))});
                }
            }
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }
};

class SystemLanguageDialog : public QDialog
{
public:
    explicit SystemLanguageDialog(QWidget *parent)
        : QDialog(parent)
    {
        fixedDialog(this, tr7("Language for non-Unicode programs"), QStringLiteral("seven-region"));
        setFixedWidth(420);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(12, 12, 12, 12);
        auto *intro = new QLabel(tr7("Select the language that the system uses by default for all accounts and for programs that do not support Unicode."));
        intro->setWordWrap(true);
        v->addWidget(intro);
        v->addSpacing(8);
        v->addWidget(new QLabel(tr7("Current system locale:")));
        const QString current = systemLocale().lang;
        auto *box = new QComboBox;
        fillLocales(box, current);
        v->addWidget(box);
        v->addSpacing(20);
        auto *btns = new QHBoxLayout;
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        ok->setMinimumWidth(75);
        cancel->setMinimumWidth(75);
        ok->setDefault(true);
        btns->addWidget(ok);
        btns->addWidget(cancel);
        v->addLayout(btns);
        setFixedSize(sizeHint());
        connect(ok, &QPushButton::clicked, this, [this, box, current] {
            const QString lang = box->currentData().toString();
            if (lang.section(QLatin1Char('.'), 0, 0) != current.section(QLatin1Char('.'), 0, 0)) {
                if (!ensureLocale(lang)) {
                    return;
                }
                int code = 0;
                run(QStringLiteral("localectl"), {QStringLiteral("set-locale"), QStringLiteral("LANG=") + lang}, &code);
                if (code != 0) {
                    return;
                }
                QMessageBox ask(QMessageBox::Information, tr7("Change System Locale"),
                                tr7("You must restart your computer in order for the new system locale to take effect."), QMessageBox::NoButton, this);
                QPushButton *now = ask.addButton(tr7("Restart now"), QMessageBox::AcceptRole);
                ask.addButton(tr7("Cancel", "restart"), QMessageBox::RejectRole);
                ask.exec();
                if (ask.clickedButton() == now) {
                    QProcess::startDetached(QStringLiteral("qdbus6"), {QStringLiteral("org.kde.Shutdown"), QStringLiteral("/Shutdown"), QStringLiteral("logoutAndReboot")});
                }
            }
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }
};

/* ---------------- Region and Language ---------------- */

class RegionDialog : public QDialog
{
public:
    explicit RegionDialog(int tab, QWidget *parent = nullptr)
        : QDialog(parent)
        , m_old(readFormats())
        , m_f(m_old)
    {
        fixedDialog(this, tr7("Region and Language"), QStringLiteral("seven-region"));
        setAttribute(Qt::WA_DeleteOnClose);
        setFixedWidth(470);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 9, 7, 9);
        auto *tabs = new QTabWidget;
        tabs->addTab(formatsTab(), tr7("Formats"));
        tabs->addTab(keyboardsTab(), tr7("Keyboards and Languages"));
        tabs->addTab(advancedTab(), tr7("Advanced"));
        tabs->setCurrentIndex(tab);
        v->addWidget(tabs, 1);
        auto *btns = new QHBoxLayout;
        btns->setContentsMargins(0, 4, 0, 0);
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        m_apply = new QPushButton(tr7("Apply"));
        for (QPushButton *b : {ok, cancel, m_apply}) {
            b->setMinimumWidth(75);
            btns->addWidget(b);
        }
        ok->setDefault(true);
        m_apply->setEnabled(false);
        v->addLayout(btns);
        setFixedHeight(sizeHint().height());
        connect(ok, &QPushButton::clicked, this, [this] {
            apply();
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_apply, &QPushButton::clicked, this, [this] {
            apply();
        });
    }

private:
    static QLabel *text(const QString &t)
    {
        auto *l = new QLabel(t);
        l->setWordWrap(true);
        return l;
    }

    void changed()
    {
        m_apply->setEnabled(!(m_f == m_old));
        refreshSamples();
    }

    void apply()
    {
        if (m_f == m_old) {
            return;
        }
        const bool localeChanged = m_f.locale != m_old.locale;
        writeFormats(m_f, m_old);
        m_old = m_f;
        changed();
        if (localeChanged) {
            QMessageBox::information(this, tr7("Region and Language"),
                                     tr7("The new format applies to all programs at the next logon."));
        }
    }

    QComboBox *formatCombo(const QStringList &items, const QString &current, std::function<void(const QString &)> set)
    {
        auto *c = new QComboBox;
        c->addItems(items);
        if (c->findText(current) < 0) {
            c->insertItem(0, current);
        }
        c->setCurrentText(current);
        c->setFixedWidth(220);
        connect(c, &QComboBox::currentTextChanged, this, [this, set](const QString &t) {
            set(t);
            changed();
        });
        return c;
    }

    void syncCombos()
    {
        const QList<QPair<QComboBox *, QString>> pairs{{m_shortDate, m_f.shortDate}, {m_longDate, m_f.longDate}, {m_shortTime, m_f.shortTime}, {m_longTime, m_f.longTime}};
        for (const auto &p : pairs) {
            p.first->blockSignals(true);
            if (p.first->findText(p.second) < 0) {
                p.first->insertItem(0, p.second);
            }
            p.first->setCurrentText(p.second);
            p.first->blockSignals(false);
        }
        m_firstDay->blockSignals(true);
        m_firstDay->setCurrentIndex((m_f.firstDay + 6) % 7);
        m_firstDay->blockSignals(false);
    }

    void refreshSamples()
    {
        if (!m_sShortDate) {
            return;
        }
        m_sShortDate->setText(sample(m_f, m_f.shortDate, false));
        m_sLongDate->setText(sample(m_f, m_f.longDate, false));
        m_sShortTime->setText(sample(m_f, m_f.shortTime, true));
        m_sLongTime->setText(sample(m_f, m_f.longTime, true));
    }

    QWidget *formatsTab()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(10, 10, 10, 10);
        v->addWidget(new QLabel(tr7("Format:")));
        auto *format = new QComboBox;
        fillLocales(format, m_f.locale);
        v->addWidget(format);
        v->addSpacing(8);
        auto *box = new QGroupBox(tr7("Date and time formats"));
        auto *g = new QGridLayout(box);
        g->setVerticalSpacing(8);
        m_shortDate = formatCombo({QStringLiteral("dd.MM.yyyy"), QStringLiteral("dd.MM.yy"), QStringLiteral("d.M.yy"), QStringLiteral("yyyy-MM-dd")}, m_f.shortDate, [this](const QString &t) {
            m_f.shortDate = t;
        });
        m_longDate = formatCombo({tr7("MMMM d, yyyy"), tr7("dddd, MMMM d, yyyy"), QStringLiteral("d MMMM yyyy")}, m_f.longDate, [this](const QString &t) {
            m_f.longDate = t;
        });
        m_shortTime = formatCombo({QStringLiteral("H:mm"), QStringLiteral("HH:mm"), QStringLiteral("h:mm tt")}, m_f.shortTime, [this](const QString &t) {
            m_f.shortTime = t;
        });
        m_longTime = formatCombo({QStringLiteral("H:mm:ss"), QStringLiteral("HH:mm:ss"), QStringLiteral("h:mm:ss tt")}, m_f.longTime, [this](const QString &t) {
            m_f.longTime = t;
        });
        m_firstDay = new QComboBox;
        for (int d = 1; d <= 7; ++d) {
            m_firstDay->addItem(QLocale{}.standaloneDayName(d));
        }
        m_firstDay->setCurrentIndex((m_f.firstDay + 6) % 7);
        m_firstDay->setFixedWidth(220);
        connect(m_firstDay, &QComboBox::currentIndexChanged, this, [this](int i) {
            m_f.firstDay = (i + 1) % 7;
            changed();
        });
        const QList<QPair<QString, QWidget *>> rows{{tr7("Short date:"), m_shortDate},
                                                   {tr7("Long date:"), m_longDate},
                                                   {tr7("Short time:"), m_shortTime},
                                                   {tr7("Long time:"), m_longTime},
                                                   {tr7("First day of week:"), m_firstDay}};
        for (int i = 0; i < rows.size(); ++i) {
            g->addWidget(new QLabel(rows.at(i).first), i, 0);
            g->addWidget(rows.at(i).second, i, 1);
        }
        g->setColumnStretch(0, 1);
        v->addWidget(box);
        auto *samples = new QGroupBox(tr7("Examples of how data is displayed using this format:"));
        auto *sg = new QGridLayout(samples);
        m_sShortDate = new QLabel;
        m_sLongDate = new QLabel;
        m_sShortTime = new QLabel;
        m_sLongTime = new QLabel;
        const QList<QPair<QString, QLabel *>> srows{{tr7("Short date:"), m_sShortDate},
                                                   {tr7("Long date:"), m_sLongDate},
                                                   {tr7("Short time:"), m_sShortTime},
                                                   {tr7("Long time:"), m_sLongTime}};
        for (int i = 0; i < srows.size(); ++i) {
            sg->addWidget(new QLabel(srows.at(i).first), i, 0);
            sg->addWidget(srows.at(i).second, i, 1);
        }
        sg->setColumnMinimumWidth(0, 160);
        sg->setColumnStretch(1, 1);
        v->addWidget(samples);
        v->addSpacing(4);
        auto *more = new QPushButton(tr7("Additional settings..."));
        more->setMinimumWidth(more->sizeHint().width() + 30);
        v->addWidget(more, 0, Qt::AlignRight);
        v->addStretch(1);
        refreshSamples();
        auto *tick = new QTimer(page);
        connect(tick, &QTimer::timeout, this, [this] {
            refreshSamples();
        });
        tick->start(1000);
        connect(format, &QComboBox::currentIndexChanged, this, [this, format] {
            // a new format brings its own date and time patterns, like in Windows
            const Formats d = defaultsFor(format->currentData().toString());
            m_f = d;
            syncCombos();
            changed();
        });
        connect(more, &QPushButton::clicked, this, [this] {
            CustomizeFormat dlg(m_f, 0, this);
            if (dlg.exec() == QDialog::Accepted) {
                m_f = dlg.result;
                syncCombos();
                changed();
            }
        });
        return page;
    }

    QWidget *keyboardsTab()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(10, 10, 10, 10);
        auto *box = new QGroupBox(tr7("Keyboards and other input languages"));
        auto *bv = new QVBoxLayout(box);
        bv->addWidget(text(tr7("To change your keyboard or input language click Change keyboards.")));
        auto *change = new QPushButton(tr7("Change keyboards..."));
        change->setMinimumWidth(190);
        bv->addWidget(change, 0, Qt::AlignRight);
        v->addWidget(box);
        v->addStretch(1);
        connect(change, &QPushButton::clicked, this, [this] {
            TextServicesDialog dlg(0, this);
            dlg.setAttribute(Qt::WA_DeleteOnClose, false);
            dlg.exec();
        });
        return page;
    }

    QWidget *advancedTab()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(10, 10, 10, 10);
        auto *welcome = new QGroupBox(tr7("Welcome screen and new user accounts"));
        auto *wv = new QVBoxLayout(welcome);
        wv->addWidget(text(tr7("View and copy your international settings to the welcome screen, system accounts and new user accounts.")));
        auto *copy = shieldButton(tr7("Copy settings..."));
        copy->setMinimumWidth(210);
        wv->addWidget(copy, 0, Qt::AlignRight);
        v->addWidget(welcome);
        auto *system = new QGroupBox(tr7("Language for non-Unicode programs", "section"));
        auto *sv = new QVBoxLayout(system);
        sv->addWidget(text(tr7("This setting (system locale) controls the language used when displaying text in programs that do not support Unicode.")));
        sv->addSpacing(6);
        sv->addWidget(new QLabel(tr7("Current language for non-Unicode programs:")));
        auto *current = new QLabel(localeName(systemLocale().lang));
        current->setContentsMargins(20, 4, 0, 4);
        sv->addWidget(current);
        auto *changeSys = shieldButton(tr7("Change system locale..."));
        changeSys->setMinimumWidth(210);
        sv->addWidget(changeSys, 0, Qt::AlignRight);
        v->addWidget(system);
        v->addStretch(1);
        connect(copy, &QPushButton::clicked, this, [this] {
            if (!(m_f == m_old)) {
                QMessageBox ask(QMessageBox::NoIcon, tr7("Change Regional and Language Options"), tr7("Apply the regional and language changes?"),
                                QMessageBox::NoButton, this);
                ask.setInformativeText(tr7("To make sure these changes are kept, it's recommended to apply them now, before making other system changes."));
                QPushButton *yes = ask.addButton(tr7("Apply"), QMessageBox::AcceptRole);
                ask.addButton(tr7("Cancel"), QMessageBox::RejectRole);
                ask.exec();
                if (ask.clickedButton() != yes) {
                    return;
                }
                apply();
            }
            CopySettingsDialog dlg(this);
            dlg.exec();
        });
        connect(changeSys, &QPushButton::clicked, this, [this, current] {
            SystemLanguageDialog dlg(this);
            dlg.exec();
            current->setText(localeName(systemLocale().lang));
        });
        return page;
    }

    Formats m_old;
    Formats m_f;
    QPushButton *m_apply;
    QComboBox *m_shortDate = nullptr, *m_longDate = nullptr, *m_shortTime = nullptr, *m_longTime = nullptr, *m_firstDay = nullptr;
    QLabel *m_sShortDate = nullptr, *m_sLongDate = nullptr, *m_sShortTime = nullptr, *m_sLongTime = nullptr;
};
}

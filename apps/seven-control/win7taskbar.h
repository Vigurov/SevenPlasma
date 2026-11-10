/*
 * "Taskbar and Start Menu Properties" and "Customize Start Menu" dialogs of Windows 7,
 * applied to the AeroThemePlasma panel, task manager (seventasks), Show desktop button and SevenStart.
 * Only options that do something on Linux are offered: there are no toolbars, homegroup,
 * administrative tools menu or recorded TV here, so those are left out.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "win7trayicons.h"

#include <QButtonGroup>
#include <QDBusInterface>
#include <QFileDialog>
#include <QListWidget>
#include <QDialog>
#include <QGroupBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QAbstractItemView>
#include <QTabWidget>

namespace Win7Taskbar
{
inline const QString kTasks = QStringLiteral("io.gitgud.wackyideas.seventasks");
inline const QString kStart = QStringLiteral("io.gitgud.wackyideas.SevenStart");
inline const QString kPeek = QStringLiteral("io.gitgud.wackyideas.win7showdesktop");
inline const QString kDefaultSidePanel = QStringLiteral(
    "{\"Home directory\":1,\"Documents\":1,\"Pictures\":1,\"Music\":1,\"Computer\":1,\"Control Panel\":1,\"Devices and Printers\":1,\"Default Programs\":1,\"Help and Support\":1}");

struct State {
    bool valid = false;
    bool locked = true;
    bool autoHide = false;
    bool smallIcons = false;
    int location = 0; // bottom, left, right, top
    int buttons = 0;  // always combine, combine when full, never combine
    bool peek = true;
    int power = 5;
    int recentPrograms = 10; // 0 = do not keep the list
    bool recentItems = true;
    bool highlightNew = true;
    QJsonObject sidePanel;

    bool operator==(const State &o) const
    {
        return locked == o.locked && autoHide == o.autoHide && smallIcons == o.smallIcons && location == o.location && buttons == o.buttons && peek == o.peek
            && power == o.power && recentPrograms == o.recentPrograms && recentItems == o.recentItems && highlightNew == o.highlightNew && sidePanel == o.sidePanel;
    }
};

inline const QStringList &locations()
{
    static const QStringList l{QStringLiteral("bottom"), QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("top")};
    return l;
}

// the panel that holds the Start button; scripts below use `p`
inline QString findPanel()
{
    return QStringLiteral("var p = null; var ps = panels(); for (var i = 0; i < ps.length && !p; ++i) { if (ps[i].widgets('%1').length) p = ps[i]; }"
                          " function one(t) { var ws = p ? p.widgets(t) : []; if (ws.length) { ws[0].currentConfigGroup = ['General']; return ws[0]; } return null; }")
        .arg(kStart);
}

inline bool editMode()
{
    QDBusInterface shell(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"), QStringLiteral("org.kde.PlasmaShell"));
    return shell.property("editMode").toBool();
}

inline void setEditMode(bool on)
{
    QDBusInterface shell(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"), QStringLiteral("org.kde.PlasmaShell"));
    shell.setProperty("editMode", on);
}

inline State readState()
{
    State st;
    const QString out = Win7Tray::runScript(
        findPanel()
        + QStringLiteral(" if (p) { var t = one('%1'), s = one('%2'), k = one('%3'); print(JSON.stringify({"
                         " location: p.location, hiding: p.hiding, height: p.height,"
                         " grouping: t ? String(t.readConfig('groupingStrategy')) : '', whenFull: t ? String(t.readConfig('onlyGroupWhenFull')) : '',"
                         " labels: t ? String(t.readConfig('showLabels')) : '', jumplists: t ? String(t.readConfig('disableJumplists')) : '',"
                         " power: s ? String(s.readConfig('powerButtonAction')) : '', rows: s ? String(s.readConfig('numberRows')) : '',"
                         " side: s ? String(s.readConfig('sidePanelVisibility')) : '', hl: s ? String(s.readConfig('highlightNewApps')) : '', peek: k ? String(k.readConfig('peekingEnabled')) : '' })); }")
              .arg(kTasks, kStart, kPeek));
    const QJsonObject o = QJsonDocument::fromJson(out.toUtf8()).object();
    if (o.isEmpty()) {
        return st;
    }
    auto text = [&](const char *k) {
        return o.value(QLatin1String(k)).toString();
    };
    auto flag = [&](const char *k, bool def) {
        const QString v = text(k);
        return v.isEmpty() ? def : v == QLatin1String("true");
    };
    st.valid = true;
    st.locked = !editMode();
    st.autoHide = text("hiding") == QLatin1String("autohide");
    st.smallIcons = o.value(QLatin1String("height")).toInt(40) < 36;
    st.location = qMax(0, int(locations().indexOf(text("location"))));
    const bool group = text("grouping") != QLatin1String("0");
    st.buttons = !group ? 2 : flag("whenFull", false) ? 1 : 0;
    st.peek = flag("peek", true);
    st.power = text("power").isEmpty() ? 5 : text("power").toInt();
    st.recentPrograms = text("rows").isEmpty() ? 8 : text("rows").toInt(); // SevenStart's default
    st.recentItems = !flag("jumplists", false);
    st.highlightNew = flag("hl", true);
    st.sidePanel = QJsonDocument::fromJson((text("side").isEmpty() ? kDefaultSidePanel : text("side")).toUtf8()).object();
    return st;
}

inline void writeState(const State &st, const State &old)
{
    if (st.locked != old.locked) {
        setEditMode(!st.locked);
    }
    const QString side = QString::fromUtf8(QJsonDocument(st.sidePanel).toJson(QJsonDocument::Compact));
    const bool group = st.buttons != 2;
    QString js = findPanel() + QStringLiteral(" if (p) {");
    if (st.location != old.location) {
        js += QStringLiteral(" p.location = '%1';").arg(locations().value(st.location));
    }
    if (st.autoHide != old.autoHide) {
        js += QStringLiteral(" p.hiding = '%1';").arg(st.autoHide ? QStringLiteral("autohide") : QStringLiteral("none"));
    }
    if (st.smallIcons != old.smallIcons) { // Windows 7: 40 px taskbar, 30 px with small icons
        js += QStringLiteral(" p.height = %1; p.currentConfigGroup = ['General']; p.writeConfig('panelThickness', %1);").arg(st.smallIcons ? 30 : 40);
    }
    js += QStringLiteral(
              " var t = one('%1'); if (t) { t.writeConfig('groupingStrategy', %2); t.writeConfig('onlyGroupWhenFull', %3); t.writeConfig('showLabels', %4);"
              " t.writeConfig('disableJumplists', %5); t.reloadConfig(); }"
              " var s = one('%6'); if (s) { s.writeConfig('powerButtonAction', %7); s.writeConfig('numberRows', %8); s.writeConfig('sidePanelVisibility', %9); s.writeConfig('highlightNewApps', %10); s.reloadConfig(); }")
              .arg(kTasks)
              .arg(group ? 1 : 0)
              .arg(st.buttons == 1 ? QStringLiteral("true") : QStringLiteral("false"))
              .arg(st.buttons == 0 ? QStringLiteral("false") : QStringLiteral("true"))
              .arg(st.recentItems ? QStringLiteral("false") : QStringLiteral("true"))
              .arg(kStart)
              .arg(st.power)
              .arg(st.recentPrograms)
              .arg(QString::fromUtf8(QJsonDocument(QJsonArray{side}).toJson(QJsonDocument::Compact)).mid(1).chopped(1))
              .arg(st.highlightNew ? QStringLiteral("true") : QStringLiteral("false"));
    js += QStringLiteral(" var k = one('%1'); if (k) { k.writeConfig('peekingEnabled', %2); k.reloadConfig(); } }")
              .arg(kPeek, st.peek ? QStringLiteral("true") : QStringLiteral("false"));
    Win7Tray::runScript(js);
}

/** A checkbox whose text wraps onto several lines (QCheckBox cannot wrap) */
class WrapCheck : public QWidget
{
public:
    QCheckBox *box;

    explicit WrapCheck(const QString &text)
    {
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(4);
        box = new QCheckBox;
        h->addWidget(box, 0, Qt::AlignTop);
        m_label = new QLabel(text);
        m_label->setWordWrap(true);
        m_label->setContentsMargins(0, 1, 0, 0);
        h->addWidget(m_label, 1);
        m_label->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == m_label && e->type() == QEvent::MouseButtonRelease && box->isEnabled()) {
            box->toggle();
        }
        return QWidget::eventFilter(o, e);
    }

private:
    QLabel *m_label;
};

/* ---------------- Toolbars ---------------- */

inline const QString kToolbar = QStringLiteral("io.gitgud.wackyideas.win7toolbar");

struct Toolbar {
    int id = -1;
    QString kind; // desktop, address, folder
    QString folder;
    QString title() const
    {
        if (kind == QLatin1String("address")) {
            return QStringLiteral("Адреса");
        }
        if (kind == QLatin1String("desktop")) {
            return QStringLiteral("Робочий стіл");
        }
        const QString name = QString(folder).remove(QStringLiteral("file://")).section(QLatin1Char('/'), -1, -1, QString::SectionSkipEmpty);
        return name.isEmpty() ? QStringLiteral("/") : name;
    }
};

inline QList<Toolbar> toolbars()
{
    QList<Toolbar> list;
    const QString out = Win7Tray::runScript(findPanel()
                                            + QStringLiteral(" var r = []; if (p) { var ws = p.widgets('%1'); for (var i = 0; i < ws.length; ++i) {"
                                                             " ws[i].currentConfigGroup = ['General']; r.push({ id: ws[i].id, kind: String(ws[i].readConfig('kind') || 'desktop'),"
                                                             " folder: String(ws[i].readConfig('folder') || '') }); } } print(JSON.stringify(r));")
                                                  .arg(kToolbar));
    for (const QJsonValue &v : QJsonDocument::fromJson(out.toUtf8()).array()) {
        const QJsonObject o = v.toObject();
        list.append({o.value(QLatin1String("id")).toInt(), o.value(QLatin1String("kind")).toString(), o.value(QLatin1String("folder")).toString()});
    }
    return list;
}

/** New toolbars go to the left of the language bar / notification area, like in Windows 7 */
inline void addToolbar(const QString &kind, const QString &folder = QString())
{
    Win7Tray::runScript(findPanel()
                        + QStringLiteral(" if (p) { var a = p.widgets('io.gitgud.wackyideas.keyboardlayout'); if (!a.length) a = p.widgets('%1');"
                                         " var x = a.length ? a[0].geometry.x + 2 : 100000; var w = p.addWidget('%2', x, 2, 100, 36);"
                                         " w.currentConfigGroup = ['General']; w.writeConfig('kind', '%3'); w.writeConfig('folder', %4); w.reloadConfig(); }")
                              .arg(Win7Tray::kTray, kToolbar, kind,
                                   QString::fromUtf8(QJsonDocument(QJsonArray{folder}).toJson(QJsonDocument::Compact)).mid(1).chopped(1)));
}

inline void removeToolbar(int id)
{
    Win7Tray::runScript(findPanel() + QStringLiteral(" if (p) { var w = p.widgetById(%1); if (w && w.type == '%2') w.remove(); }").arg(id).arg(kToolbar));
}

/** Address / Desktop in the taskbar menu: switch on or off */
inline void toggleToolbar(const QString &kind)
{
    for (const Toolbar &t : toolbars()) {
        if (t.kind == kind) {
            removeToolbar(t.id);
            return;
        }
    }
    addToolbar(kind);
}

/** "New toolbar..." : pick a folder */
inline void newToolbar()
{
    const QString dir = QFileDialog::getExistingDirectory(nullptr, QStringLiteral("Створення панелі інструментів - Вибір папки"), QDir::homePath());
    if (!dir.isEmpty()) {
        addToolbar(QStringLiteral("folder"), dir);
    }
}

/* ---------------- Customize Start Menu ---------------- */

class CustomizeStartMenu : public QDialog
{
public:
    CustomizeStartMenu(const State &st, QWidget *parent)
        : QDialog(parent)
        , m_state(st)
    {
        setWindowTitle(QStringLiteral("Настроювання меню «Пуск»"));
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-taskbar-props")));
        resize(430, 520);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(11, 11, 11, 11);
        v->setSpacing(6);
        auto *intro = new QLabel(QStringLiteral("Можна настроїти вигляд і поведінку посилань, значків і меню в меню «Пуск»."));
        intro->setWordWrap(true);
        v->addWidget(intro);

        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setStyleSheet(QStringLiteral("QScrollArea { border: 1px solid #828790; background: #ffffff; }"));
        auto *list = new QWidget;
        list->setObjectName(QStringLiteral("win7smlist"));
        list->setStyleSheet(QStringLiteral("#win7smlist { background: #ffffff; }"));
        m_list = new QVBoxLayout(list);
        m_list->setContentsMargins(4, 3, 4, 3);
        m_list->setSpacing(1);
        scroll->setWidget(list);
        v->addWidget(scroll, 1);

        // name in the SevenStart config, text, icon, kind: 3 = hide/menu/link, 2 = hide/link, 0 = checkbox
        addItem(QStringLiteral("__highlightNew"), QStringLiteral("Виділяти нещодавно встановлені програми"), QString(), 0);
        addItem(QStringLiteral("Videos"), QStringLiteral("Відео"), QStringLiteral("folder-videos"), 3);
        addItem(QStringLiteral("Help and Support"), QStringLiteral("Довідка"), QString(), 0);
        addItem(QStringLiteral("Documents"), QStringLiteral("Документи"), QStringLiteral("folder-documents"), 3);
        addItem(QStringLiteral("Downloads"), QStringLiteral("Завантаження"), QStringLiteral("folder-download"), 3);
        addItem(QStringLiteral("Pictures"), QStringLiteral("Зображення"), QStringLiteral("folder-pictures"), 3);
        addItem(QStringLiteral("Games"), QStringLiteral("Ігри"), QStringLiteral("applications-games"), 2);
        addItem(QStringLiteral("Run"), QStringLiteral("Команда «Виконати»"), QString(), 0);
        addItem(QStringLiteral("Computer"), QStringLiteral("Комп'ютер"), QStringLiteral("computer"), 2);
        addItem(QStringLiteral("Network"), QStringLiteral("Мережа"), QString(), 0);
        addItem(QStringLiteral("Music"), QStringLiteral("Музика"), QStringLiteral("folder-music"), 3);
        addItem(QStringLiteral("Recent Items"), QStringLiteral("Недавні документи"), QString(), 0);
        addItem(QStringLiteral("Home directory"), QStringLiteral("Особиста папка"), QStringLiteral("user-home"), 3);
        addItem(QStringLiteral("Control Panel"), QStringLiteral("Панель керування"), QStringLiteral("preferences-system"), 2);
        addItem(QStringLiteral("Devices and Printers"), QStringLiteral("Пристрої та принтери"), QString(), 0);
        addItem(QStringLiteral("Default Programs"), QStringLiteral("Програми за замовчуванням"), QString(), 0);
        m_list->addStretch(1);

        auto *size = new QGroupBox(QStringLiteral("Розмір меню «Пуск»"));
        auto *sg = new QGridLayout(size);
        auto *rowsLabel = new QLabel(QStringLiteral("Кількість недавно використаних програм для відображення:"));
        rowsLabel->setWordWrap(true);
        m_rows = new QSpinBox;
        m_rows->setRange(0, 30);
        m_rows->setValue(st.recentPrograms);
        m_rows->setFixedWidth(52);
        sg->addWidget(rowsLabel, 0, 0);
        sg->addWidget(m_rows, 0, 1, Qt::AlignRight | Qt::AlignVCenter);
        sg->setColumnStretch(0, 1);
        v->addWidget(size);

        auto *btns = new QHBoxLayout;
        auto *defaults = new QPushButton(QStringLiteral("Параметри за замовчуванням"));
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
        ok->setDefault(true);
        ok->setMinimumWidth(75);
        cancel->setMinimumWidth(75);
        btns->addWidget(defaults);
        btns->addStretch(1);
        btns->addWidget(ok);
        btns->addWidget(cancel);
        v->addLayout(btns);

        m_highlightNew = st.highlightNew;
        load(st.sidePanel);
        connect(defaults, &QPushButton::clicked, this, [this] {
            m_highlightNew = true;
            load(QJsonDocument::fromJson(kDefaultSidePanel.toUtf8()).object());
            m_rows->setValue(10);
        });
        connect(ok, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    State result() const
    {
        State st = m_state;
        QJsonObject side = st.sidePanel;
        for (const Entry &e : m_entries) {
            if (e.name == QLatin1String("__highlightNew")) {
                st.highlightNew = e.check->isChecked();
                continue;
            }
            side.remove(e.name);
            const int mode = e.check ? (e.check->isChecked() ? 1 : 0) : e.group->checkedId();
            if (mode > 0) {
                side.insert(e.name, mode);
            }
        }
        st.sidePanel = side;
        st.recentPrograms = m_rows->value();
        return st;
    }

private:
    struct Entry {
        QString name;
        QCheckBox *check = nullptr;
        QButtonGroup *group = nullptr;
    };

    void addItem(const QString &name, const QString &text, const QString &icon, int kind)
    {
        Entry e;
        e.name = name;
        if (kind == 0) {
            e.check = new QCheckBox(text);
            m_list->addWidget(e.check);
        } else {
            auto *head = new QWidget;
            auto *h = new QHBoxLayout(head);
            h->setContentsMargins(1, 1, 0, 1);
            h->setSpacing(4);
            auto *ic = new QLabel;
            ic->setPixmap(QIcon::fromTheme(icon).pixmap(16, 16));
            h->addWidget(ic);
            h->addWidget(new QLabel(text), 1);
            m_list->addWidget(head);
            e.group = new QButtonGroup(this);
            auto addRadio = [&](const QString &label, int id) {
                auto *r = new QRadioButton(label);
                r->setContentsMargins(0, 0, 0, 0);
                auto *row = new QHBoxLayout;
                row->setContentsMargins(22, 0, 0, 0);
                row->addWidget(r);
                m_list->addLayout(row);
                e.group->addButton(r, id);
            };
            addRadio(QStringLiteral("Не відображати цей елемент"), 0);
            if (kind == 3) {
                addRadio(QStringLiteral("Відображати як меню"), 2);
            }
            addRadio(QStringLiteral("Відображати як посилання"), 1);
        }
        m_entries.append(e);
    }

    void load(const QJsonObject &side)
    {
        for (const Entry &e : std::as_const(m_entries)) {
            if (e.name == QLatin1String("__highlightNew")) {
                e.check->setChecked(m_highlightNew);
                continue;
            }
            const int mode = side.contains(e.name) ? side.value(e.name).toInt(1) : 0;
            if (e.check) {
                e.check->setChecked(mode > 0);
            } else {
                QAbstractButton *b = e.group->button(mode);
                (b ? b : e.group->button(1))->setChecked(true);
            }
        }
    }

    State m_state;
    bool m_highlightNew = true;
    QVBoxLayout *m_list;
    QSpinBox *m_rows;
    QList<Entry> m_entries;
};

/* ---------------- Taskbar and Start Menu Properties ---------------- */

class PropertiesDialog : public QDialog
{
public:
    explicit PropertiesDialog(int tab, QWidget *parent = nullptr)
        : QDialog(parent)
        , m_old(readState())
        , m_state(m_old)
    {
        setWindowTitle(QStringLiteral("Властивості панелі завдань і меню «Пуск»"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-taskbar-props")));
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        setFixedWidth(500);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 9, 7, 9);
        auto *tabs = new QTabWidget;
        tabs->addTab(taskbarTab(), QStringLiteral("Панель завдань"));
        tabs->addTab(startTab(), QStringLiteral("Меню «Пуск»"));
        tabs->addTab(toolbarsTab(), QStringLiteral("Панелі інструментів"));
        tabs->setCurrentIndex(tab);
        v->addWidget(tabs, 1);
        auto *btns = new QHBoxLayout;
        btns->setContentsMargins(0, 4, 0, 0);
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
        m_apply = new QPushButton(QStringLiteral("Застосувати"));
        for (QPushButton *b : {ok, cancel, m_apply}) {
            b->setMinimumWidth(75);
            btns->addWidget(b);
        }
        ok->setDefault(true);
        m_apply->setEnabled(false);
        v->addLayout(btns);
        if (!m_old.valid) {
            tabs->setEnabled(false);
        }
        setFixedHeight(sizeHint().height()); // fixed size like Windows 7
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
    bool toolbarsChanged() const
    {
        if (!m_toolbarList) {
            return false;
        }
        for (int i = 0; i < m_toolbarList->count(); ++i) {
            const QListWidgetItem *it = m_toolbarList->item(i);
            if ((it->checkState() == Qt::Checked) != it->data(Qt::UserRole + 2).toBool()) {
                return true;
            }
        }
        return false;
    }

    void changed()
    {
        m_apply->setEnabled(m_old.valid && (!(m_state == m_old) || toolbarsChanged()));
    }

    void applyToolbars()
    {
        for (int i = 0; i < m_toolbarList->count(); ++i) {
            QListWidgetItem *it = m_toolbarList->item(i);
            const bool on = it->checkState() == Qt::Checked;
            if (on == it->data(Qt::UserRole + 2).toBool()) {
                continue;
            }
            const int id = it->data(Qt::UserRole + 1).toInt();
            if (on) {
                addToolbar(it->data(Qt::UserRole).toString());
            } else if (id >= 0) {
                removeToolbar(id);
            }
        }
        fillToolbars();
    }

    void fillToolbars()
    {
        m_toolbarList->blockSignals(true);
        m_toolbarList->clear();
        const QList<Toolbar> present = toolbars();
        auto add = [&](const QString &kind, const QString &text, int id, bool on) {
            auto *it = new QListWidgetItem(text, m_toolbarList);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
            it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
            it->setData(Qt::UserRole, kind);
            it->setData(Qt::UserRole + 1, id);
            it->setData(Qt::UserRole + 2, on);
        };
        for (const QString &kind : {QStringLiteral("address"), QStringLiteral("desktop")}) {
            int id = -1;
            for (const Toolbar &t : present) {
                if (t.kind == kind) {
                    id = t.id;
                }
            }
            add(kind, Toolbar{-1, kind, {}}.title(), id, id >= 0);
        }
        for (const Toolbar &t : present) { // folders added with "New toolbar..."
            if (t.kind == QLatin1String("folder")) {
                add(t.kind, t.title(), t.id, true);
            }
        }
        m_toolbarList->blockSignals(false);
    }

    QWidget *toolbarsTab()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(10, 10, 10, 10);
        v->setSpacing(4);
        v->addWidget(text(QStringLiteral("Виберіть панелі інструментів, які мають відображатися на панелі завдань.")));
        m_toolbarList = new QListWidget;
        m_toolbarList->setMinimumHeight(330);
        v->addWidget(m_toolbarList, 1);
        fillToolbars();
        connect(m_toolbarList, &QListWidget::itemChanged, this, [this] {
            changed();
        });
        return page;
    }

    void apply()
    {
        if (toolbarsChanged()) {
            applyToolbars();
        }
        if (!m_old.valid || m_state == m_old) {
            changed();
            return;
        }
        writeState(m_state, m_old);
        m_old = m_state;
        changed();
    }

    WrapCheck *check(const QString &text, bool on, std::function<void(bool)> set)
    {
        auto *c = new WrapCheck(text);
        c->box->setChecked(on);
        connect(c->box, &QCheckBox::toggled, this, [this, set](bool v) {
            set(v);
            changed();
        });
        return c;
    }

    QComboBox *combo(const QStringList &items, int current, std::function<void(int)> set)
    {
        auto *c = new QComboBox;
        c->addItems(items);
        c->setCurrentIndex(current);
        c->setFixedWidth(226);
        c->view()->setMinimumWidth(c->view()->sizeHintForColumn(0) + 12); // the open list shows the whole text
        connect(c, &QComboBox::currentIndexChanged, this, [this, set](int i) {
            set(i);
            changed();
        });
        return c;
    }

    static QLabel *text(const QString &t)
    {
        auto *l = new QLabel(t);
        l->setWordWrap(true);
        return l;
    }

    QWidget *taskbarTab()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(10, 10, 10, 10);
        v->setSpacing(8);

        auto *look = new QGroupBox(QStringLiteral("Оформлення панелі завдань"));
        auto *lv = new QVBoxLayout(look);
        lv->setSpacing(6);
        lv->addWidget(check(QStringLiteral("Закріпити панель завдань"), m_state.locked, [this](bool on) {
            m_state.locked = on;
        }));
        lv->addWidget(check(QStringLiteral("Автоматично приховувати панель завдань"), m_state.autoHide, [this](bool on) {
            m_state.autoHide = on;
        }));
        lv->addWidget(check(QStringLiteral("Використовувати маленькі значки"), m_state.smallIcons, [this](bool on) {
            m_state.smallIcons = on;
        }));
        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(8);
        grid->addWidget(text(QStringLiteral("Розташування панелі завдань на екрані:")), 0, 0);
        grid->addWidget(combo({QStringLiteral("Знизу"), QStringLiteral("Ліворуч"), QStringLiteral("Праворуч"), QStringLiteral("Згори")}, m_state.location,
                              [this](int i) {
                                  m_state.location = i;
                              }),
                        0, 1);
        grid->addWidget(new QLabel(QStringLiteral("Кнопки панелі завдань:")), 1, 0);
        grid->addWidget(combo({QStringLiteral("Завжди групувати, приховувати підписи"), QStringLiteral("Групувати, коли панель завдань заповнена"),
                               QStringLiteral("Ніколи не групувати")},
                              m_state.buttons,
                              [this](int i) {
                                  m_state.buttons = i;
                              }),
                        1, 1);
        grid->setColumnStretch(0, 1);
        lv->addSpacing(4);
        lv->addLayout(grid);
        v->addWidget(look);

        auto *tray = new QGroupBox(QStringLiteral("Область сповіщень"));
        auto *th = new QHBoxLayout(tray);
        th->addWidget(text(QStringLiteral("Настроювання значків і сповіщень, які відображатимуться в області сповіщень.")), 1);
        auto *customize = new QPushButton(QStringLiteral("Настроїти..."));
        th->addWidget(customize, 0, Qt::AlignVCenter);
        connect(customize, &QPushButton::clicked, this, [] {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--tray-icons")});
        });
        v->addWidget(tray);

        auto *peek = new QGroupBox(QStringLiteral("Попередній перегляд робочого столу за допомогою Aero Peek"));
        auto *pv = new QVBoxLayout(peek);
        pv->addWidget(text(QStringLiteral("Тимчасово переглядати робочий стіл, навівши вказівник миші на кнопку «Показати робочий стіл» у кінці панелі завдань.")));
        pv->addWidget(check(QStringLiteral("Використовувати Aero Peek для попереднього перегляду робочого столу"), m_state.peek, [this](bool on) {
            m_state.peek = on;
        }));
        v->addWidget(peek);
        v->addStretch(1);
        return page;
    }

    QWidget *startTab()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(10, 10, 10, 10);
        v->setSpacing(10);
        auto *top = new QHBoxLayout;
        top->addWidget(text(QStringLiteral("Щоб настроїти вигляд і поведінку посилань, значків і меню в меню «Пуск», натисніть кнопку «Настроїти».")), 1);
        auto *customize = new QPushButton(QStringLiteral("Настроїти..."));
        top->addWidget(customize, 0, Qt::AlignTop);
        v->addLayout(top);
        auto *powerRow = new QHBoxLayout;
        auto *powerLabel = new QLabel(QStringLiteral("Дія кнопки живлення:"));
        powerRow->addWidget(powerLabel);
        auto *power = combo({QStringLiteral("Змінити користувача"), QStringLiteral("Вийти з системи"), QStringLiteral("Заблокувати"), QStringLiteral("Перезавантаження"),
                             QStringLiteral("Сон"), QStringLiteral("Завершення роботи")},
                            m_state.power, [this](int i) {
                                m_state.power = i;
                            });
        power->setFixedWidth(160);
        powerRow->addWidget(power);
        powerRow->addStretch(1);
        v->addLayout(powerRow);

        auto *privacy = new QGroupBox(QStringLiteral("Конфіденційність"));
        auto *pv = new QVBoxLayout(privacy);
        pv->setSpacing(6);
        const int savedRows = Win7Tray::cache().value(QStringLiteral("Start/RecentPrograms"), 10).toInt();
        m_recent = check(QStringLiteral("Зберігати та відображати список нещодавно відкритих програм у меню «Пуск»"), m_state.recentPrograms > 0, [this, savedRows](bool on) {
            if (!on && m_state.recentPrograms > 0) {
                Win7Tray::cache().setValue(QStringLiteral("Start/RecentPrograms"), m_state.recentPrograms);
            }
            m_state.recentPrograms = on ? qMax(1, Win7Tray::cache().value(QStringLiteral("Start/RecentPrograms"), savedRows).toInt()) : 0;
        });
        pv->addWidget(m_recent);
        pv->addWidget(check(QStringLiteral("Зберігати та відображати список нещодавно відкритих елементів у меню «Пуск» і на панелі завдань"), m_state.recentItems,
                            [this](bool on) {
                                m_state.recentItems = on;
                            }));
        v->addWidget(privacy);
        v->addStretch(1);

        connect(customize, &QPushButton::clicked, this, [this] {
            CustomizeStartMenu dlg(m_state, this);
            if (dlg.exec() == QDialog::Accepted) {
                m_state = dlg.result();
                m_recent->box->blockSignals(true);
                m_recent->box->setChecked(m_state.recentPrograms > 0);
                m_recent->box->blockSignals(false);
                changed();
            }
        });
        return page;
    }

    State m_old;
    State m_state;
    QPushButton *m_apply;
    WrapCheck *m_recent = nullptr;
    QListWidget *m_toolbarList = nullptr;
};

inline void showProperties(int tab)
{
    auto *dlg = new PropertiesDialog(tab);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}
}

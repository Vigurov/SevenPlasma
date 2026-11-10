/*
 * Color Management (colorcpl.exe): Devices, All Profiles and Advanced, backed by colord (colormgr) and
 * colord-kde, which registers the monitors and puts the default profile of each on the screen.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "win7folderopts.h"
#include "win7header.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>

namespace Win7Color
{

using Win7FolderOpts::dlu;

template<typename T>
T *place(QWidget *parent, T *w, const QRect &r)
{
    w->setParent(parent);
    w->setGeometry(r);
    return w;
}

/** colormgr in the C locale, so its "Key: value" lines can be read */
inline QString colormgr(const QStringList &args, int *exitCode = nullptr)
{
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    env.insert(QStringLiteral("LANGUAGE"), QStringLiteral("C"));
    p.setProcessEnvironment(env);
    p.start(QStringLiteral("colormgr"), args);
    p.waitForFinished(10000);
    if (exitCode) {
        *exitCode = p.exitCode();
    }
    return QString::fromUtf8(p.readAllStandardOutput());
}

struct Profile {
    QString path; // colord object path
    QString id;   // "icc-…", as devices list their profiles
    QString title;
    QString file;
};

struct Device {
    QString path;
    QString name;
    QList<Profile> profiles; // the first is the default
};

/** "Key:     value" blocks separated by blank lines */
inline QList<QList<QPair<QString, QString>>> blocks(const QString &out)
{
    QList<QList<QPair<QString, QString>>> list;
    QList<QPair<QString, QString>> cur;
    QString lastKey;
    for (const QString &line : out.split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty()) {
            if (!cur.isEmpty()) {
                list << cur;
                cur.clear();
            }
            continue;
        }
        const int colon = line.indexOf(QLatin1Char(':'));
        if (!line.startsWith(QLatin1Char(' ')) && colon > 0) {
            lastKey = line.left(colon).trimmed();
            cur.append({lastKey, line.mid(colon + 1).trimmed()});
        } else {
            cur.append({lastKey + QStringLiteral("+"), line.trimmed()}); // continuation (a profile's file)
        }
    }
    if (!cur.isEmpty()) {
        list << cur;
    }
    return list;
}

inline QList<Profile> allProfiles()
{
    QList<Profile> list;
    for (const auto &b : blocks(colormgr({QStringLiteral("get-profiles")}))) {
        Profile p;
        for (const auto &kv : b) {
            if (kv.first == QLatin1String("Object Path")) {
                p.path = kv.second;
            } else if (kv.first == QLatin1String("Profile ID")) {
                p.id = kv.second;
            } else if (kv.first == QLatin1String("Title")) {
                p.title = kv.second;
            } else if (kv.first == QLatin1String("Filename")) {
                p.file = kv.second;
            }
        }
        if (!p.path.isEmpty()) {
            list << p;
        }
    }
    return list;
}

inline QList<Device> displays()
{
    const QList<Profile> profiles = allProfiles();
    auto byPath = [&profiles](const QString &path) {
        for (const Profile &p : profiles) {
            if (p.path == path || p.id == path || p.path.endsWith(QLatin1Char('/') + path)) {
                return p;
            }
        }
        return Profile{path, path, QString()};
    };
    QList<Device> list;
    for (const auto &b : blocks(colormgr({QStringLiteral("get-devices-by-kind"), QStringLiteral("display")}))) {
        Device d;
        QString vendor, model, output, embedded;
        for (const auto &kv : b) {
            if (kv.first == QLatin1String("Object Path")) {
                d.path = kv.second;
            } else if (kv.first == QLatin1String("Vendor")) {
                vendor = kv.second;
            } else if (kv.first == QLatin1String("Model")) {
                model = kv.second;
            } else if (kv.first == QLatin1String("Embedded")) {
                embedded = kv.second;
            } else if (kv.first == QLatin1String("Metadata") && kv.second.startsWith(QLatin1String("XRANDR_name="))) {
                output = kv.second.mid(12);
            } else if (kv.first.startsWith(QLatin1String("Profile ")) && !kv.first.endsWith(QLatin1Char('+'))) {
                d.profiles << byPath(kv.second);
            }
        }
        d.name = QStringLiteral("Дисплей: %1. %2 %3%4").arg(list.size() + 1).arg(vendor, model, embedded == QLatin1String("Yes") ? QStringLiteral(" — вбудований") : QString());
        if (!output.isEmpty()) {
            d.name += QStringLiteral(" (%1)").arg(output);
        }
        if (!d.path.isEmpty()) {
            list << d;
        }
    }
    return list;
}

class ColorManagement : public QDialog
{
public:
    ColorManagement()
    {
        setWindowTitle(QStringLiteral("Керування кольорами"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-cpi-color")));
        setAttribute(Qt::WA_DeleteOnClose);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 7, 7, 7);
        auto *tabs = new QTabWidget;
        tabs->addTab(devicesPage(), QStringLiteral("Пристрої"));
        tabs->addTab(allPage(), QStringLiteral("Усі профілі"));
        tabs->addTab(advancedPage(), QStringLiteral("Додатково"));
        v->addWidget(tabs);
        auto *bar = new QHBoxLayout;
        bar->addStretch(1);
        auto *close = new QPushButton(QStringLiteral("Закрити"));
        close->setMinimumWidth(75);
        bar->addWidget(close);
        v->addLayout(bar);
        connect(close, &QPushButton::clicked, this, &QDialog::close);
        setFixedSize(sizeHint());
        reload();
    }

private:
    QWidget *page()
    {
        auto *p = new QWidget;
        p->setFixedSize(dlu(0, 0, 300, 230).size());
        return p;
    }

    QTreeWidget *list(QWidget *p, const QRect &r)
    {
        auto *t = place(p, new QTreeWidget, r);
        t->setRootIsDecorated(false);
        t->setHeaderLabels({QStringLiteral("Ім'я"), QStringLiteral("Ім'я файлу")});
        Win7HeaderView::install(t);
        t->setColumnWidth(0, r.width() / 2);
        return t;
    }

    QWidget *devicesPage()
    {
        QWidget *p = page();
        place(p, new QLabel(QStringLiteral("Пристрій:")), dlu(7, 9, 36, 10));
        m_device = place(p, new QComboBox, dlu(46, 7, 247, 14));
        m_mine = place(p, new QCheckBox(QStringLiteral("Використовувати мої параметри для цього пристрою")), dlu(46, 24, 247, 10));
        m_mine->setChecked(true);
        place(p, new QLabel(QStringLiteral("Профілі, пов'язані з цим пристроєм:")), dlu(7, 42, 286, 10));
        m_assoc = list(p, dlu(7, 54, 286, 130));
        auto *add = place(p, new QPushButton(QStringLiteral("Додати...")), dlu(7, 190, 60, 14));
        m_remove = place(p, new QPushButton(QStringLiteral("Видалити")), dlu(71, 190, 60, 14));
        m_default = place(p, new QPushButton(QStringLiteral("Установити як профіль за замовчуванням")), dlu(135, 190, 158, 14));
        connect(m_device, &QComboBox::currentIndexChanged, this, [this] { showDevice(); });
        connect(m_mine, &QCheckBox::toggled, this, [this] { syncButtons(); });
        connect(m_assoc, &QTreeWidget::currentItemChanged, this, [this] { syncButtons(); });
        connect(add, &QPushButton::clicked, this, [this] { associate(); });
        connect(m_remove, &QPushButton::clicked, this, [this] {
            if (auto *it = m_assoc->currentItem()) {
                colormgr({QStringLiteral("device-remove-profile"), currentDevice().path, it->data(0, Qt::UserRole).toString()});
                reload();
            }
        });
        connect(m_default, &QPushButton::clicked, this, [this] {
            if (auto *it = m_assoc->currentItem()) {
                colormgr({QStringLiteral("device-make-profile-default"), currentDevice().path, it->data(0, Qt::UserRole).toString()});
                reload();
            }
        });
        return p;
    }

    QWidget *allPage()
    {
        QWidget *p = page();
        place(p, new QLabel(QStringLiteral("Профілі кольорів, установлені на цьому комп'ютері:")), dlu(7, 9, 286, 10));
        m_all = list(p, dlu(7, 22, 286, 162));
        auto *add = place(p, new QPushButton(QStringLiteral("Додати...")), dlu(7, 190, 60, 14));
        auto *del = place(p, new QPushButton(QStringLiteral("Видалити")), dlu(71, 190, 60, 14));
        connect(add, &QPushButton::clicked, this, [this] { importProfile(); });
        connect(del, &QPushButton::clicked, this, [this] {
            auto *it = m_all->currentItem();
            if (!it) {
                return;
            }
            const QString file = it->text(1);
            if (!file.startsWith(QDir::homePath())) {
                QMessageBox::information(this, windowTitle(), QStringLiteral("Системний профіль «%1» не можна видалити.").arg(it->text(0)));
                return;
            }
            if (QMessageBox::question(this, windowTitle(), QStringLiteral("Видалити профіль «%1»?").arg(it->text(0))) == QMessageBox::Yes) {
                colormgr({QStringLiteral("delete-profile"), it->data(0, Qt::UserRole).toString()});
                QFile::remove(file);
                reload();
            }
        });
        return p;
    }

    QWidget *advancedPage()
    {
        QWidget *p = page();
        place(p, new QGroupBox(QStringLiteral("Калібрування дисплея")), dlu(7, 7, 286, 54));
        place(p, new QLabel(QStringLiteral("Калібрування дисплея допомагає точно відтворювати кольори. Профіль, створений колориметром, можна додати на вкладці «Усі профілі».")),
              dlu(14, 18, 188, 36))
            ->setWordWrap(true);
        place(p, new QPushButton(QStringLiteral("Калібрувати дисплей")), dlu(206, 26, 80, 14))->setEnabled(false);
        place(p, new QGroupBox(QStringLiteral("Як працює керування кольорами")), dlu(7, 68, 286, 60));
        place(p, new QLabel(QStringLiteral("Профіль за замовчуванням кожного монітора застосовує служба кольорів (colord). Програми, що підтримують керування кольорами, "
                                           "використовують його для точного відображення зображень.")),
              dlu(14, 80, 270, 44))
            ->setWordWrap(true);
        return p;
    }

    Device currentDevice() const
    {
        return m_devices.value(qMax(0, m_device->currentIndex()));
    }

    void reload()
    {
        const int idx = qMax(0, m_device->currentIndex());
        m_devices = displays();
        m_device->blockSignals(true);
        m_device->clear();
        for (const Device &d : std::as_const(m_devices)) {
            m_device->addItem(QIcon::fromTheme(QStringLiteral("video-display")), d.name);
        }
        m_device->setCurrentIndex(qMin(idx, int(m_devices.size()) - 1));
        m_device->blockSignals(false);
        showDevice();
        m_all->clear();
        auto *group = new QTreeWidgetItem(m_all, {QStringLiteral("Профілі ICC")});
        QFont b = group->font(0);
        b.setBold(true);
        group->setFont(0, b);
        group->setFirstColumnSpanned(true);
        for (const Profile &pr : allProfiles()) {
            auto *it = new QTreeWidgetItem(group, {pr.title, pr.file});
            it->setData(0, Qt::UserRole, pr.path);
        }
        m_all->expandAll();
    }

    void showDevice()
    {
        m_assoc->clear();
        const Device d = currentDevice();
        bool first = true;
        for (const Profile &pr : d.profiles) {
            auto *it = new QTreeWidgetItem(m_assoc, {first ? pr.title + QStringLiteral(" (за замовчуванням)") : pr.title, QFileInfo(pr.file).fileName()});
            it->setData(0, Qt::UserRole, pr.path);
            first = false;
        }
        syncButtons();
    }

    void syncButtons()
    {
        const bool on = m_mine->isChecked() && !m_devices.isEmpty();
        const bool sel = on && m_assoc->currentItem();
        m_remove->setEnabled(sel);
        m_default->setEnabled(sel && m_assoc->indexOfTopLevelItem(m_assoc->currentItem()) > 0);
        m_assoc->setEnabled(on);
    }

    /** "Associate Color Profile": the installed profiles, or Browse for an .icc file */
    void associate()
    {
        QDialog d(this);
        d.setWindowTitle(QStringLiteral("Пов'язати профіль кольорів"));
        auto *v = new QVBoxLayout(&d);
        v->addWidget(new QLabel(QStringLiteral("Виберіть профіль, який слід пов'язати з пристроєм:")));
        auto *t = new QTreeWidget;
        t->setRootIsDecorated(false);
        t->setHeaderLabels({QStringLiteral("Ім'я"), QStringLiteral("Ім'я файлу")});
        Win7HeaderView::install(t);
        for (const Profile &pr : allProfiles()) {
            auto *it = new QTreeWidgetItem(t, {pr.title, QFileInfo(pr.file).fileName()});
            it->setData(0, Qt::UserRole, pr.path);
        }
        t->setColumnWidth(0, 260);
        v->addWidget(t);
        auto *bar = new QHBoxLayout;
        auto *browse = new QPushButton(QStringLiteral("Огляд..."));
        bar->addWidget(browse);
        bar->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
        bar->addWidget(ok);
        bar->addWidget(cancel);
        v->addLayout(bar);
        connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
        connect(t, &QTreeWidget::itemDoubleClicked, &d, &QDialog::accept);
        QString chosen;
        connect(browse, &QPushButton::clicked, &d, [this, &d, &chosen] {
            chosen = importProfile();
            if (!chosen.isEmpty()) {
                d.accept();
            }
        });
        d.resize(520, 360);
        if (d.exec() != QDialog::Accepted) {
            return;
        }
        if (chosen.isEmpty() && t->currentItem()) {
            chosen = t->currentItem()->data(0, Qt::UserRole).toString();
        }
        if (!chosen.isEmpty()) {
            colormgr({QStringLiteral("device-add-profile"), currentDevice().path, chosen});
            reload();
        }
    }

    /** an .icc/.icm file into ~/.local/share/icc and colord; returns its object path */
    QString importProfile()
    {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Установлення профілю"), QDir::homePath(), QStringLiteral("Профілі ICC (*.icc *.icm *.ICC *.ICM)"));
        if (f.isEmpty()) {
            return {};
        }
        int code = 0;
        const QString out = colormgr({QStringLiteral("import-profile"), f}, &code);
        reload();
        for (const auto &b : blocks(out)) {
            for (const auto &kv : b) {
                if (kv.first == QLatin1String("Object Path")) {
                    return kv.second;
                }
            }
        }
        // already installed: find it by its file name
        for (const Profile &pr : allProfiles()) {
            if (QFileInfo(pr.file).fileName() == QFileInfo(f).fileName()) {
                return pr.path;
            }
        }
        if (code != 0) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Не вдалося встановити профіль «%1».").arg(QFileInfo(f).fileName()));
        }
        return {};
    }

    QComboBox *m_device = nullptr;
    QCheckBox *m_mine = nullptr;
    QTreeWidget *m_assoc = nullptr;
    QTreeWidget *m_all = nullptr;
    QPushButton *m_remove = nullptr;
    QPushButton *m_default = nullptr;
    QList<Device> m_devices;
};

} // namespace Win7Color

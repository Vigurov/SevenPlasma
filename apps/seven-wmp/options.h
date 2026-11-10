#pragma once
/*
 * Tools > Options... (WMP 12 "Параметри") and the library locations dialog ("Розташування бібліотеки ...").
 */
#include "library.h"
#include "res.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>

namespace Opt
{
inline bool value(const QString &key, bool def)
{
    return QSettings().value(QStringLiteral("options/") + key, def).toBool();
}
}

/** Manage libraries > Music / Videos / Pictures: the folders the library watches */
class LocationsDialog : public QDialog
{
public:
    LocationsDialog(MediaItem::Kind kind, QWidget *parent)
        : QDialog(parent)
        , m_kind(kind)
    {
        const QString name = kind == MediaItem::Audio ? QStringLiteral("Музика") : kind == MediaItem::Video ? QStringLiteral("Відео") : QStringLiteral("Зображення");
        setWindowTitle(QStringLiteral("Розташування бібліотеки \"%1\"").arg(name));
        auto *l = new QVBoxLayout(this);
        auto *head = new QLabel(QStringLiteral("<b>Змінення способу збирання вмісту бібліотеки \"%1\"</b><br>"
                                               "Після додавання папки до бібліотеки її файли відображатимуться в програвачі Windows Media.")
                                    .arg(name),
                                this);
        head->setWordWrap(true);
        l->addWidget(head);
        auto *row = new QHBoxLayout;
        auto *lbl = new QLabel(QStringLiteral("Розташування бібліотеки"), this);
        row->addWidget(lbl);
        row->addStretch(1);
        auto *add = new QPushButton(QStringLiteral("Додати..."), this);
        auto *remove = new QPushButton(QStringLiteral("Видалити"), this);
        row->addWidget(add);
        row->addWidget(remove);
        l->addLayout(row);
        m_list = new QListWidget(this);
        m_list->setIconSize(QSize(32, 32));
        for (const QString &f : Library::folders(kind)) {
            addFolder(f);
        }
        l->addWidget(m_list, 1);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("OK"));
        bb->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Скасувати"));
        l->addWidget(bb);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(add, &QPushButton::clicked, this, [this] {
            const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Додати папку до бібліотеки"), QDir::homePath());
            if (!d.isEmpty()) {
                addFolder(d);
            }
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            delete m_list->currentItem();
        });
        resize(520, 380);
    }
    void accept() override
    {
        QStringList folders;
        for (int i = 0; i < m_list->count(); ++i) {
            folders << m_list->item(i)->data(Qt::UserRole).toString();
        }
        Library::setFolders(m_kind, folders);
        QDialog::accept();
    }

private:
    void addFolder(const QString &f)
    {
        auto *it = new QListWidgetItem(QIcon::fromTheme(QStringLiteral("folder")), QFileInfo(f).fileName() + QStringLiteral("\n") + f, m_list);
        it->setData(Qt::UserRole, f);
    }
    MediaItem::Kind m_kind;
    QListWidget *m_list;
};

class OptionsDialog : public QDialog
{
public:
    explicit OptionsDialog(QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("Параметри"));
        auto *l = new QVBoxLayout(this);
        auto *tabs = new QTabWidget(this);
        l->addWidget(tabs);
        tabs->addTab(playerTab(), QStringLiteral("Програвач"));
        tabs->addTab(simpleTab(QStringLiteral("Укажіть, де зберігати скопійовану музику, і змініть параметри копіювання."),
                               {QStringLiteral("Копіювати музику до цього розташування: ") + QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
                                QStringLiteral("Формат: MP3"), QStringLiteral("Якість звуку: 192 кбіт/с")},
                               true),
                     QStringLiteral("Копіювання музики"));
        tabs->addTab(simpleTab(QStringLiteral("Укажіть параметри для пристроїв і динаміків."), {QStringLiteral("Пристрої: динаміки, дисплей")}, true), QStringLiteral("Пристрої"));
        tabs->addTab(simpleTab(QStringLiteral("Ці параметри визначають спосіб записування аудіо-компакт-дисків і дисків з даними."),
                               {QStringLiteral("Швидкість записування: найвища"), QStringLiteral("Застосовувати вирівнювання гучності для доріжок")}, true),
                     QStringLiteral("Записування"));
        tabs->addTab(performanceTab(), QStringLiteral("Продуктивність"));
        tabs->addTab(libraryTab(), QStringLiteral("Бібліотека"));
        tabs->addTab(simpleTab(QStringLiteral("Модулі розширюють можливості програвача Windows Media."), {QStringLiteral("Категорія: Візуалізація"), QStringLiteral("Обкладинка альбому, Смуги та хвилі")}, true),
                     QStringLiteral("Модулі"));
        tabs->addTab(privacyTab(), QStringLiteral("Конфіденційність"));
        tabs->addTab(simpleTab(QStringLiteral("Виберіть, чи може вміст використовувати сценарії та чи дозволено запускати елементи керування ActiveX."),
                               {QStringLiteral("Запускати команди сценаріїв, якщо вони присутні"), QStringLiteral("Відтворювати розширений вміст, що використовує веб-сторінки, без запиту")}, false),
                     QStringLiteral("Безпека"));
        tabs->addTab(simpleTab(QStringLiteral("Налаштуйте параметри протоколів для потокового мультимедіа."), {QStringLiteral("Протоколи: RTSP/UDP, RTSP/TCP, HTTP"), QStringLiteral("Проксі-сервер: використовувати параметри браузера")}, true),
                     QStringLiteral("Мережа"));

        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply | QDialogButtonBox::Help, this);
        bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("OK"));
        bb->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Скасувати"));
        bb->button(QDialogButtonBox::Apply)->setText(QStringLiteral("Застосувати"));
        bb->button(QDialogButtonBox::Help)->setText(QStringLiteral("Довідка"));
        l->addWidget(bb);
        connect(bb, &QDialogButtonBox::accepted, this, [this] {
            apply();
            accept();
        });
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &OptionsDialog::apply);
        setFixedSize(520, 560);
    }

    std::function<void(MediaItem::Kind)> manageLibrary;

private:
    QCheckBox *check(QWidget *parent, QLayout *l, const QString &text, const QString &key, bool def)
    {
        auto *c = new QCheckBox(text, parent);
        c->setChecked(Opt::value(key, def));
        l->addWidget(c);
        m_checks.insert(key, c);
        return c;
    }
    QWidget *header(QWidget *parent, int iconGroup, const QString &text)
    {
        auto *w = new QWidget(parent);
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 6);
        auto *ic = new QLabel(w);
        ic->setPixmap(Res::icon(iconGroup).pixmap(32, 32));
        h->addWidget(ic, 0, Qt::AlignTop);
        auto *t = new QLabel(text, w);
        t->setWordWrap(true);
        h->addWidget(t, 1);
        return w;
    }
    QWidget *playerTab()
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->addWidget(header(w, 102, QStringLiteral("Укажіть параметри оновлення програвача та інші параметри.")));
        auto *upd = new QGroupBox(QStringLiteral("Автоматичні оновлення"), w);
        auto *ul = new QVBoxLayout(upd);
        ul->addWidget(new QLabel(QStringLiteral("Перевіряти наявність оновлень:"), upd));
        auto *grp = new QButtonGroup(w);
        const int freq = QSettings().value(QStringLiteral("options/updates"), 1).toInt();
        const QStringList names{QStringLiteral("Раз на день"), QStringLiteral("Раз на тиждень"), QStringLiteral("Раз на місяць")};
        for (int i = 0; i < 3; ++i) {
            auto *r = new QRadioButton(names[i], upd);
            r->setChecked(i == freq);
            grp->addButton(r, i);
            ul->addWidget(r);
        }
        m_updates = grp;
        l->addWidget(upd);
        auto *set = new QGroupBox(QStringLiteral("Параметри програвача"), w);
        auto *sl = new QVBoxLayout(set);
        check(set, sl, QStringLiteral("Зберігати режим поточного відтворення поверх інших вікон"), QStringLiteral("onTop"), false);
        check(set, sl, QStringLiteral("Дозволити заставку під час відтворення"), QStringLiteral("screensaver"), false);
        check(set, sl, QStringLiteral("Додавати до бібліотеки локальні файли мультимедіа під час відтворення"), QStringLiteral("addPlayed"), true);
        check(set, sl, QStringLiteral("Додавати до бібліотеки віддалені файли мультимедіа під час відтворення"), QStringLiteral("addRemote"), false);
        check(set, sl, QStringLiteral("Підключатися до Інтернету (перевизначає інші команди)"), QStringLiteral("internet"), false);
        check(set, sl, QStringLiteral("Зупиняти відтворення під час перемикання користувачів"), QStringLiteral("stopOnSwitch"), true);
        check(set, sl, QStringLiteral("Дозволити автоматичне приховування елементів керування відтворенням"), QStringLiteral("autoHide"), true);
        check(set, sl, QStringLiteral("Зберігати оцінки у файлах мультимедіа"), QStringLiteral("ratingsInFiles"), false);
        l->addWidget(set);
        l->addStretch(1);
        return w;
    }
    QWidget *performanceTab()
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->addWidget(header(w, 466, QStringLiteral("Ці параметри дають змогу налаштувати продуктивність програвача.")));
        auto *buf = new QGroupBox(QStringLiteral("Буферизація мережі"), w);
        auto *bl = new QVBoxLayout(buf);
        auto *r1 = new QRadioButton(QStringLiteral("Стандартна буферизація (рекомендовано)"), buf);
        r1->setChecked(true);
        bl->addWidget(r1);
        bl->addWidget(new QRadioButton(QStringLiteral("Буферизувати 5 с вмісту"), buf));
        l->addWidget(buf);
        auto *v = new QGroupBox(QStringLiteral("Прискорення відео"), w);
        auto *vl = new QVBoxLayout(v);
        auto *slider = new QSlider(Qt::Horizontal, v);
        slider->setRange(0, 2);
        slider->setValue(2);
        slider->setTickPosition(QSlider::TicksBelow);
        auto *sr = new QHBoxLayout;
        sr->addWidget(new QLabel(QStringLiteral("Немає"), v));
        sr->addWidget(slider, 1);
        sr->addWidget(new QLabel(QStringLiteral("Повне"), v));
        vl->addLayout(sr);
        l->addWidget(v);
        auto *g = new QGroupBox(QStringLiteral("Видимість відео"), w);
        auto *gl = new QVBoxLayout(g);
        check(g, gl, QStringLiteral("Вмикати показ субтитрів для DVD-дисків і відео, якщо доступні"), QStringLiteral("subtitles"), false);
        check(g, gl, QStringLiteral("Підганяти відео до розміру програвача, якщо відео більше"), QStringLiteral("fitVideo"), true);
        l->addWidget(g);
        l->addStretch(1);
        return w;
    }
    QWidget *libraryTab()
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->addWidget(header(w, 29513, QStringLiteral("Укажіть параметри для бібліотеки мультимедіа.")));
        auto *row = new QHBoxLayout;
        auto *music = new QPushButton(QStringLiteral("Керування бібліотекою музики..."), w);
        auto *video = new QPushButton(QStringLiteral("Керування бібліотекою відео..."), w);
        row->addWidget(music);
        row->addWidget(video);
        l->addLayout(row);
        connect(music, &QPushButton::clicked, this, [this] {
            if (manageLibrary) {
                manageLibrary(MediaItem::Audio);
            }
        });
        connect(video, &QPushButton::clicked, this, [this] {
            if (manageLibrary) {
                manageLibrary(MediaItem::Video);
            }
        });
        auto *g = new QGroupBox(QStringLiteral("Параметри бібліотеки"), w);
        auto *gl = new QVBoxLayout(g);
        check(g, gl, QStringLiteral("Видаляти файли з комп'ютера під час видалення з бібліотеки"), QStringLiteral("deleteFiles"), false);
        check(g, gl, QStringLiteral("Автоматично знаходити обкладинки альбомів"), QStringLiteral("findArt"), true);
        check(g, gl, QStringLiteral("Отримувати додаткові відомості з Інтернету"), QStringLiteral("internetInfo"), false);
        check(g, gl, QStringLiteral("Зберігати оцінки у файлах мультимедіа"), QStringLiteral("ratingsInFiles2"), false);
        l->addWidget(g);
        l->addStretch(1);
        return w;
    }
    QWidget *privacyTab()
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->addWidget(header(w, 1151, QStringLiteral("Укажіть параметри конфіденційності.")));
        auto *g = new QGroupBox(QStringLiteral("Покращені можливості відтворення та пристроїв"), w);
        auto *gl = new QVBoxLayout(g);
        check(g, gl, QStringLiteral("Показувати відомості про мультимедіа з Інтернету"), QStringLiteral("mediaInfo"), false);
        check(g, gl, QStringLiteral("Оновлювати музичні файли, отримуючи відомості про мультимедіа з Інтернету"), QStringLiteral("updateMusic"), false);
        l->addWidget(g);
        auto *h = new QGroupBox(QStringLiteral("Журнал"), w);
        auto *hl = new QVBoxLayout(h);
        check(h, hl, QStringLiteral("Зберігати й відображати список нещодавно відкритих файлів"), QStringLiteral("history"), true);
        auto *clear = new QPushButton(QStringLiteral("Очистити журнал"), h);
        hl->addWidget(clear, 0, Qt::AlignLeft);
        connect(clear, &QPushButton::clicked, this, [] {
            QSettings().remove(QStringLiteral("recent"));
        });
        l->addWidget(h);
        l->addStretch(1);
        return w;
    }
    QWidget *simpleTab(const QString &head, const QStringList &lines, bool disabled)
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->addWidget(header(w, 102, head));
        auto *g = new QGroupBox(w);
        auto *gl = new QVBoxLayout(g);
        for (const QString &t : lines) {
            auto *c = new QCheckBox(t, g);
            c->setChecked(true);
            c->setEnabled(!disabled);
            gl->addWidget(c);
        }
        l->addWidget(g);
        auto *note = new QLabel(QStringLiteral("<i>Ці параметри недоступні на цьому комп'ютері.</i>"), w);
        note->setVisible(disabled);
        l->addWidget(note);
        l->addStretch(1);
        return w;
    }
    void apply()
    {
        QSettings s;
        for (auto it = m_checks.cbegin(); it != m_checks.cend(); ++it) {
            s.setValue(QStringLiteral("options/") + it.key(), it.value()->isChecked());
        }
        s.setValue(QStringLiteral("options/updates"), m_updates->checkedId());
        if (onApply) {
            onApply();
        }
    }

public:
    std::function<void()> onApply;

private:
    QHash<QString, QCheckBox *> m_checks;
    QButtonGroup *m_updates = nullptr;
};

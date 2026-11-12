#pragma once
/*
 * Windows Help and Support (HelpPane.exe of Windows 7): glass at the top with the green Back / Forward
 * buttons, "Search Help" and the Home / Print / Browse Help / Ask / Options buttons, white Help pages, "Offline
 * Help" at the bottom. The pictures are HelpPane.exe's own (~/.local/share/seven-control/helppane); the pages
 * are ours, about using this Windows 7 look on Linux, in English and Ukrainian.
 *
 *   seven-control --help-support [page]
 */
#include "win7chrome.h"
#include "win7i18n.h"

#include <AeroQt/insetwindow.h>
#include <AeroQt/navbuttons.h>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QPrintDialog>
#include <QPrinter>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTextDocumentFragment>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace Win7Help
{
using Win7I18n::tr7;

inline bool ukrainian()
{
    const QStringList s = Win7I18n::keySuffixes();
    return !s.isEmpty() && s.first().startsWith(QLatin1String("uk"));
}

inline QPixmap pic(const QString &name)
{
    return QPixmap(QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-control/helppane/") + name + QStringLiteral(".png")));
}

struct Page {
    QString id, title, body; // body: HTML; links "page:<id>" go to other pages, "run:<args>" open our windows
};

/** the pages, in the UI language */
inline const QList<Page> &pages()
{
    static QList<Page> list;
    if (!list.isEmpty()) {
        return list;
    }
    const bool uk = ukrainian();
    auto add = [&](const char *id, const QString &en, const QString &ukTitle, const QString &enBody, const QString &ukBody) {
        list.append({QString::fromLatin1(id), uk ? ukTitle : en, uk ? ukBody : enBody});
    };
    add("start", QStringLiteral("How to get started with your computer"), QStringLiteral("Як почати роботу з комп'ютером"),
        QStringLiteral("<p>This computer runs Linux with the look and the programs of Windows 7. Most things are where Windows 7 has them.</p>"
                       "<ul><li><b>Start menu</b>: click the round Start button or press the Windows logo key. Type in the search box to find a program, a setting or a file.</li>"
                       "<li><b>Taskbar</b>: open programs have buttons on it; pin a program by dragging it there. Right-click a button for its Jump List.</li>"
                       "<li><b>Control Panel</b>: every setting of the computer. <a href=\"run:\">Open Control Panel</a></li>"
                       "<li><b>Personalize</b>: right-click the desktop and choose Personalize to change the theme, the background, the window color and the sounds. <a href=\"run:--personalize\">Open Personalization</a></li>"
                       "<li><b>Programs</b>: install new programs with the software manager of your Linux distribution; they appear in the Start menu.</li></ul>"
                       "<p>See also: <a href=\"page:basics\">Windows Basics</a>, <a href=\"page:keys\">Keyboard shortcuts</a>.</p>"),
        QStringLiteral("<p>На цьому комп'ютері працює Linux з виглядом і програмами Windows 7. Майже все розташовано там, де й у Windows 7.</p>"
                       "<ul><li><b>Меню «Пуск»</b>: клацніть круглу кнопку «Пуск» або натисніть клавішу з емблемою Windows. Почніть вводити текст у полі пошуку, щоб знайти програму, параметр або файл.</li>"
                       "<li><b>Панель завдань</b>: на ній кнопки відкритих програм; програму можна закріпити, перетягнувши її туди. Клацніть кнопку правою кнопкою миші, щоб відкрити список переходів.</li>"
                       "<li><b>Панель керування</b>: усі параметри комп'ютера. <a href=\"run:\">Відкрити панель керування</a></li>"
                       "<li><b>Персоналізація</b>: клацніть робочий стіл правою кнопкою миші й виберіть «Персоналізація», щоб змінити тему, фон, колір вікон і звуки. <a href=\"run:--personalize\">Відкрити персоналізацію</a></li>"
                       "<li><b>Програми</b>: нові програми встановлюються менеджером програм вашого дистрибутива Linux; вони з'являються в меню «Пуск».</li></ul>"
                       "<p>Див. також: <a href=\"page:basics\">Основи Windows</a>, <a href=\"page:keys\">Сполучення клавіш</a>.</p>"));
    add("basics", QStringLiteral("Windows Basics"), QStringLiteral("Основи Windows"),
        QStringLiteral("<h3>Windows</h3><p>Drag a window to the left or right edge of the screen to fill half of it, to the top to maximize it. Shake a window to minimize all the others. "
                       "Point at the button at the right end of the taskbar to see the desktop through the windows (Aero Peek).</p>"
                       "<h3>Files and folders</h3><p>Windows Explorer shows your files: Documents, Pictures, Music and Videos are in Libraries; disks and USB drives are in Computer. "
                       "Deleted files go to the Recycle Bin until you empty it.</p>"
                       "<h3>Gadgets</h3><p>Right-click the desktop and choose Gadgets to put a clock, a calendar, the weather and others on the desktop.</p>"
                       "<p>More: <a href=\"page:desktop\">The desktop</a>, <a href=\"page:files\">Files and folders</a>, <a href=\"page:programs\">Programs</a>.</p>"),
        QStringLiteral("<h3>Вікна</h3><p>Перетягніть вікно до лівого або правого краю екрана, щоб воно зайняло його половину, або до верхнього краю, щоб розгорнути його. Струсніть вікно, щоб згорнути всі інші. "
                       "Наведіть вказівник на кнопку в правому кінці панелі завдань, щоб побачити робочий стіл крізь вікна (Aero Peek).</p>"
                       "<h3>Файли та папки</h3><p>Провідник Windows показує ваші файли: «Документи», «Зображення», «Музика» й «Відео» — у бібліотеках; диски та USB-накопичувачі — у вікні «Комп'ютер». "
                       "Видалені файли потрапляють до кошика, доки його не очистити.</p>"
                       "<h3>Гаджети</h3><p>Клацніть робочий стіл правою кнопкою миші й виберіть «Гаджети», щоб розмістити на ньому годинник, календар, погоду тощо.</p>"
                       "<p>Докладніше: <a href=\"page:desktop\">Робочий стіл</a>, <a href=\"page:files\">Файли та папки</a>, <a href=\"page:programs\">Програми</a>.</p>"));
    add("desktop", QStringLiteral("The desktop and personalization"), QStringLiteral("Робочий стіл і персоналізація"),
        QStringLiteral("<p>Right-click the desktop for View, Sort by, New, Screen resolution, Gadgets and Personalize. "
                       "The icons Computer, your files, Network, Recycle Bin and Control Panel are chosen in Personalize &gt; Change desktop icons. <a href=\"run:--desktop-icons\">Change desktop icons</a></p>"
                       "<p>Window color, the background (also a slide show), the screen saver, the sounds and the mouse pointers are in <a href=\"run:--personalize\">Personalization</a>.</p>"),
        QStringLiteral("<p>Клацніть робочий стіл правою кнопкою миші: «Вигляд», «Сортувати за», «Створити», «Роздільна здатність екрана», «Гаджети» й «Персоналізація». "
                       "Значки «Комп'ютер», ваші файли, «Мережа», «Кошик» і «Панель керування» вибираються в розділі «Персоналізація» &gt; «Змінити значки робочого стола». <a href=\"run:--desktop-icons\">Змінити значки робочого стола</a></p>"
                       "<p>Колір вікон, фон (також слайд-шоу), заставка, звуки й вказівники миші — у розділі <a href=\"run:--personalize\">Персоналізація</a>.</p>"));
    add("files", QStringLiteral("Files and folders"), QStringLiteral("Файли та папки"),
        QStringLiteral("<p>Open Windows Explorer from the taskbar or with Windows logo key+E. Copy with Ctrl+C and paste with Ctrl+V, rename with F2, delete with Delete. "
                       "Search from the box at the top right of each window.</p>"
                       "<p>Files are kept in your home folder; other disks and USB drives appear in Computer when they are connected (AutoPlay asks what to do with them).</p>"),
        QStringLiteral("<p>Відкрийте провідник Windows із панелі завдань або клавішами емблема Windows+E. Копіюйте Ctrl+C і вставляйте Ctrl+V, перейменовуйте F2, видаляйте клавішею Delete. "
                       "Шукайте з поля у правому верхньому куті кожного вікна.</p>"
                       "<p>Файли зберігаються у вашій домашній папці; інші диски й USB-накопичувачі з'являються у вікні «Комп'ютер», щойно їх підключено (автозапуск запитає, що з ними зробити).</p>"));
    add("programs", QStringLiteral("Programs that come with this computer"), QStringLiteral("Програми, що постачаються з комп'ютером"),
        QStringLiteral("<p>Calculator, Notepad, Paint, Snipping Tool (Print Screen), Windows Media Player, Windows Photo Viewer, Character Map, the games (Solitaire, FreeCell, Minesweeper, Chess Titans and others), "
                       "Task Manager (Ctrl+Shift+Esc) and the system tools of Control Panel are all in the Start menu.</p>"
                       "<p>Programs for Windows do not run here by themselves; many have a Linux version in the software manager.</p>"),
        QStringLiteral("<p>Калькулятор, Блокнот, Paint, Ножиці (Print Screen), Програвач Windows Media, Засіб перегляду фотографій Windows, Таблиця символів, ігри («Косинка», «Вільна комірка», «Сапер», Chess Titans тощо), "
                       "Диспетчер завдань (Ctrl+Shift+Esc) і системні засоби панелі керування — усе це в меню «Пуск».</p>"
                       "<p>Програми для Windows тут самі не запускаються; у багатьох є версія для Linux у менеджері програм.</p>"));
    add("hardware", QStringLiteral("Displays, projectors and devices"), QStringLiteral("Екрани, проектори та пристрої"),
        QStringLiteral("<p>To connect a projector or a second monitor, plug it in and press Windows logo key+P: Computer only, Duplicate, Extend or Projector only. "
                       "<a href=\"run:--display-switch\">Connect to a projector</a></p>"
                       "<p>Resolution and several screens: <a href=\"run:--resolution\">Screen Resolution</a>. Printers, Bluetooth and other devices: <a href=\"run:--devices\">Devices and Printers</a>.</p>"),
        QStringLiteral("<p>Щоб підключити проектор або другий монітор, під'єднайте його й натисніть клавіші емблема Windows+P: «Лише комп'ютер», «Дублювати», «Розширити» або «Лише проектор». "
                       "<a href=\"run:--display-switch\">Підключення до проектора</a></p>"
                       "<p>Роздільна здатність і кілька екранів: <a href=\"run:--resolution\">Роздільна здатність екрана</a>. Принтери, Bluetooth та інші пристрої: <a href=\"run:--devices\">Пристрої та принтери</a>.</p>"));
    add("keys", QStringLiteral("Keyboard shortcuts"), QStringLiteral("Сполучення клавіш"),
        QStringLiteral("<table cellspacing=\"4\">"
                       "<tr><td><b>Windows logo key</b></td><td>Open or close the Start menu</td></tr>"
                       "<tr><td><b>Windows logo key+D</b></td><td>Show the desktop</td></tr>"
                       "<tr><td><b>Windows logo key+E</b></td><td>Open Computer</td></tr>"
                       "<tr><td><b>Windows logo key+R</b></td><td>Open the Run dialog box</td></tr>"
                       "<tr><td><b>Windows logo key+L</b></td><td>Lock the computer</td></tr>"
                       "<tr><td><b>Windows logo key+P</b></td><td>Choose a presentation display mode</td></tr>"
                       "<tr><td><b>Windows logo key+U</b></td><td>Open Ease of Access Center</td></tr>"
                       "<tr><td><b>Windows logo key+Pause</b></td><td>Display the System Properties</td></tr>"
                       "<tr><td><b>Windows logo key+Up / Down</b></td><td>Maximize / restore or minimize the window</td></tr>"
                       "<tr><td><b>Windows logo key+Left / Right</b></td><td>Fill the left or right half of the screen</td></tr>"
                       "<tr><td><b>Windows logo key+Shift+Left / Right</b></td><td>Move the window to the other monitor</td></tr>"
                       "<tr><td><b>Windows logo key+Home</b></td><td>Minimize all but the active window</td></tr>"
                       "<tr><td><b>Windows logo key+Tab</b></td><td>Cycle through the windows</td></tr>"
                       "<tr><td><b>Windows logo key+1…9</b></td><td>Start or switch to the program pinned to the taskbar at that position</td></tr>"
                       "<tr><td><b>Windows logo key+Plus / Minus</b></td><td>Zoom in or out (Magnifier)</td></tr>"
                       "<tr><td><b>Alt+Tab</b></td><td>Switch between open windows</td></tr>"
                       "<tr><td><b>Alt+F4</b></td><td>Close the active window</td></tr>"
                       "<tr><td><b>Alt+Spacebar</b></td><td>Open the shortcut menu of the active window</td></tr>"
                       "<tr><td><b>Ctrl+Shift+Esc</b></td><td>Open Task Manager</td></tr>"
                       "<tr><td><b>Print Screen</b></td><td>Open Snipping Tool</td></tr></table>"),
        QStringLiteral("<table cellspacing=\"4\">"
                       "<tr><td><b>Клавіша з емблемою Windows</b></td><td>Відкрити або закрити меню «Пуск»</td></tr>"
                       "<tr><td><b>Емблема Windows+D</b></td><td>Показати робочий стіл</td></tr>"
                       "<tr><td><b>Емблема Windows+E</b></td><td>Відкрити «Комп'ютер»</td></tr>"
                       "<tr><td><b>Емблема Windows+R</b></td><td>Відкрити діалогове вікно «Виконати»</td></tr>"
                       "<tr><td><b>Емблема Windows+L</b></td><td>Заблокувати комп'ютер</td></tr>"
                       "<tr><td><b>Емблема Windows+P</b></td><td>Вибрати режим показу на екранах</td></tr>"
                       "<tr><td><b>Емблема Windows+U</b></td><td>Відкрити центр спеціальних можливостей</td></tr>"
                       "<tr><td><b>Емблема Windows+Pause</b></td><td>Показати властивості системи</td></tr>"
                       "<tr><td><b>Емблема Windows+Вгору / Вниз</b></td><td>Розгорнути / відновити або згорнути вікно</td></tr>"
                       "<tr><td><b>Емблема Windows+Вліво / Вправо</b></td><td>Розмістити вікно в лівій або правій половині екрана</td></tr>"
                       "<tr><td><b>Емблема Windows+Shift+Вліво / Вправо</b></td><td>Перемістити вікно на інший монітор</td></tr>"
                       "<tr><td><b>Емблема Windows+Home</b></td><td>Згорнути всі вікна, крім активного</td></tr>"
                       "<tr><td><b>Емблема Windows+Tab</b></td><td>Перебрати вікна</td></tr>"
                       "<tr><td><b>Емблема Windows+1…9</b></td><td>Запустити програму, закріплену на панелі завдань у цьому місці, або перейти до неї</td></tr>"
                       "<tr><td><b>Емблема Windows+Плюс / Мінус</b></td><td>Збільшити або зменшити (екранна лупа)</td></tr>"
                       "<tr><td><b>Alt+Tab</b></td><td>Перемикатися між відкритими вікнами</td></tr>"
                       "<tr><td><b>Alt+F4</b></td><td>Закрити активне вікно</td></tr>"
                       "<tr><td><b>Alt+Пробіл</b></td><td>Відкрити контекстне меню активного вікна</td></tr>"
                       "<tr><td><b>Ctrl+Shift+Esc</b></td><td>Відкрити диспетчер завдань</td></tr>"
                       "<tr><td><b>Print Screen</b></td><td>Відкрити «Ножиці»</td></tr></table>"));
    add("security", QStringLiteral("Security and maintenance"), QStringLiteral("Безпека та обслуговування"),
        QStringLiteral("<p>The flag in the notification area is Action Center: it tells you when something needs your attention. <a href=\"run:--action-center\">Open Action Center</a></p>"
                       "<p>Updates of the system and of the programs come from your Linux distribution: <a href=\"run:--windows-update\">Windows Update</a>. "
                       "Windows Firewall, Backup and Restore and Disk Cleanup are in Control Panel &gt; System and Security.</p>"),
        QStringLiteral("<p>Прапорець в області сповіщень — це центр підтримки: він повідомляє, коли щось потребує вашої уваги. <a href=\"run:--action-center\">Відкрити центр підтримки</a></p>"
                       "<p>Оновлення системи й програм надходять від вашого дистрибутива Linux: <a href=\"run:--windows-update\">Windows Update</a>. "
                       "Брандмауер Windows, архівація та відновлення й очищення диска — у розділі «Панель керування» &gt; «Система й безпека».</p>"));
    add("about", QStringLiteral("About this Windows 7 look"), QStringLiteral("Про цей вигляд Windows 7"),
        QStringLiteral("<p>This look is SevenPlasma, made on top of KDE Plasma and AeroThemePlasma. The pictures, sounds and fonts of Windows 7 were taken from your own Windows 7 image when it was installed; "
                       "nothing of Microsoft's comes with SevenPlasma itself.</p>"
                       "<p>To remove it, run <b>uninstall.sh</b> in the SevenPlasma folder: Plasma comes back as it was before.</p>"),
        QStringLiteral("<p>Цей вигляд — SevenPlasma, зроблений на основі KDE Plasma та AeroThemePlasma. Зображення, звуки й шрифти Windows 7 узято з вашого власного образу Windows 7 під час встановлення; "
                       "у самому SevenPlasma немає нічого від Microsoft.</p>"
                       "<p>Щоб видалити його, запустіть <b>uninstall.sh</b> у папці SevenPlasma: Plasma повернеться до попереднього вигляду.</p>"));
    return list;
}

class Window : public QWidget
{
public:
    explicit Window(const QString &startPage)
    {
        setWindowTitle(tr7("Windows Help and Support"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-helppane"), QIcon::fromTheme(QStringLiteral("help-browser"))));
        setAttribute(Qt::WA_DeleteOnClose);

        // the glass, as HelpPane: Back / Forward on the left and Home, Print, Browse Help, Ask, Options on the
        // right; "Search Help" on its own row below, in the middle
        auto *top = new QWidget(this);
        top->setStyleSheet(QStringLiteral("QToolButton { border: 1px solid transparent; border-radius: 3px; background: transparent; color: black; padding: 1px 3px; }"
                                          "QToolButton:hover { border: 1px solid rgba(255, 255, 255, 170); background: rgba(255, 255, 255, 90); }"
                                          "QToolButton:pressed { border: 1px solid rgba(0, 0, 0, 90); background: rgba(255, 255, 255, 60); }"
                                          "QToolButton::menu-indicator { image: none; }"));
        auto *tv = new QVBoxLayout(top);
        tv->setContentsMargins(6, 4, 6, 6);
        tv->setSpacing(4);
        auto *tl = new QHBoxLayout;
        tl->setSpacing(4);
        m_navs = new Aero::NavButtons(top);
        m_navs->back()->setEnabled(false);
        m_navs->forward()->setEnabled(false);
        m_navs->back()->setToolTip(tr7("Back"));
        m_navs->forward()->setToolTip(tr7("Forward"));
        tl->addWidget(m_navs);
        tl->addStretch(1);
        // bitmap 2 of HelpPane.exe: Home, Print, Browse Help, Ask (24 x 24 each)
        const QPixmap strip = pic(QStringLiteral("toolbar"));
        auto cell = [&strip](int i) {
            return strip.isNull() ? QIcon() : QIcon(strip.copy(i * 24, 0, 24, 24));
        };
        auto print = [this] {
            QPrinter printer;
            QPrintDialog dlg(&printer, this);
            if (dlg.exec() == QDialog::Accepted) {
                m_view->print(&printer);
            }
        };
        tl->addWidget(toolButton(top, cell(0), tr7("Help and Support home"), [this] {
            showPage(QStringLiteral("home"));
        }));
        tl->addWidget(toolButton(top, cell(1), tr7("Print"), print));
        tl->addWidget(toolButton(top, cell(2), tr7("Browse Help"), [this] {
            showPage(QStringLiteral("browse"));
        }));
        auto *ask = toolButton(top, cell(3), tr7("&Ask"), [this] {
            showPage(QStringLiteral("ask"));
        });
        ask->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        tl->addWidget(ask);
        auto *options = toolButton(top, QIcon(), tr7("&Options") + QStringLiteral("  \u25BE"), nullptr);
        options->setToolButtonStyle(Qt::ToolButtonTextOnly);
        options->setFixedHeight(30);
        auto *menu = new QMenu(options);
        menu->addAction(tr7("Print..."), this, print);
        menu->addAction(tr7("Browse Help"), this, [this] {
            showPage(QStringLiteral("browse"));
        });
        auto *text = menu->addMenu(tr7("Text Size"));
        for (const auto &[name, size] : {std::pair{tr7("Largest"), 14}, {tr7("Larger"), 12}, {tr7("Medium"), 10}, {tr7("Smaller"), 9}, {tr7("Smallest"), 8}}) {
            text->addAction(name, this, [this, size] {
                QFont f = m_view->font();
                f.setPointSize(size);
                m_view->setFont(f);
                showPage(m_current, false);
            });
        }
        options->setMenu(menu);
        options->setPopupMode(QToolButton::InstantPopup);
        tl->addWidget(options);
        tv->addLayout(tl);
        auto *sl0 = new QHBoxLayout;
        m_search = new QLineEdit(top);
        m_search->setFixedSize(330, 24);
        new Win7SearchBox(m_search, tr7("Search Help"));
        sl0->addStretch(1);
        sl0->addWidget(m_search);
        sl0->addStretch(1);
        tv->addLayout(sl0);

        // the pages
        auto *content = new QWidget(this);
        auto *cl = new QVBoxLayout(content);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(0);
        m_view = new QTextBrowser(content);
        m_view->setOpenLinks(false);
        m_view->setFrameShape(QFrame::NoFrame);
        m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        m_view->setStyleSheet(QStringLiteral("QTextBrowser { background: white; }"));
        m_view->document()->setDefaultStyleSheet(QStringLiteral("a { color: #0066cc; text-decoration: none; } h3 { color: #1e395b; font-weight: normal; font-size: 11pt; }"));
        {
            QPalette pal = m_view->palette();
            pal.setColor(QPalette::Link, QColor(0x00, 0x66, 0xcc));
            pal.setColor(QPalette::LinkVisited, QColor(0x00, 0x66, 0xcc));
            m_view->setPalette(pal);
        }
        {
            // the light line between the parts of the start page (a picture: Qt's rules and cells keep their padding)
            const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
            QDir().mkpath(dir);
            m_line = dir + QStringLiteral("/help-line.png");
            QImage line(1, 1, QImage::Format_RGB32);
            line.fill(QColor(0xd5, 0xdb, 0xe3));
            line.save(m_line);
        }
        cl->addWidget(m_view, 1);
        // the bottom: "More support options" on the left, "Offline Help" on the right
        auto *status = new QWidget(content);
        status->setObjectName(QStringLiteral("win7helpstatus"));
        status->setStyleSheet(QStringLiteral("#win7helpstatus { background: #f0f0f0; border-top: 1px solid #d5dfe5; }"
                                             "QToolButton { border: 1px solid transparent; background: transparent; padding: 1px 2px; }"
                                             "QToolButton:hover { color: #3399ff; }"));
        auto *sv = new QVBoxLayout(status);
        sv->setContentsMargins(4, 3, 6, 3);
        sv->setSpacing(2);
        auto *more = new QToolButton(status);
        more->setIcon(QIcon(pic(QStringLiteral("ask"))));
        more->setText(tr7("&More support options"));
        more->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        connect(more, &QToolButton::clicked, this, [this] {
            showPage(QStringLiteral("ask"));
        });
        auto *ml = new QHBoxLayout;
        ml->addWidget(more);
        ml->addStretch(1);
        sv->addLayout(ml);
        auto *ol = new QHBoxLayout;
        ol->addStretch(1);
        auto *offline = new QToolButton(status);
        const QPixmap states = pic(QStringLiteral("offline"));
        if (!states.isNull()) {
            offline->setIcon(QIcon(states.copy(32, 0, 16, 16))); // the computer: Help on this computer
        }
        offline->setText(tr7("Offli&ne Help") + QStringLiteral("  \u25BE"));
        offline->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        offline->setToolTip(tr7("This Help is on your computer: no Internet connection is needed."));
        ol->addWidget(offline);
        sv->addLayout(ol);
        cl->addWidget(status);

        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->addWidget(content);
        Aero::makeInsetWindow(this, content, top, nullptr);

        connect(m_view, &QTextBrowser::anchorClicked, this, [this](const QUrl &u) {
            open(u);
        });
        connect(m_navs->back(), &QAbstractButton::clicked, this, [this] {
            if (m_pos > 0) {
                showPage(m_history.at(--m_pos), false);
            }
        });
        connect(m_navs->forward(), &QAbstractButton::clicked, this, [this] {
            if (m_pos + 1 < m_history.size()) {
                showPage(m_history.at(++m_pos), false);
            }
        });
        connect(m_search, &QLineEdit::returnPressed, this, [this] {
            if (!m_search->text().trimmed().isEmpty()) {
                showPage(QStringLiteral("search:") + m_search->text().trimmed());
            }
        });
        resize(512, 790); // HelpPane's first size
        m_view->setFocus(); // the search box keeps its "Search Help" hint
        showPage(startPage.isEmpty() ? QStringLiteral("home") : startPage);
    }

private:
    QToolButton *toolButton(QWidget *parent, const QIcon &icon, const QString &tip, const std::function<void()> &f)
    {
        auto *b = new QToolButton(parent);
        b->setIcon(icon);
        b->setIconSize(QSize(24, 24));
        b->setText(tip);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        if (f) {
            connect(b, &QToolButton::clicked, this, f);
        }
        return b;
    }

    void open(const QUrl &u)
    {
        const QString s = u.toString();
        if (s.startsWith(QLatin1String("page:"))) {
            showPage(s.mid(5));
        } else if (s.startsWith(QLatin1String("run:"))) {
            const QString args = s.mid(4);
            QProcess::startDetached(QCoreApplication::applicationFilePath(), args.isEmpty() ? QStringList() : args.split(QLatin1Char(' ')));
        } else {
            QDesktopServices::openUrl(u);
        }
    }

    static QString frame(const QString &title, const QString &body)
    {
        // Windows 7 Help pages: the title in large dark blue, plain text below
        return QStringLiteral("<div style=\"margin: 14px 22px;\"><p style=\"font-size: 15pt; color: #1e395b; margin-bottom: 10px;\">%1</p>"
                              "<div style=\"color: #000;\">%2</div></div>")
            .arg(title.toHtmlEscaped(), body);
    }

    QString html(const QString &id) const
    {
        if (id == QLatin1String("home")) {
            // HelpPane's start page: the magnifier and "Find an answer quickly", the three links with square
            // bullets, a line, then the globe ("More on the Windows website" there; this computer here)
            const QString dir = QUrl::fromLocalFile(QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-control/helppane"),
                                                                           QStandardPaths::LocateDirectory)).toString();
            return QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" style=\"margin-top: 14px;\"><tr>"
                                  "<td width=\"60\" valign=\"top\" align=\"right\"><img src=\"%9/find.png\" width=\"44\" height=\"44\"></td>"
                                  "<td valign=\"top\" style=\"padding-left: 10px;\"><p style=\"font-size: 12pt; color: #0066cc; margin: 4px 0 8px 0;\">%1</p><p>%2</p></td></tr></table>"
                                  "<p style=\"font-size: 12pt; color: #333; margin: 52px 0 12px 20px;\">%3</p>"
                                  "<ul style=\"list-style-type: square; margin-left: 18px;\"><li style=\"margin-bottom: 6px;\"><a href=\"page:start\">%4</a></li><li style=\"margin-bottom: 6px;\"><a href=\"page:basics\">%5</a></li><li><a href=\"page:browse\">%6</a></li></ul>"
                                  "<p align=\"center\" style=\"margin: 24px 0 16px 0;\"><img src=\"%10\" width=\"400\" height=\"1\"></p>"
                                  "<p style=\"font-size: 12pt; color: #333; margin: 6px 0 10px 20px;\">%7</p>"
                                  "<table cellspacing=\"0\" cellpadding=\"0\"><tr><td width=\"60\" valign=\"top\" align=\"right\"><img src=\"%9/website.png\" width=\"48\" height=\"48\"></td>"
                                  "<td valign=\"top\" style=\"padding-left: 10px;\">%8</td></tr></table>")
                .arg(tr7("Find an answer quickly"), tr7("Enter a few words in the search box above."), tr7("Not sure where to start?"),
                     tr7("How to get started with your computer"), tr7("Learn about Windows Basics"), tr7("Browse Help topics"),
                     tr7("More about this computer"),
                     tr7("Read <a href=\"page:about\">about this Windows 7 look</a>: what it is made of, where its pictures come from and how to remove it."),
                     dir, QUrl::fromLocalFile(m_line).toString());
        }
        if (id == QLatin1String("browse")) {
            QString list;
            for (const Page &p : pages()) {
                list += QStringLiteral("<p><a href=\"page:%1\">%2</a></p>").arg(p.id, p.title.toHtmlEscaped());
            }
            return frame(tr7("Contents"), list);
        }
        if (id == QLatin1String("ask")) {
            return frame(tr7("More support options"),
                         QStringLiteral("<p>%1</p><p><a href=\"https://github.com/Vigurov/SevenPlasma/issues\">%2</a></p>")
                             .arg(tr7("Ask a question or report a problem with this Windows 7 look on its project page:"),
                                  QStringLiteral("github.com/Vigurov/SevenPlasma")));
        }
        if (id.startsWith(QLatin1String("search:"))) {
            const QString q = id.mid(7);
            const QStringList words = q.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            QString list;
            int n = 0;
            for (const Page &p : pages()) {
                const QString plain = (p.title + QLatin1Char(' ') + QTextDocumentFragment::fromHtml(p.body).toPlainText());
                bool all = true;
                for (const QString &w : words) {
                    all = all && plain.contains(w, Qt::CaseInsensitive);
                }
                if (all) {
                    ++n;
                    list += QStringLiteral("<p><a href=\"page:%1\">%2</a></p>").arg(p.id, p.title.toHtmlEscaped());
                }
            }
            return frame(tr7("Best 30 results for %1").arg(q),
                         n ? list : QStringLiteral("<p>%1</p>").arg(tr7("No results were found. Try other words.")));
        }
        for (const Page &p : pages()) {
            if (p.id == id) {
                return frame(p.title, p.body);
            }
        }
        return html(QStringLiteral("home"));
    }

    void showPage(const QString &id, bool record = true)
    {
        if (record) {
            while (m_history.size() > m_pos + 1) {
                m_history.removeLast();
            }
            m_history.append(id);
            m_pos = m_history.size() - 1;
        }
        m_current = id;
        m_view->setHtml(html(id));
        m_navs->back()->setEnabled(m_pos > 0);
        m_navs->forward()->setEnabled(m_pos + 1 < m_history.size());
    }

    QTextBrowser *m_view = nullptr;
    QLineEdit *m_search = nullptr;
    Aero::NavButtons *m_navs = nullptr;
    QStringList m_history;
    int m_pos = -1;
    QString m_current;
    QString m_line;
};
}

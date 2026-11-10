/*
 * "Control Panel" results of the Start menu search, as a KRunner D-Bus plugin (org.kde.krunner1).
 * seven-control --krunner is started by D-Bus when the Start menu searches; every match opens a
 * Control Panel page or tool and is shown in the "Панель керування" group, like Windows 7.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

struct RemoteMatch {
    QString id;
    QString text;
    QString iconName;
    int categoryRelevance = 0;
    double relevance = 0;
    QVariantMap properties;
};
using RemoteMatches = QList<RemoteMatch>;

struct RemoteAction {
    QString id;
    QString text;
    QString iconName;
};
using RemoteActions = QList<RemoteAction>;

Q_DECLARE_METATYPE(RemoteMatch)
Q_DECLARE_METATYPE(RemoteMatches)
Q_DECLARE_METATYPE(RemoteAction)
Q_DECLARE_METATYPE(RemoteActions)

QDBusArgument &operator<<(QDBusArgument &a, const RemoteMatch &m)
{
    a.beginStructure();
    a << m.id << m.text << m.iconName << m.categoryRelevance << m.relevance << m.properties;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, RemoteMatch &m)
{
    a.beginStructure();
    a >> m.id >> m.text >> m.iconName >> m.categoryRelevance >> m.relevance >> m.properties;
    a.endStructure();
    return a;
}
QDBusArgument &operator<<(QDBusArgument &a, const RemoteAction &x)
{
    a.beginStructure();
    a << x.id << x.text << x.iconName;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, RemoteAction &x)
{
    a.beginStructure();
    a >> x.id >> x.text >> x.iconName;
    a.endStructure();
    return a;
}

namespace
{
struct Task {
    QString text;
    QString icon;
    QStringList command; // first item "" = seven-control itself
    QString keywords;
};

const QList<Task> &tasks()
{
    static const QList<Task> list{
        {QStringLiteral("Зміна пароля Windows"), QStringLiteral("seven-cpi-users"), {QString(), QStringLiteral("--users")}, QStringLiteral("пароль обліковий запис користувач")},
        {QStringLiteral("Створення облікового запису"), QStringLiteral("seven-cpi-users"), {QString(), QStringLiteral("--manage-users")}, QStringLiteral("користувач додати гість")},
        {QStringLiteral("Керування іншим обліковим записом"), QStringLiteral("seven-cpi-users"), {QString(), QStringLiteral("--manage-users")}, QStringLiteral("користувач обліковий запис видалити")},
        {QStringLiteral("Змінення рисунка облікового запису"), QStringLiteral("seven-cpi-users"), {QString(), QStringLiteral("--users")}, QStringLiteral("аватар рисунок фото користувач")},
        {QStringLiteral("Змінення теми"), QStringLiteral("seven-cpi-personalize"), {QString(), QStringLiteral("--personalize")}, QStringLiteral("тема оформлення персоналізація aero")},
        {QStringLiteral("Змінення фону робочого столу"), QStringLiteral("seven-cpi-personalize"), {QStringLiteral("systemsettings"), QStringLiteral("kcm_wallpaper")}, QStringLiteral("шпалери фон картинка")},
        {QStringLiteral("Змінення звукових ефектів"), QStringLiteral("seven-cpi-sound"), {QString(), QStringLiteral("--sound"), QStringLiteral("2")}, QStringLiteral("звук схема звуки")},
        {QStringLiteral("Керування звуковими пристроями"), QStringLiteral("seven-cpi-sound"), {QString(), QStringLiteral("--sound")}, QStringLiteral("звук динаміки навушники мікрофон гучність")},
        {QStringLiteral("Налаштування роздільної здатності екрана"), QStringLiteral("seven-cpi-display"), {QString(), QStringLiteral("--resolution")}, QStringLiteral("екран монітор роздільна частота")},
        {QStringLiteral("Змінення розміру тексту та інших елементів"), QStringLiteral("seven-cpi-display"), {QString(), QStringLiteral("--display")}, QStringLiteral("екран шрифт масштаб текст")},
        {QStringLiteral("Змінення параметрів миші"), QStringLiteral("seven-cpi-mouse"), {QString(), QStringLiteral("--mouse")}, QStringLiteral("миша мишка кнопки клацання швидкість")},
        {QStringLiteral("Змінення вигляду покажчика миші"), QStringLiteral("seven-cpi-mouse"), {QString(), QStringLiteral("--mouse"), QStringLiteral("1")}, QStringLiteral("миша курсор покажчик")},
        {QStringLiteral("Змінення параметрів клавіатури"), QStringLiteral("seven-cpi-keyboard"), {QString(), QStringLiteral("--keyboard")}, QStringLiteral("клавіатура повторення курсор")},
        {QStringLiteral("Змінення розкладки клавіатури або інших способів введення"), QStringLiteral("seven-cpi-region"), {QString(), QStringLiteral("--text-services")}, QStringLiteral("мова розкладка клавіатура")},
        {QStringLiteral("Змінення форматів дати, часу й чисел"), QStringLiteral("seven-cpi-region"), {QString(), QStringLiteral("--region")}, QStringLiteral("регіон мова формат")},
        {QStringLiteral("Установлення дати й часу"), QStringLiteral("seven-cpi-datetime"), {QString(), QStringLiteral("--set-datetime")}, QStringLiteral("дата час годинник")},
        {QStringLiteral("Змінення часового поясу"), QStringLiteral("seven-cpi-datetime"), {QString(), QStringLiteral("--timezone")}, QStringLiteral("час пояс годинник")},
        {QStringLiteral("Змінення параметрів енергозбереження"), QStringLiteral("seven-cpi-power"), {QString(), QStringLiteral("--power")}, QStringLiteral("живлення енергія батарея план")},
        {QStringLiteral("Налаштування функцій кнопок живлення"), QStringLiteral("seven-cpi-power"), {QString(), QStringLiteral("--power"), QStringLiteral("buttons")}, QStringLiteral("живлення кнопка вимкнення")},
        {QStringLiteral("Налаштування переходу в сплячий режим"), QStringLiteral("seven-cpi-power"), {QString(), QStringLiteral("--power"), QStringLiteral("edit")}, QStringLiteral("сон сплячий режим екран вимкнення")},
        {QStringLiteral("Перегляд стану мережі та завдань"), QStringLiteral("seven-cpi-network"), {QString(), QStringLiteral("--net-center")}, QStringLiteral("мережа інтернет wi-fi вайфай")},
        {QStringLiteral("Перегляд мережевих підключень"), QStringLiteral("seven-cpi-network"), {QString(), QStringLiteral("--net-connections")}, QStringLiteral("мережа адаптер підключення")},
        {QStringLiteral("Перевірка стану брандмауера"), QStringLiteral("seven-cpi-firewall"), {QString(), QStringLiteral("--firewall")}, QStringLiteral("брандмауер захист мережа")},
        {QStringLiteral("Перегляд пристроїв і принтерів"), QStringLiteral("seven-cpi-printers"), {QString(), QStringLiteral("--devices")}, QStringLiteral("пристрій принтер bluetooth")},
        {QStringLiteral("Установка принтера"), QStringLiteral("seven-cpi-printers"), {QString(), QStringLiteral("--add-printer")}, QStringLiteral("принтер друк додати")},
        {QStringLiteral("Диспетчер пристроїв"), QStringLiteral("seven-cpi-devmgr"), {QStringLiteral("seven-devmgr")}, QStringLiteral("пристрій драйвер обладнання")},
        {QStringLiteral("Автоматичне відтворення компакт-дисків або інших носіїв"), QStringLiteral("seven-cpi-autostart"), {QString(), QStringLiteral("--autoplay")}, QStringLiteral("автозапуск диск cd dvd usb флешка")},
        {QStringLiteral("Центр оновлення Windows"), QStringLiteral("seven-cpi-update"), {QString(), QStringLiteral("--windows-update")}, QStringLiteral("оновлення update оновити windows update")},
        {QStringLiteral("Центр підтримки"), QStringLiteral("seven-cpi-actioncenter"), {QString(), QStringLiteral("--action-center")}, QStringLiteral("центр підтримки безпека обслуговування повідомлення проблеми звіти антивірус")},
        {QStringLiteral("Змінення параметрів індексування Windows Search"), QStringLiteral("seven-cpi-indexing"), {QString(), QStringLiteral("--indexing")}, QStringLiteral("індексування індекс пошук baloo search")},
        {QStringLiteral("Перегляд стану комп'ютера та вирішення проблем"), QStringLiteral("seven-cpi-actioncenter"), {QString(), QStringLiteral("--action-center")}, QStringLiteral("стан проблеми звіти про проблеми аварійне завершення")},
        {QStringLiteral("Параметри папок"), QStringLiteral("seven-cpi-folderopts"), {QString(), QStringLiteral("--folder-options")}, QStringLiteral("папка папки параметри приховані файли клацання")},
        {QStringLiteral("Перегляд встановлених шрифтів"), QStringLiteral("seven-cpi-fonts"), {QString(), QStringLiteral("--fonts")}, QStringLiteral("шрифт шрифти fonts")},
        {QStringLiteral("Настроювання тексту ClearType"), QStringLiteral("seven-cttune"), {QString(), QStringLiteral("--cttune")}, QStringLiteral("cleartype шрифт текст згладжування чіткість")},
        {QStringLiteral("Пошук символу"), QStringLiteral("seven-charmap"), {QString(), QStringLiteral("--charmap")}, QStringLiteral("символ таблиця символів charmap знак")},
        {QStringLiteral("Видалення програми"), QStringLiteral("seven-cpi-programs"), {QString(), QStringLiteral("--programs")}, QStringLiteral("програма видалити встановлені")},
        {QStringLiteral("Перегляд встановлених оновлень"), QStringLiteral("seven-cpi-programs"), {QString(), QStringLiteral("--updates")}, QStringLiteral("оновлення програма")},
        {QStringLiteral("Увімкнення або вимкнення компонентів Windows"), QStringLiteral("seven-cpi-programs"), {QString(), QStringLiteral("--features")}, QStringLiteral("компоненти функції ігри")},
        {QStringLiteral("Задання програм за замовчуванням"), QStringLiteral("seven-cpi-defaults"), {QString(), QStringLiteral("--set-defaults")}, QStringLiteral("програма замовчуванням оглядач браузер")},
        {QStringLiteral("Зіставлення типу файлу з програмою"), QStringLiteral("seven-cpi-defaults"), {QString(), QStringLiteral("--associations")}, QStringLiteral("файл тип розширення відкривати")},
        {QStringLiteral("Налаштування значків на панелі завдань"), QStringLiteral("seven-cpi-trayicons"), {QString(), QStringLiteral("--tray-icons")}, QStringLiteral("значки сповіщення панель")},
        {QStringLiteral("Панель завдань і меню «Пуск»"), QStringLiteral("seven-cpi-taskbar"), {QString(), QStringLiteral("--taskbar-props")}, QStringLiteral("панель завдань пуск меню")},
        {QStringLiteral("Центр спеціальних можливостей"), QStringLiteral("seven-cpi-access"), {QString(), QStringLiteral("--ease")}, QStringLiteral("спеціальні можливості доступність")},
        {QStringLiteral("Збільшення частини екрана за допомогою екранної лупи"), QStringLiteral("seven-cpi-access"), {QString(), QStringLiteral("--magnifier")}, QStringLiteral("лупа збільшення екран")},
        {QStringLiteral("Введення тексту за допомогою екранної клавіатури"), QStringLiteral("seven-cpi-access"), {QString(), QStringLiteral("--osk")}, QStringLiteral("клавіатура екранна")},
        {QStringLiteral("Читання тексту вголос за допомогою екранного диктора"), QStringLiteral("seven-cpi-access"), {QString(), QStringLiteral("--narrator")}, QStringLiteral("диктор читання голос")},
        {QStringLiteral("Полегшення роботи з клавіатурою"), QStringLiteral("seven-cpi-access"), {QString(), QStringLiteral("--ease"), QStringLiteral("keyboard")}, QStringLiteral("залипання клавіш клавіатура")},
        {QStringLiteral("Звільнення місця на диску шляхом видалення непотрібних файлів"), QStringLiteral("seven-cleanmgr"), {QString(), QStringLiteral("--cleanmgr")}, QStringLiteral("диск очищення місце кеш тимчасові")},
        {QStringLiteral("Створення і форматування розділів жорсткого диска"), QStringLiteral("seven-cpi-admin"), {QString(), QStringLiteral("--diskmgmt")}, QStringLiteral("диск розділ форматування керування дисками")},
        {QStringLiteral("Архівування даних комп'ютера"), QStringLiteral("seven-cpi-backup"), {QString(), QStringLiteral("--backup")}, QStringLiteral("архівація резервна копія backup архів відновлення")},
        {QStringLiteral("Відновлення файлів з архіву"), QStringLiteral("seven-cpi-backup"), {QString(), QStringLiteral("--restore-files")}, QStringLiteral("відновити файли архів резервна копія")},
        {QStringLiteral("Створення образу системи"), QStringLiteral("seven-cpi-backup"), {QString(), QStringLiteral("--sysimage")}, QStringLiteral("образ системи архів timeshift")},
        {QStringLiteral("Відновлення комп'ютера до попереднього стану"), QStringLiteral("seven-cpi-recovery"), {QString(), QStringLiteral("--rstrui")}, QStringLiteral("відновлення системи точка відновлення rstrui")},
        {QStringLiteral("Перегляд журналів подій"), QStringLiteral("seven-cpi-admin"), {QString(), QStringLiteral("--eventvwr")}, QStringLiteral("журнал події перегляд подій помилки eventvwr")},
        {QStringLiteral("Перегляд обсягу ОЗП і швидкості процесора"), QStringLiteral("seven-cpi-system"), {QStringLiteral("seven-system")}, QStringLiteral("система процесор пам'ять озп комп'ютер")},
        {QStringLiteral("Перегляд запущених процесів у диспетчері задач"), QStringLiteral("seven-cpi-perf"), {QStringLiteral("seven-taskmgr")}, QStringLiteral("процеси диспетчер задач")},
    };
    return list;
}

/** every word of the query starts a word of the text or the keywords */
int score(const Task &t, const QStringList &words)
{
    const QStringList hay = (t.text + QLatin1Char(' ') + t.keywords).toLower().split(QRegularExpression(QStringLiteral("[\\s,«»()]+")), Qt::SkipEmptyParts);
    int s = 0;
    for (const QString &w : words) {
        bool found = false;
        for (const QString &h : hay) {
            if (h.startsWith(w)) {
                found = true;
                s += h == w ? 3 : 2;
                break;
            }
        }
        if (!found) {
            return 0;
        }
    }
    return s;
}
}

class ControlPanelRunner : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.krunner1")
public:
    using QObject::QObject;

public Q_SLOTS:
    RemoteActions Actions()
    {
        return {};
    }

    RemoteMatches Match(const QString &query)
    {
        keepAlive();
        RemoteMatches out;
        const QStringList words = query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (words.isEmpty() || query.trimmed().size() < 2) {
            return out;
        }
        for (int i = 0; i < tasks().size(); ++i) {
            const int s = score(tasks().at(i), words);
            if (!s) {
                continue;
            }
            RemoteMatch m;
            m.id = QString::number(i);
            m.text = tasks().at(i).text;
            m.iconName = tasks().at(i).icon;
            m.categoryRelevance = 50; // "possible match"
            m.relevance = qMin(0.9, 0.5 + s * 0.05);
            m.properties.insert(QStringLiteral("category"), QStringLiteral("Панель керування"));
            out.append(m);
        }
        return out;
    }

    void Run(const QString &id, const QString &)
    {
        keepAlive();
        const int i = id.toInt();
        if (i < 0 || i >= tasks().size()) {
            return;
        }
        QStringList cmd = tasks().at(i).command;
        QString program = cmd.takeFirst();
        if (program.isEmpty()) {
            program = QCoreApplication::applicationFilePath();
        } else if (program.startsWith(QLatin1String("seven-"))) {
            program = QDir::homePath() + QStringLiteral("/.local/bin/") + program;
        }
        QProcess::startDetached(program, cmd);
    }

    void Teardown()
    {
    }

private:
    /** D-Bus starts us again when needed, so quit after a while without searches */
    void keepAlive()
    {
        if (!m_idle) {
            m_idle = new QTimer(this);
            m_idle->setSingleShot(true);
            connect(m_idle, &QTimer::timeout, qApp, &QCoreApplication::quit);
        }
        m_idle->start(10 * 60 * 1000);
    }
    QTimer *m_idle = nullptr;
};

int runKRunner()
{
    qDBusRegisterMetaType<RemoteMatch>();
    qDBusRegisterMetaType<RemoteMatches>();
    qDBusRegisterMetaType<RemoteAction>();
    qDBusRegisterMetaType<RemoteActions>();
    auto *runner = new ControlPanelRunner(qApp);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/runner"), runner, QDBusConnection::ExportAllSlots) || !bus.registerService(QStringLiteral("org.sevenplasma.ControlPanelRunner"))) {
        return 1;
    }
    QMetaObject::invokeMethod(runner, [runner] { runner->Match(QString()); }, Qt::QueuedConnection); // starts the idle timer
    return qApp->exec();
}

#include "sevenrunner.moc"

#pragma once
#include "win7i18n.h"
/*
 * Auto playlists ("Автоматичний список відтворення"): rules over the music library, saved as JSON
 * (~/Music/Playlists/<name>.autopl) and evaluated whenever the list is opened.
 */
#include "library.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace AutoPl
{
struct Rule {
    QString field; // rating, genre, artist, album, year, added, length
    QString op;    // >=, <=, =, contains, notcontains
    QString value;
};

inline const QList<QPair<QString, QString>> &fields()
{
    static const QList<QPair<QString, QString>> f{{QStringLiteral("rating"), tr7("Rating (stars)")},
                                                  {QStringLiteral("genre"), tr7("Genre")},
                                                  {QStringLiteral("artist"), tr7("Contributing Artist")},
                                                  {QStringLiteral("album"), tr7("Album")},
                                                  {QStringLiteral("year"), tr7("Release Year")},
                                                  {QStringLiteral("added"), tr7("Date Added in the last (days)")},
                                                  {QStringLiteral("length"), tr7("Length (minutes)")}};
    return f;
}
inline bool numeric(const QString &field)
{
    return field == QLatin1String("rating") || field == QLatin1String("year") || field == QLatin1String("added") || field == QLatin1String("length");
}

inline bool matches(const MediaItem &m, const Rule &r)
{
    if (numeric(r.field)) {
        const double v = r.value.toDouble();
        double x = 0;
        if (r.field == QLatin1String("rating")) {
            x = m.rating;
        } else if (r.field == QLatin1String("year")) {
            x = m.year;
        } else if (r.field == QLatin1String("length")) {
            x = m.duration / 60000.0;
        } else {
            // added in the last N days: compare the age of the file
            x = (QDateTime::currentSecsSinceEpoch() - m.mtime) / 86400.0;
            return x <= v;
        }
        if (r.op == QLatin1String(">=")) {
            return x >= v;
        }
        if (r.op == QLatin1String("<=")) {
            return x <= v;
        }
        return qFuzzyCompare(x + 1, v + 1);
    }
    const QString s = r.field == QLatin1String("genre") ? m.genre : r.field == QLatin1String("artist") ? m.artist : m.album;
    if (r.op == QLatin1String("notcontains")) {
        return !s.contains(r.value, Qt::CaseInsensitive);
    }
    if (r.op == QLatin1String("=")) {
        return s.compare(r.value, Qt::CaseInsensitive) == 0;
    }
    return s.contains(r.value, Qt::CaseInsensitive);
}

inline QList<Rule> load(const QString &file)
{
    QList<Rule> rules;
    QFile f(file);
    if (f.open(QIODevice::ReadOnly)) {
        const auto arr = QJsonDocument::fromJson(f.readAll()).object()[u"rules"].toArray();
        for (const auto &v : arr) {
            const auto o = v.toObject();
            rules << Rule{o[u"field"].toString(), o[u"op"].toString(), o[u"value"].toString()};
        }
    }
    return rules;
}
inline void save(const QString &file, const QList<Rule> &rules)
{
    QJsonArray arr;
    for (const auto &r : rules) {
        arr << QJsonObject{{QStringLiteral("field"), r.field}, {QStringLiteral("op"), r.op}, {QStringLiteral("value"), r.value}};
    }
    QFile f(file);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject{{QStringLiteral("rules"), arr}}).toJson());
    }
}
inline QList<QUrl> evaluate(const QString &file, const Library *lib)
{
    const auto rules = load(file);
    QList<QUrl> out;
    for (const auto &m : lib->items(MediaItem::Audio)) {
        bool ok = true;
        for (const auto &r : rules) {
            ok = ok && matches(m, r);
        }
        if (ok) {
            out << m.url();
        }
    }
    return out;
}

/** "Новий автоматичний список відтворення" */
class Dialog : public QDialog
{
public:
    explicit Dialog(QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(tr7("New Auto Playlist"));
        auto *l = new QVBoxLayout(this);
        l->addWidget(new QLabel(tr7("Auto Playlist name:"), this));
        m_name = new QLineEdit(tr7("New Auto Playlist"), this);
        l->addWidget(m_name);
        auto *crit = new QLabel(tr7("<b>Create an auto playlist that includes the following:</b><br>Music in my library"), this);
        l->addWidget(crit);
        m_rows = new QVBoxLayout;
        l->addLayout(m_rows);
        auto *add = new QPushButton(tr7("Click here to add criteria"), this);
        add->setFlat(true);
        add->setStyleSheet(QStringLiteral("QPushButton { color: #3366cc; text-align: left; }"));
        l->addWidget(add);
        connect(add, &QPushButton::clicked, this, [this] {
            addRow({QStringLiteral("rating"), QStringLiteral(">="), QStringLiteral("4")});
        });
        addRow({QStringLiteral("rating"), QStringLiteral(">="), QStringLiteral("4")});
        l->addStretch(1);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("OK"));
        bb->button(QDialogButtonBox::Cancel)->setText(tr7("Cancel"));
        l->addWidget(bb);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        resize(520, 360);
    }
    QString name() const
    {
        return m_name->text().trimmed();
    }
    QList<Rule> rules() const
    {
        QList<Rule> out;
        for (const auto &r : m_widgets) {
            out << Rule{r.field->currentData().toString(), r.op->currentData().toString(), r.value->text()};
        }
        return out;
    }

private:
    struct RowWidgets {
        QComboBox *field;
        QComboBox *op;
        QLineEdit *value;
    };
    void addRow(const Rule &rule)
    {
        auto *row = new QHBoxLayout;
        auto *field = new QComboBox(this);
        for (const auto &f : fields()) {
            field->addItem(f.second, f.first);
        }
        field->setCurrentIndex(field->findData(rule.field));
        auto *op = new QComboBox(this);
        auto *value = new QLineEdit(rule.value, this);
        auto fillOps = [op, field] {
            op->clear();
            if (numeric(field->currentData().toString())) {
                op->addItem(tr7("Is At Least"), QStringLiteral(">="));
                op->addItem(tr7("Is At Most"), QStringLiteral("<="));
                op->addItem(tr7("Is"), QStringLiteral("="));
            } else {
                op->addItem(tr7("Contains"), QStringLiteral("contains"));
                op->addItem(tr7("Does Not Contain"), QStringLiteral("notcontains"));
                op->addItem(tr7("Is"), QStringLiteral("="));
            }
        };
        fillOps();
        op->setCurrentIndex(qMax(0, op->findData(rule.op)));
        connect(field, &QComboBox::currentIndexChanged, this, fillOps);
        row->addSpacing(20);
        row->addWidget(field);
        row->addWidget(op);
        row->addWidget(value, 1);
        m_rows->addLayout(row);
        m_widgets << RowWidgets{field, op, value};
    }
    QLineEdit *m_name;
    QVBoxLayout *m_rows;
    QList<RowWidgets> m_widgets;
};
}

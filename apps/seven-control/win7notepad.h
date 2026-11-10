/*
 * Notepad of Windows 7 (notepad.exe): its menus (menu 1), Go To Line (dialog 14), Find and Replace as Notepad
 * shows comdlg32's templates 1540/1541 (no "whole word", no Help), the status bar with line and column,
 * word wrap, the font in Lucida Console 10, Page Setup and Print with the file name on top and the page number
 * below, and the ".LOG" trick that stamps the time when such a file is opened.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "win7folderopts.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QIntValidator>
#include <QPointer>
#include <QProcess>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialog>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFontDialog>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPageSetupDialog>
#include <QPainter>
#include <QPlainTextEdit>
#include "win7wrapmenubar.h"
#include <QProxyStyle>
#include <QResizeEvent>
#include <QStyleOptionMenuItem>
#include <QVBoxLayout>
#include <QSizeGrip>
#include <QScrollBar>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringDecoder>
#include <QStringEncoder>
#include <QTextBlock>
#include <QTextDocument>

namespace Win7Notepad
{

using Win7FolderOpts::dlu;

template<typename T>
T *place(QWidget *parent, T *w, const QRect &r)
{
    w->setParent(parent);
    w->setGeometry(r);
    return w;
}

inline QSettings &settings()
{
    static QSettings s(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/seven-notepadrc"), QSettings::IniFormat);
    return s;
}

/** The encodings of Notepad's Save As: ANSI (here Windows-1251), Unicode, Unicode big endian, UTF-8 */
enum Encoding { Ansi, Utf16LE, Utf16BE, Utf8 };

class Notepad : public QMainWindow
{
public:
    explicit Notepad(const QString &file = QString())
    {
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-notepad")));
        setAcceptDrops(true);
        m_edit = new QPlainTextEdit;
        m_edit->setFrameShape(QFrame::NoFrame);
        // Windows 7's Notepad always shows its scroll bars (greyed out, without a thumb, while there is nothing
        // to scroll; the horizontal one only without Word Wrap) and the size grip in the corner between them
        m_edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        for (QScrollBar *bar : {m_edit->verticalScrollBar(), m_edit->horizontalScrollBar()}) {
            bar->setEnabled(false);
            connect(bar, &QScrollBar::rangeChanged, bar, [bar](int min, int max) {
                bar->setEnabled(max > min);
            });
        }
        m_cornerGrip = new QSizeGrip(m_edit);
        {
            // the corner between the scroll bars is window grey (#f0f0f0) on Windows 7, not the text's white
            QPalette pal = m_cornerGrip->palette();
            pal.setColor(QPalette::Window, QColor(0xf0, 0xf0, 0xf0));
            m_cornerGrip->setPalette(pal);
            m_cornerGrip->setAutoFillBackground(true);
        }
        m_edit->setCornerWidget(m_cornerGrip);
        m_edit->setAcceptDrops(false);
        QFont f(QStringLiteral("Lucida Console"), 10);
        f.fromString(settings().value(QStringLiteral("Font"), f.toString()).toString());
        m_edit->setFont(f);
        m_edit->setTabStopDistance(QFontMetricsF(f).horizontalAdvance(QLatin1Char(' ')) * 8);
        // Windows 7 (measured): the text box frame around the text and the scroll bars, 1 px of white inside it,
        // 1 px of window colour between it and the menu bar; blue while the text has the focus
        struct EditFrame : QWidget {
            QWidget *edit;
            explicit EditFrame(QWidget *e)
                : edit(e)
            {
                auto *l = new QVBoxLayout(this);
                l->setContentsMargins(2, 3, 2, 2);
                l->addWidget(e);
                connect(qApp, &QApplication::focusChanged, this, [this] { update(); });
            }
            void paintEvent(QPaintEvent *) override
            {
                QPainter p(this);
                const bool focus = edit->hasFocus() && isActiveWindow();
                const QRect r = rect().adjusted(0, 1, 0, 0);
                p.fillRect(r.adjusted(1, 1, -1, -1), Qt::white);
                p.setPen(focus ? QColor(0x3d, 0x7b, 0xad) : QColor(0xab, 0xad, 0xb3));
                p.drawLine(r.topLeft(), r.topRight());
                p.setPen(focus ? QColor(0xb5, 0xcf, 0xe7) : QColor(0xe2, 0xe3, 0xea));
                p.drawLine(r.left(), r.top() + 1, r.left(), r.bottom());
                p.setPen(focus ? QColor(0xa4, 0xc9, 0xe3) : QColor(0xdb, 0xdf, 0xe6));
                p.drawLine(r.right(), r.top() + 1, r.right(), r.bottom());
                p.setPen(focus ? QColor(0xb7, 0xd9, 0xed) : QColor(0xe3, 0xe9, 0xef));
                p.drawLine(r.left() + 1, r.bottom(), r.right() - 1, r.bottom());
            }
        };
        setCentralWidget(new EditFrame(m_edit));
        m_position = new QLabel;
        m_position->setMinimumWidth(170);
        statusBar()->addPermanentWidget(m_position);
        statusBar()->setSizeGripEnabled(true);
        buildMenus();
        {
            // the menus move to a menu bar that wraps like Windows 7's (instead of a "»" arrow)
            win7WrapMenuBar(this);
            // Windows 7 lets the window shrink down to its title bar
            m_edit->setMinimumSize(1, 1);
            centralWidget()->setMinimumSize(1, 1);
            // Windows 7 lets it shrink down to the title bar with its buttons
            setMinimumSize(112, 1);
        }
        connect(m_edit, &QPlainTextEdit::cursorPositionChanged, this, [this] { updatePosition(); });
        connect(m_edit->document(), &QTextDocument::modificationChanged, this, [this] { updateTitle(); });
        connect(m_edit, &QPlainTextEdit::selectionChanged, this, [this] { updateEditActions(); });
        connect(m_edit, &QPlainTextEdit::textChanged, this, [this] { updateEditActions(); });
        resize(settings().value(QStringLiteral("Size"), QSize(720, 480)).toSize());
        setWrap(settings().value(QStringLiteral("WordWrap"), false).toBool());
        if (!file.isEmpty()) {
            load(file);
        }
        updateTitle();
        updatePosition();
        updateEditActions();
    }

protected:
    void closeEvent(QCloseEvent *e) override
    {
        if (!maybeSave()) {
            e->ignore();
            return;
        }
        settings().setValue(QStringLiteral("Size"), size());
        e->accept();
    }
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (e->mimeData()->hasUrls()) {
            e->acceptProposedAction();
        }
    }
    void dropEvent(QDropEvent *e) override
    {
        const QList<QUrl> urls = e->mimeData()->urls();
        if (!urls.isEmpty() && urls.first().isLocalFile() && maybeSave()) {
            load(urls.first().toLocalFile());
        }
    }

private:
    QAction *add(QMenu *menu, const QString &text, const QKeySequence &key, std::function<void()> f)
    {
        QAction *a = menu->addAction(text);
        if (!key.isEmpty()) {
            a->setShortcut(key);
        }
        connect(a, &QAction::triggered, this, [f] { f(); });
        return a;
    }

    void buildMenus()
    {
        QMenu *file = menuBar()->addMenu(QStringLiteral("&Файл"));
        add(file, QStringLiteral("&Створити"), QKeySequence(QStringLiteral("Ctrl+N")), [this] { newFile(); });
        add(file, QStringLiteral("&Відкрити..."), QKeySequence(QStringLiteral("Ctrl+O")), [this] { openFile(); });
        add(file, QStringLiteral("&Зберегти"), QKeySequence(QStringLiteral("Ctrl+S")), [this] { save(); });
        add(file, QStringLiteral("Зберегти &як..."), {}, [this] { saveAs(); });
        file->addSeparator();
        add(file, QStringLiteral("&Параметри сторінки..."), {}, [this] { pageSetup(); });
        add(file, QStringLiteral("&Друк..."), QKeySequence(QStringLiteral("Ctrl+P")), [this] { print(); });
        file->addSeparator();
        add(file, QStringLiteral("В&ихід"), {}, [this] { close(); });

        QMenu *edit = menuBar()->addMenu(QStringLiteral("&Редагування"));
        m_undo = add(edit, QStringLiteral("&Скасувати"), QKeySequence(QStringLiteral("Ctrl+Z")), [this] { m_edit->undo(); });
        edit->addSeparator();
        m_cut = add(edit, QStringLiteral("&Вирізати"), QKeySequence(QStringLiteral("Ctrl+X")), [this] { m_edit->cut(); });
        m_copy = add(edit, QStringLiteral("&Копіювати"), QKeySequence(QStringLiteral("Ctrl+C")), [this] { m_edit->copy(); });
        m_paste = add(edit, QStringLiteral("Вст&авити"), QKeySequence(QStringLiteral("Ctrl+V")), [this] { m_edit->paste(); });
        m_delete = add(edit, QStringLiteral("В&идалити"), QKeySequence(QStringLiteral("Del")), [this] { m_edit->textCursor().removeSelectedText(); });
        m_delete->setShortcutContext(Qt::WidgetShortcut); // Del keeps deleting characters in the text
        edit->addSeparator();
        m_find = add(edit, QStringLiteral("&Знайти..."), QKeySequence(QStringLiteral("Ctrl+F")), [this] { findDialog(false); });
        m_findNext = add(edit, QStringLiteral("Знайти &далі"), QKeySequence(QStringLiteral("F3")), [this] {
            if (m_findText.isEmpty()) {
                findDialog(false);
            } else {
                findNext(true);
            }
        });
        add(edit, QStringLiteral("За&мінити..."), QKeySequence(QStringLiteral("Ctrl+H")), [this] { findDialog(true); });
        m_goTo = add(edit, QStringLiteral("&Перейти..."), QKeySequence(QStringLiteral("Ctrl+G")), [this] { goToLine(); });
        edit->addSeparator();
        add(edit, QStringLiteral("Виді&лити все"), QKeySequence(QStringLiteral("Ctrl+A")), [this] { m_edit->selectAll(); });
        add(edit, QStringLiteral("&Час і дата"), QKeySequence(QStringLiteral("F5")), [this] { m_edit->insertPlainText(timeStamp()); });

        QMenu *format = menuBar()->addMenu(QStringLiteral("Фор&мат"));
        m_wrap = add(format, QStringLiteral("&Перенесення за словами"), {}, [this] { setWrap(m_wrap->isChecked()); });
        m_wrap->setCheckable(true);
        add(format, QStringLiteral("&Шрифт..."), {}, [this] { chooseFont(); });

        QMenu *view = menuBar()->addMenu(QStringLiteral("&Вигляд"));
        m_status = add(view, QStringLiteral("&Рядок стану"), {}, [this] {
            settings().setValue(QStringLiteral("StatusBar"), m_status->isChecked());
            statusBar()->setVisible(m_status->isChecked());
        });
        m_status->setCheckable(true);

        QMenu *help = menuBar()->addMenu(QStringLiteral("&Довідка"));
        add(help, QStringLiteral("Перегляд &довідки"), {}, [] { QProcess::startDetached(QStringLiteral("khelpcenter"), {}); });
        help->addSeparator();
        add(help, QStringLiteral("&Про програму"), {}, [this] {
            QMessageBox box(this);
            box.setWindowTitle(QStringLiteral("Про програму «Блокнот»"));
            box.setIconPixmap(QIcon::fromTheme(QStringLiteral("seven-notepad")).pixmap(48, 48));
            box.setText(QStringLiteral("<b>Блокнот</b>"));
            box.setInformativeText(QStringLiteral("Простий текстовий редактор у стилі Windows 7.\nПрацює на Linux із KDE Plasma."));
            box.exec();
        });
    }

    static QString timeStamp()
    {
        return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm dd.MM.yyyy"));
    }

    /** Word wrap turns off Go To and the status bar, as in Windows 7's Notepad */
    void setWrap(bool on)
    {
        m_wrap->setChecked(on);
        settings().setValue(QStringLiteral("WordWrap"), on);
        m_edit->setLineWrapMode(on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
        m_edit->setHorizontalScrollBarPolicy(on ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAlwaysOn);
        m_goTo->setEnabled(!on);
        m_status->setEnabled(!on);
        const bool status = !on && settings().value(QStringLiteral("StatusBar"), false).toBool();
        m_status->setChecked(status);
        statusBar()->setVisible(status);
    }

    void chooseFont()
    {
        bool ok = false;
        const QFont f = QFontDialog::getFont(&ok, m_edit->font(), this, QStringLiteral("Шрифт"));
        if (ok) {
            m_edit->setFont(f);
            m_edit->setTabStopDistance(QFontMetricsF(f).horizontalAdvance(QLatin1Char(' ')) * 8);
            settings().setValue(QStringLiteral("Font"), f.toString());
        }
    }

    void updatePosition()
    {
        const QTextCursor c = m_edit->textCursor();
        m_position->setText(QStringLiteral("   Рядок %1, стовпець %2").arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1));
    }

    void updateEditActions()
    {
        const bool sel = m_edit->textCursor().hasSelection();
        m_cut->setEnabled(sel);
        m_copy->setEnabled(sel);
        m_delete->setEnabled(sel);
        m_undo->setEnabled(m_edit->document()->isUndoAvailable());
        const bool any = !m_edit->document()->isEmpty();
        m_find->setEnabled(any);
        m_findNext->setEnabled(any);
    }

    QString shownName() const
    {
        return m_file.isEmpty() ? QStringLiteral("Без назви") : QFileInfo(m_file).fileName();
    }

    void updateTitle()
    {
        setWindowTitle(QStringLiteral("%1 - Блокнот").arg(shownName()));
    }

    /* ---------------- files ---------------- */

    void load(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, QStringLiteral("Блокнот"), QStringLiteral("Не вдається знайти файл «%1».\n\nПеревірте правильність імені файлу й повторіть спробу.").arg(path));
            return;
        }
        const QByteArray data = f.readAll();
        QString text;
        if (data.startsWith("\xEF\xBB\xBF")) {
            m_encoding = Utf8;
            m_bom = true;
            text = QString::fromUtf8(data.mid(3));
        } else if (data.startsWith("\xFF\xFE")) {
            m_encoding = Utf16LE;
            QStringDecoder d(QStringDecoder::Utf16LE);
            text = d(data.mid(2));
        } else if (data.startsWith("\xFE\xFF")) {
            m_encoding = Utf16BE;
            QStringDecoder d(QStringDecoder::Utf16BE);
            text = d(data.mid(2));
        } else {
            QStringDecoder utf8(QStringDecoder::Utf8);
            text = utf8(data);
            m_bom = false;
            if (utf8.hasError()) {
                // not UTF-8: Notepad's "ANSI", the Cyrillic code page of a Ukrainian Windows
                m_encoding = Ansi;
                QStringDecoder cp(QStringLiteral("windows-1251"));
                text = cp.isValid() ? cp(data) : QString::fromLatin1(data);
            } else {
                m_encoding = Utf8;
            }
        }
        m_edit->setPlainText(text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")));
        m_crlf = data.contains("\r\n");
        m_file = QFileInfo(path).absoluteFilePath();
        // a file that starts with .LOG gets the time on a new line each time it is opened
        if (text.startsWith(QLatin1String(".LOG"))) {
            m_edit->moveCursor(QTextCursor::End);
            m_edit->insertPlainText(QLatin1Char('\n') + timeStamp() + QLatin1Char('\n'));
        } else {
            m_edit->document()->setModified(false);
            m_edit->moveCursor(QTextCursor::Start);
        }
        updateTitle();
    }

    bool writeTo(const QString &path, Encoding enc)
    {
        QString text = m_edit->toPlainText();
        if (m_crlf) {
            text.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
        }
        QByteArray out;
        switch (enc) {
        case Ansi: {
            QStringEncoder e(QStringLiteral("windows-1251"));
            out = e.isValid() ? QByteArray(e(text)) : text.toLocal8Bit();
            break;
        }
        case Utf16LE: {
            QStringEncoder e(QStringEncoder::Utf16LE);
            out = QByteArray("\xFF\xFE", 2) + QByteArray(e(text));
            break;
        }
        case Utf16BE: {
            QStringEncoder e(QStringEncoder::Utf16BE);
            out = QByteArray("\xFE\xFF", 2) + QByteArray(e(text));
            break;
        }
        case Utf8:
            out = (m_bom ? QByteArray("\xEF\xBB\xBF") : QByteArray()) + text.toUtf8();
            break;
        }
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(out) != out.size()) {
            QMessageBox::warning(this, QStringLiteral("Блокнот"), QStringLiteral("Не вдається зберегти файл «%1».\n\nМожливо, немає дозволу на запис у цю папку.").arg(path));
            return false;
        }
        m_file = QFileInfo(path).absoluteFilePath();
        m_encoding = enc;
        m_edit->document()->setModified(false);
        updateTitle();
        return true;
    }

    bool save()
    {
        return m_file.isEmpty() ? saveAs() : writeTo(m_file, m_encoding);
    }

    /** Save As with Notepad's "Encoding" box under the file name */
    bool saveAs()
    {
        QFileDialog dlg(this, QStringLiteral("Зберегти як"), m_file.isEmpty() ? QDir::homePath() + QStringLiteral("/*.txt") : m_file);
        dlg.setAcceptMode(QFileDialog::AcceptSave);
        dlg.setNameFilters({QStringLiteral("Текстові документи (*.txt)"), QStringLiteral("Усі файли (*)")});
        dlg.setDefaultSuffix(QStringLiteral("txt"));
        if (m_file.isEmpty()) {
            dlg.selectFile(QStringLiteral("*.txt"));
        }
        if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty()) {
            return false;
        }
        // the encoding: kept from the opened file, chosen here for a new one
        Encoding enc = m_encoding;
        QDialog ask(this);
        ask.setWindowTitle(QStringLiteral("Кодування"));
        auto *v = new QVBoxLayout(&ask);
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(QStringLiteral("Кодування:")));
        auto *combo = new QComboBox;
        combo->addItems({QStringLiteral("ANSI"), QStringLiteral("Юнікод"), QStringLiteral("Юнікод (зворотний порядок байтів)"), QStringLiteral("UTF-8")});
        combo->setCurrentIndex(enc);
        row->addWidget(combo, 1);
        v->addLayout(row);
        auto *bar = new QHBoxLayout;
        bar->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("Зберегти"));
        ok->setDefault(true);
        bar->addWidget(ok);
        v->addLayout(bar);
        connect(ok, &QPushButton::clicked, &ask, &QDialog::accept);
        if (m_file.isEmpty() && ask.exec() == QDialog::Accepted) {
            enc = Encoding(combo->currentIndex());
        }
        return writeTo(dlg.selectedFiles().first(), enc);
    }

    /** "Do you want to save changes to Untitled?" */
    bool maybeSave()
    {
        if (!m_edit->document()->isModified()) {
            return true;
        }
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("Блокнот"));
        box.setText(QStringLiteral("Зберегти зміни у файлі «%1»?").arg(m_file.isEmpty() ? shownName() : m_file));
        QPushButton *yes = box.addButton(QStringLiteral("Зберегти"), QMessageBox::AcceptRole);
        QPushButton *no = box.addButton(QStringLiteral("Не зберігати"), QMessageBox::DestructiveRole);
        box.addButton(QStringLiteral("Скасувати"), QMessageBox::RejectRole);
        box.setDefaultButton(yes);
        box.exec();
        if (box.clickedButton() == yes) {
            return save();
        }
        return box.clickedButton() == no;
    }

    void newFile()
    {
        if (!maybeSave()) {
            return;
        }
        m_edit->clear();
        m_file.clear();
        m_encoding = Utf8;
        m_bom = false;
        m_crlf = false;
        m_edit->document()->setModified(false);
        updateTitle();
    }

    void openFile()
    {
        if (!maybeSave()) {
            return;
        }
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Відкрити"), m_file.isEmpty() ? QDir::homePath() : QFileInfo(m_file).absolutePath(),
                                                          QStringLiteral("Текстові документи (*.txt);;Усі файли (*)"));
        if (!path.isEmpty()) {
            load(path);
        }
    }

    /* ---------------- printing ---------------- */

    QPrinter &printer()
    {
        if (!m_printer) {
            m_printer = std::make_unique<QPrinter>(QPrinter::HighResolution);
            m_printer->setPageMargins(QMarginsF(20, 25, 20, 25), QPageLayout::Millimeter);
        }
        return *m_printer;
    }

    void pageSetup()
    {
        QPageSetupDialog dlg(&printer(), this);
        dlg.setWindowTitle(QStringLiteral("Параметри сторінки"));
        dlg.exec();
    }

    /** the file name on top of each page and "Сторінка N" below, as Notepad's &f and Page &p */
    void print()
    {
        QPrintDialog dlg(&printer(), this);
        dlg.setWindowTitle(QStringLiteral("Друк"));
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        QPrinter &p = printer();
        QPainter painter(&p);
        QFont f = m_edit->font();
        painter.setFont(f);
        const QRect page = p.pageLayout().paintRectPixels(p.resolution());
        const QRect area(0, 0, page.width(), page.height());
        const QFontMetrics fm(f, &p);
        const int lineH = fm.lineSpacing();
        const int top = lineH * 2, bottom = area.height() - lineH * 2;
        int y = top, number = 1;
        auto header = [&] {
            painter.drawText(QRect(0, 0, area.width(), lineH), Qt::AlignHCenter, shownName());
            painter.drawText(QRect(0, area.height() - lineH, area.width(), lineH), Qt::AlignHCenter, QStringLiteral("Сторінка %1").arg(number));
        };
        header();
        for (QTextBlock b = m_edit->document()->begin(); b.isValid(); b = b.next()) {
            // wrap long lines at the printable width
            QString rest = b.text();
            do {
                int n = rest.size();
                while (n > 1 && fm.horizontalAdvance(rest.left(n)) > area.width()) {
                    --n;
                }
                if (y + lineH > bottom) {
                    p.newPage();
                    ++number;
                    header();
                    y = top;
                }
                painter.drawText(0, y + fm.ascent(), rest.left(n));
                y += lineH;
                rest = rest.mid(n);
            } while (!rest.isEmpty());
        }
    }

    /* ---------------- find, replace, go to ---------------- */

    bool findNext(bool down, bool wrapMessage = true)
    {
        QTextDocument::FindFlags flags;
        if (!down) {
            flags |= QTextDocument::FindBackward;
        }
        if (m_matchCase) {
            flags |= QTextDocument::FindCaseSensitively;
        }
        if (m_edit->find(m_findText, flags)) {
            return true;
        }
        if (wrapMessage) {
            QMessageBox::information(m_findDialog ? static_cast<QWidget *>(m_findDialog) : this, QStringLiteral("Блокнот"),
                                     QStringLiteral("Не вдається знайти «%1»").arg(m_findText));
        }
        return false;
    }

    /** Find (comdlg32 1540) or Replace (1541), the parts Notepad uses */
    void findDialog(bool replace)
    {
        if (m_findDialog) {
            m_findDialog->close();
        }
        auto *d = new QDialog(this);
        m_findDialog = d;
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->setWindowTitle(replace ? QStringLiteral("Заміна") : QStringLiteral("Пошук"));
        const QString selected = m_edit->textCursor().selectedText();
        if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator)) {
            m_findText = selected;
        }
        if (!replace) {
            d->setFixedSize(dlu(0, 0, 250, 62).size());
            place(d, new QLabel(QStringLiteral("Що:")), dlu(4, 8, 42, 8));
            auto *what = place(d, new QLineEdit(m_findText), dlu(47, 7, 136, 12));
            auto *mc = place(d, new QCheckBox(QStringLiteral("З урахуванням регістру")), dlu(4, 42, 100, 12));
            mc->setChecked(m_matchCase);
            place(d, new QGroupBox(QStringLiteral("Напрямок")), dlu(107, 26, 76, 28));
            auto *up = place(d, new QRadioButton(QStringLiteral("Угору")), dlu(111, 38, 32, 12));
            auto *dn = place(d, new QRadioButton(QStringLiteral("Униз")), dlu(145, 38, 34, 12));
            (m_down ? dn : up)->setChecked(true);
            auto *next = place(d, new QPushButton(QStringLiteral("Знайти далі")), dlu(190, 5, 56, 14));
            auto *cancel = place(d, new QPushButton(QStringLiteral("Скасувати")), dlu(190, 23, 56, 14));
            next->setDefault(true);
            next->setEnabled(!what->text().isEmpty());
            connect(what, &QLineEdit::textChanged, next, [next](const QString &t) { next->setEnabled(!t.isEmpty()); });
            connect(next, &QPushButton::clicked, d, [this, what, mc, dn] {
                m_findText = what->text();
                m_matchCase = mc->isChecked();
                m_down = dn->isChecked();
                findNext(m_down);
            });
            connect(cancel, &QPushButton::clicked, d, &QDialog::close);
        } else {
            d->setFixedSize(dlu(0, 0, 240, 94).size());
            place(d, new QLabel(QStringLiteral("Що:")), dlu(4, 9, 48, 8));
            auto *what = place(d, new QLineEdit(m_findText), dlu(54, 7, 122, 12));
            place(d, new QLabel(QStringLiteral("Чим:")), dlu(4, 26, 48, 8));
            auto *with = place(d, new QLineEdit(m_replaceText), dlu(54, 24, 122, 12));
            auto *mc = place(d, new QCheckBox(QStringLiteral("З урахуванням регістру")), dlu(5, 62, 110, 12));
            mc->setChecked(m_matchCase);
            auto *next = place(d, new QPushButton(QStringLiteral("Знайти далі")), dlu(182, 4, 54, 14));
            auto *one = place(d, new QPushButton(QStringLiteral("Замінити")), dlu(182, 21, 54, 14));
            auto *all = place(d, new QPushButton(QStringLiteral("Замінити все")), dlu(182, 38, 54, 14));
            auto *cancel = place(d, new QPushButton(QStringLiteral("Скасувати")), dlu(182, 55, 54, 14));
            next->setDefault(true);
            auto sync = [this, what, with, mc] {
                m_findText = what->text();
                m_replaceText = with->text();
                m_matchCase = mc->isChecked();
            };
            for (QPushButton *b : {next, one, all}) {
                b->setEnabled(!what->text().isEmpty());
                connect(what, &QLineEdit::textChanged, b, [b](const QString &t) { b->setEnabled(!t.isEmpty()); });
            }
            connect(next, &QPushButton::clicked, d, [this, sync] {
                sync();
                findNext(true);
            });
            connect(one, &QPushButton::clicked, d, [this, sync] {
                sync();
                const Qt::CaseSensitivity cs = m_matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
                if (m_edit->textCursor().selectedText().compare(m_findText, cs) == 0) {
                    m_edit->insertPlainText(m_replaceText);
                }
                findNext(true);
            });
            connect(all, &QPushButton::clicked, d, [this, sync] {
                sync();
                QTextCursor c = m_edit->textCursor();
                c.beginEditBlock();
                m_edit->moveCursor(QTextCursor::Start);
                while (findNext(true, false)) {
                    m_edit->insertPlainText(m_replaceText);
                }
                c.endEditBlock();
            });
            connect(cancel, &QPushButton::clicked, d, &QDialog::close);
        }
        d->show();
    }

    /** Go To Line (dialog 14) */
    void goToLine()
    {
        QDialog d(this);
        d.setWindowTitle(QStringLiteral("Перехід до рядка"));
        d.setFixedSize(dlu(0, 0, 166, 60).size());
        place(&d, new QLabel(QStringLiteral("Номер рядка:")), dlu(7, 7, 115, 8));
        auto *line = place(&d, new QLineEdit(QString::number(m_edit->textCursor().blockNumber() + 1)), dlu(7, 18, 151, 14));
        line->setValidator(new QIntValidator(1, 1 << 30, line));
        line->selectAll();
        auto *go = place(&d, new QPushButton(QStringLiteral("Перейти")), dlu(55, 39, 50, 14));
        auto *cancel = place(&d, new QPushButton(QStringLiteral("Скасувати")), dlu(109, 39, 50, 14));
        go->setDefault(true);
        connect(go, &QPushButton::clicked, &d, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
        while (d.exec() == QDialog::Accepted) {
            const int n = line->text().toInt();
            if (n >= 1 && n <= m_edit->document()->blockCount()) {
                QTextCursor c(m_edit->document()->findBlockByNumber(n - 1));
                m_edit->setTextCursor(c);
                return;
            }
            QMessageBox::information(&d, QStringLiteral("Блокнот - перехід до рядка"), QStringLiteral("Номер рядка перевищує загальну кількість рядків"));
        }
    }

    QPlainTextEdit *m_edit = nullptr;
    QSizeGrip *m_cornerGrip = nullptr;
    QLabel *m_position = nullptr;
    QAction *m_undo = nullptr, *m_cut = nullptr, *m_copy = nullptr, *m_paste = nullptr, *m_delete = nullptr;
    QAction *m_find = nullptr, *m_findNext = nullptr, *m_goTo = nullptr, *m_wrap = nullptr, *m_status = nullptr;
    QString m_file;
    Encoding m_encoding = Utf8;
    bool m_bom = false;
    bool m_crlf = false;
    QString m_findText, m_replaceText;
    bool m_matchCase = false;
    bool m_down = true;
    QPointer<QDialog> m_findDialog;
    std::unique_ptr<QPrinter> m_printer;
};

} // namespace Win7Notepad

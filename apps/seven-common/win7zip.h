#pragma once
/*
 * Compressed (zipped) Folders of Windows 7 (zipfldr.dll) in the desktop and Explorer context menus:
 *   a .zip file:  "Видобути все..." right after "Open" -> the "Extract Compressed (Zipped) Folders" wizard
 *   any items:    nothing here, "Send to -> Compressed (zipped) folder" is in the Send to menu
 * Shared by the desktop and Explorer menus. Extraction runs unzip (never overwrites files).
 */
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>
#include <KLocalizedString>

namespace Win7Zip
{
/** Windows 7 extracts only ZIP files by itself */
inline bool isArchive(const QString &path, const QString &mime)
{
    return mime == QLatin1String("application/zip") || path.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive);
}

/** the Aero wizard "Extract Compressed (Zipped) Folders": destination, "Show extracted files when complete" */
inline void extractAll(const QString &zip)
{
    const QFileInfo fi(zip);
    auto *dlg = new QDialog;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(i18nd("sevenplasma", "Extract Compressed (Zipped) Folders"));
    dlg->setWindowIcon(QIcon::fromTheme(QStringLiteral("application-zip")));
    dlg->setStyleSheet(QStringLiteral("QDialog { background: white; } #seven-zip-commands { background: #f0f0f0; border-top: 1px solid #dfdfdf; }"));
    auto *outer = new QVBoxLayout(dlg);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *body = new QVBoxLayout;
    body->setContentsMargins(40, 18, 30, 20);
    body->setSpacing(8);
    auto *head = new QLabel(i18nd("sevenplasma", "Select a Destination and Extract Files"));
    QFont hf = head->font();
    hf.setPixelSize(15);
    head->setFont(hf);
    head->setStyleSheet(QStringLiteral("color: #003399;"));
    body->addWidget(head);
    body->addSpacing(14);
    auto *label = new QLabel(i18nd("sevenplasma", "Files will be extracted to this &folder:"));
    body->addWidget(label);
    auto *row = new QHBoxLayout;
    auto *dest = new QLineEdit(fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName());
    label->setBuddy(dest);
    auto *browse = new QPushButton(i18nd("sevenplasma", "B&rowse..."));
    row->addWidget(dest, 1);
    row->addWidget(browse);
    body->addLayout(row);
    auto *show = new QCheckBox(i18nd("sevenplasma", "S&how extracted files when complete"));
    show->setChecked(true);
    body->addWidget(show);
    body->addStretch(1);
    outer->addLayout(body, 1);
    auto *commands = new QWidget;
    commands->setObjectName(QStringLiteral("seven-zip-commands"));
    commands->setAttribute(Qt::WA_StyledBackground);
    auto *cl = new QHBoxLayout(commands);
    cl->setContentsMargins(10, 10, 10, 10);
    cl->addStretch(1);
    auto *ok = new QPushButton(i18nd("sevenplasma", "&Extract"));
    auto *cancel = new QPushButton(i18nd("sevenplasma", "Cancel"));
    ok->setDefault(true);
    cl->addWidget(ok);
    cl->addWidget(cancel);
    outer->addWidget(commands);
    dlg->resize(560, 400);
    QObject::connect(browse, &QPushButton::clicked, dlg, [dlg, dest]() {
        const QString d = QFileDialog::getExistingDirectory(dlg, i18nd("sevenplasma", "Select a destination"), dest->text());
        if (!d.isEmpty()) {
            dest->setText(d);
        }
    });
    QObject::connect(cancel, &QPushButton::clicked, dlg, &QDialog::reject);
    QObject::connect(ok, &QPushButton::clicked, dlg, [dlg, dest, show, zip]() {
        const QString target = QDir::cleanPath(dest->text().trimmed());
        if (target.isEmpty() || QDir::isRelativePath(target)) {
            QMessageBox::warning(dlg, i18nd("sevenplasma", "Extract Compressed (Zipped) Folders"),
                                 i18nd("sevenplasma", "Windows cannot complete the extraction.\n\nThe destination folder path is invalid. Please specify a different path."));
            return;
        }
        if (!QDir().mkpath(target)) {
            QMessageBox::warning(dlg, i18nd("sevenplasma", "Extract Compressed (Zipped) Folders"),
                                 i18nd("sevenplasma", "Windows cannot complete the extraction.\n\nThe destination folder could not be created."));
            return;
        }
        const bool open = show->isChecked();
        dlg->hide();
        auto *p = new QProcess;
        QObject::connect(p, &QProcess::finished, p, [p, dlg, target, open, zip](int code) {
            p->deleteLater();
            if (code > 1) { // 1 = warnings only (e.g. files skipped because they already exist)
                QMessageBox::warning(nullptr, i18nd("sevenplasma", "Extract Compressed (Zipped) Folders"),
                                     i18nd("sevenplasma", "Windows cannot complete the extraction.\n\nThe Compressed (zipped) Folder \"%1\" is invalid.").arg(QFileInfo(zip).fileName()));
            } else if (open) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(target));
            }
            dlg->close();
        });
        p->start(QStringLiteral("unzip"), {QStringLiteral("-n"), QStringLiteral("-q"), zip, QStringLiteral("-d"), target});
    });
    dlg->show();
}

/** "Видобути все..." for one ZIP file (the caller checks isArchive) */
inline void addExtractActions(QMenu *menu, const QString &path)
{
    if (QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty()) {
        return;
    }
    menu->addAction(i18nd("sevenplasma", "Extract All..."), menu, [path]() {
        extractAll(path);
    });
}

/** compressing is "Send to -> Compressed (zipped) folder", nothing to add here */
inline void addCompressActions(QMenu *, const QStringList &)
{
}
}

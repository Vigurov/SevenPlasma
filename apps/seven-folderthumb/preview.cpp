#include "compose.h"
#include <QGuiApplication>
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const QStringList files = Win7Folder::previewFiles(QString::fromLocal8Bit(argv[1]));
    const QStringList others = files.isEmpty() ? Win7Folder::otherFiles(QString::fromLocal8Bit(argv[1])) : QStringList();
    Win7Folder::compose(files, QByteArray(argv[3]).toInt(), 1.0, others).save(QString::fromLocal8Bit(argv[2]));
    return 0;
}

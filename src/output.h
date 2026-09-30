#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QString>

// PNG в памяти; пустой массив при ошибке кодирования.
QByteArray encodePng(const QImage& image);

struct WriteResult {
    enum Status { Ok, Exists, Error };
    Status status = Error;
    QString error; // текст для пользователя, если status != Ok
};

enum class WriteMode {
    NoReplace, // существующая запись в каталоге (файл, симлинк) → Exists
    Replace,   // заменить запись в каталоге; цель симлинка не трогается
};

// Атомарная запись: временный файл 0600 в том же каталоге (O_EXCL), fsync, затем renameat2.
// Путь назначения никогда не открывается напрямую, поэтому запись не может пойти «сквозь» симлинк.
// После вызова временных файлов .hot-screenshot-* не остаётся.
WriteResult writeFileAtomic(const QString& path, const QByteArray& data, WriteMode mode);

// «2026-09-30_14-05-33.png»; при attempt > 0 — «2026-09-30_14-05-33_<attempt>.png».
QString quickSaveFileName(const QDateTime& now, int attempt);

// Сохраняет в dir под первым свободным именем (attempt 0..99) без перезаписи.
WriteResult quickSave(const QImage& image, const QString& dir, const QDateTime& now, QString* savedPath);

// ~/Pictures/Screenshots (создаётся при необходимости).
QString screenshotsDir();

void copyToClipboard(const QImage& image);

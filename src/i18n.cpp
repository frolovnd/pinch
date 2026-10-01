#include "i18n.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

QStringList installTranslations(QCoreApplication& app, const QLocale& locale)
{
    QStringList loaded;
    // Переводчики принадлежат app: живут до его уничтожения, утечек нет.
    auto install = [&](const QString& name, bool ok, QTranslator* t) {
        if (!ok) {
            delete t;
            return;
        }
        app.installTranslator(t);
        loaded.append(name);
    };

    // Английская таблица — база и запасной вариант: ключи, которых нет в выбранном языке, берутся из неё.
    auto* en = new QTranslator(&app);
    install(QStringLiteral("pinch_en"), en->load(QStringLiteral(":/i18n/pinch_en.qm")), en);

    // Таблица для языка локали ставится позже английской, то есть имеет больший приоритет.
    auto* own = new QTranslator(&app);
    const bool ownOk = own->load(locale, QStringLiteral("pinch"), QStringLiteral("_"), QStringLiteral(":/i18n"))
        && !own->filePath().endsWith(QLatin1String("pinch_en.qm"));
    install(QFileInfo(own->filePath()).completeBaseName(), ownOk, own);

    // Стандартные диалоги Qt (Сохранить как, подтверждение перезаписи).
    auto* qt = new QTranslator(&app);
    const bool qtOk = qt->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                               QLibraryInfo::path(QLibraryInfo::TranslationsPath));
    install(QFileInfo(qt->filePath()).completeBaseName(), qtOk, qt);
    return loaded;
}

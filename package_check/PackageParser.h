#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Один ComponentEntry пакета.
// В примерах SDK встречаются ModuleType: NRX, MGD, NSF и LISP.
// Тип берётся из манифеста как есть: .dll может быть и NRX, и MGD.
struct PackageModule
{
    QString type;     // NRX, MGD, NSF, LISP или иной тип из XML.
    QString appName;  // Атрибут AppName, подпись в отчёте.
    QString path;     // Абсолютный путь к ModuleName.
};

// Результат разбора .package.
// Файл — XML-манифест nanoCAD, не архив. Корень пакета — каталог,
// в котором лежит сам .package: от него считаются FileName и ModuleName.
struct PackageDescription
{
    QString name;                  // Атрибут Name у ApplicationPackage.
    QString directory;             // Каталог пакета, база для относительных путей.
    QStringList cfgFiles;          // Абсолютные пути cfg из ConfigEntry.
    QList<PackageModule> modules;  // ComponentEntry в порядке файла.
    QString error;                 // Пусто, если XML разобран и в пакете есть cfg или модули.
};

// Команды, собранные из одного cfg.
// Одинаковые имена (без учёта регистра) в каждом списке встречаются один раз,
// порядок — порядок появления в файле.
struct ParsedCfg
{
    QString path;
    QStringList catalog;    // \ConfigMan\Commands\Command_* / Intername — каталог команд.
    QStringList menu;       // \Menu\... / Intername — пункты меню.
    QStringList toolbars;   // \Toolbars\... / Intername — кнопки тулбаров.
    QStringList cuixFiles;  // Абсолютные пути из ключа CUIX, %CFG_PATH% уже раскрыт.
    QString error;
};

// Команды ленты из одного cuix.
// cuix — zip (OPC). Команда кнопки — атрибут MenuMacroID в *.cui.
struct ParsedCuix
{
    QString path;
    QStringList commands;
    QString error;
};

PackageDescription readPackageFile(const QString& packagePath);
ParsedCfg readCfgFile(const QString& cfgPath);
ParsedCuix readCuixFile(const QString& cuixPath);

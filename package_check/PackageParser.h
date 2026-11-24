#pragma once

#include <QString>
#include <QStringList>

// Результат разбора .package.
// Файл — XML-манифест nanoCAD, не архив. Корень пакета — каталог,
// в котором лежит сам .package: от него считаются FileName и ModuleName.
struct PackageDescription
{
    QString name;            // Атрибут Name у ApplicationPackage.
    QString directory;       // Каталог пакета, база для относительных путей.
    QStringList cfgFiles;    // Абсолютные пути cfg из ConfigEntry.
    QStringList nrxModules;  // Абсолютные пути nrx из ComponentEntry с ModuleType="NRX".
    QString error;           // Пусто, если XML разобран и в пакете есть cfg или nrx.
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

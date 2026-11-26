#include "stdafx.h"
#include "package_check.h"
#include "PackageParser.h"

#include "hostQt.h"
#include "nrxgate/nc2ac.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {

// Исход загрузки одного nrx. loaded == false не всегда значит, что модуль
// не работает: loadModule возвращает ошибку и для уже загруженного модуля.
struct ModuleLoadResult
{
    QString type;
    QString path;
    bool loaded = false;
    QString message;
};

// Сливает списки из нескольких cfg одного пакета, сохраняя первое написание имени.
void appendUnique(QStringList& list, const QStringList& extra)
{
    for (const QString& item : extra)
    {
        if (!list.contains(item, Qt::CaseInsensitive))
            list.append(item);
    }
}

bool runHostCommand(const std::wstring& commandLine);

// (getcname "имя") возвращает локальное имя команды или nil, если её нет.
// Так видны команды LISP, .NET и NSF, которых нет в стеке NRX.
// ncedGetCName в nanoCAD не реализован, поэтому вызываем саму LISP-функцию.
bool lispGetCName(const std::wstring& commandName)
{
    if (commandName.empty())
        return false;

    QString escaped = QString::fromStdWString(commandName);
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    escaped.replace(QLatin1Char('"'), QLatin1String("\\\""));

    const QString expression = QString::fromWCharArray(
        L"(setq PKGCHECK_CNAME (getcname \"%1\"))").arg(escaped);
    if (!runHostCommand(expression.toStdWString()))
        return false;

    resbuf* value = nullptr;
    if (ncedGetSym(L"PKGCHECK_CNAME", &value) != RTNORM || value == nullptr)
        return false;

    const bool found = value->restype == RTSTR
        && value->resval.rstring != nullptr
        && value->resval.rstring[0] != L'\0';
    ncutRelRb(value);
    return found;
}

// Командная строка ищет имя по всем группам и без учёта регистра.
// lookupLocalCmd и lookupGlobalCmd смотрят стек NRX.
// iterator() и ncedGetCName в nanoCAD не реализованы и поднимают NOT_IMPLEMENTED.
bool isCommandRegistered(const QString& name)
{
    if (acedRegCmds == nullptr || name.isEmpty())
        return false;

    // Стек команд nanoCAD хранит имена в верхнем регистре.
    // const std::wstring local = name.toUpper().toStdWString();
    const std::wstring local = name.toStdWString();
    const std::wstring global = std::wstring(L"_") + local;

    if (acedRegCmds->lookupLocalCmd(local.c_str()) != nullptr)
        return true;
    if (acedRegCmds->lookupGlobalCmd(local.c_str()) != nullptr)
        return true;
    if (acedRegCmds->lookupLocalCmd(global.c_str()) != nullptr)
        return true;
    if (acedRegCmds->lookupGlobalCmd(global.c_str()) != nullptr)
        return true;

#if defined(CURRENT_NC26)
    return lispGetCName(local) || lispGetCName(global);
#else
    return false;
#endif

}

// Команды, которых нет в стеке nanoCAD. Порядок исходного списка сохраняется,
// чтобы в отчёте было видно, в каком месте меню или тулбара они стоят.
QStringList missingCommands(const QStringList& names)
{
    QStringList missing;
    for (const QString& name : names)
    {
        if (!isCommandRegistered(name))
            missing.append(name);
    }
    return missing;
}

// Блок отчёта: заголовок, счётчик и либо перечень пропусков, либо короткая отметка.
// Пустой список — отдельный случай: это не «все зарегистрированы», а «в cfg ничего не было».
QString formatCommandSection(const QString& title, const QStringList& names, const QStringList& missing)
{
    QString text = title;
    text += QString::fromWCharArray(L": %1, не зарегистрировано %2\n")
                .arg(names.size())
                .arg(missing.size());
    if (names.isEmpty())
        text += QString::fromWCharArray(L"  список пуст\n");
    else if (missing.isEmpty())
        text += QString::fromWCharArray(L"  все команды зарегистрированы\n");
    else
    {
        for (const QString& name : missing)
            text += QString::fromWCharArray(L"  %1\n").arg(name);
    }
    text += QLatin1Char('\n');
    return text;
}

// ncedCmd выполняет команду синхронно и забирает следующие RTSTR как ответы на запросы.
// Так NETLOAD и NSF получают путь к файлу и не открывают диалог выбора.
// Строка должна жить, пока ncutBuildList её копирует в resbuf.
bool runHostCommand(const std::wstring& commandLine)
{
    resbuf* arguments = ncutBuildList(RTSTR, commandLine.c_str(), RTNONE);
    if (arguments == nullptr)
        return false;

    const int code = ncedCmd(arguments);
    ncutRelRb(arguments);
    return code == RTNORM;
}

bool runHostCommand(const wchar_t* command, const std::wstring& argument)
{
    resbuf* arguments = ncutBuildList(RTSTR, command, RTSTR, argument.c_str(), RTNONE);
    if (arguments == nullptr)
        return false;

    const int code = ncedCmd(arguments);
    ncutRelRb(arguments);
    return code == RTNORM;
}

// (load "path") — встроенная функция LISP. Путь с прямыми слэшами,
// кавычки и обратные слэши экранируются, иначе выражение обрывается.
bool loadLispFile(const QString& path)
{
    QString escaped = QDir::fromNativeSeparators(path);
    escaped.replace(QLatin1Char('\\'), QLatin1Char('/'));
    escaped.replace(QLatin1String("\""), QLatin1String("\\\""));
    const QString expression = QString::fromWCharArray(L"(load \"%1\")").arg(escaped);
    return runHostCommand(expression.toStdWString());
}

ModuleLoadResult loadPackageModule(const PackageModule& module)
{
    ModuleLoadResult result;
    result.path = module.path;
    result.type = module.type;

    if (!QFileInfo::exists(module.path))
    {
        result.message = QString::fromWCharArray(L"файл не найден");
        return result;
    }

    const QString native = QDir::toNativeSeparators(QFileInfo(module.path).absoluteFilePath());
    const std::wstring modulePath = native.toStdWString();

    // NRX регистрирует команды в acrxEntryPoint. loadModule подходит и для .nrx, и для .dll,
    // если в пакете у сборки указан тип NRX. printit = false: текст загрузки пишем сами.
    if (module.type == QLatin1String("NRX"))
    {
        if (acrxDynamicLinker == nullptr)
        {
            result.message = QString::fromWCharArray(L"динамический компоновщик недоступен");
            return result;
        }
        result.loaded = acrxDynamicLinker->loadModule(modulePath.c_str(), false);
        if (!result.loaded)
            result.message = QString::fromWCharArray(L"loadModule вернул ошибку (модуль мог быть уже загружен)");
        return result;
    }

    // MGD — управляемая сборка .NET. Команда NETLOAD есть в справке nanoCAD.
    // Глобальное имя с подчёркиванием не зависит от языка интерфейса.
    if (module.type == QLatin1String("MGD"))
    {
        result.loaded = runHostCommand(L"_NETLOAD", modulePath);
        if (!result.loaded)
            result.message = QString::fromWCharArray(L"NETLOAD не выполнился");
        return result;
    }

    // NSF — XML-пакет скриптовых команд (JScript/VBScript). Его грузит команда NSF.
    if (module.type == QLatin1String("NSF"))
    {
        result.loaded = runHostCommand(L"_NSF", modulePath);
        if (!result.loaded)
            result.message = QString::fromWCharArray(L"NSF не выполнился");
        return result;
    }

    // LISP доступен на всю сессию nanoCAD, не только в текущем чертеже.
    if (module.type == QLatin1String("LISP"))
    {
        result.loaded = loadLispFile(native);
        if (!result.loaded)
            result.message = QString::fromWCharArray(L"(load ...) не выполнился");
        return result;
    }

    result.message = QString::fromWCharArray(L"неизвестный тип модуля");
    return result;
}

// Диалог без собственного Q_OBJECT: кнопка связывается с QDialog::accept.
// Так файл не нужно добавлять в секцию QtMoc проекта.
void showReport(QWidget* parent, const QString& report)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QString::fromWCharArray(L"PACKAGE_CHECK"));
    dialog.resize(760, 560);

    auto* layout = new QVBoxLayout(&dialog);
    auto* text = new QTextEdit(&dialog);
    text->setReadOnly(true);
    text->setPlainText(report);
    layout->addWidget(text);

    auto* closeButton = new QPushButton(QString::fromWCharArray(L"Закрыть"), &dialog);
    QObject::connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(closeButton);
    dialog.exec();
}

// Команда PACKAGE_CHECK.
// Сначала разбираем пакет и cfg, затем загружаем модули и только после этого
// сверяем имена со стеком команд: регистрация происходит при загрузке NRX, .NET, NSF и LISP.
void package_check_cmd()
{
    // QWinWidget привязывает Qt-диалоги к главному окну nanoCAD,
    // иначе файл и отчёт открываются без родителя и уходят под кадр.
    QWinWidget host(ndsw_ncadMainWnd());
    host.showCentered();

    const QString packagePath = QFileDialog::getOpenFileName(
        &host,
        QString::fromWCharArray(L"Пакет нанокад"),
        QString(),
        QString::fromWCharArray(L"Пакеты (*.package);;Все файлы (*.*)"));
    if (packagePath.isEmpty())
        return;

    const PackageDescription package = readPackageFile(packagePath);
    // Частичный разбор ещё полезен: например, XML оборвался после списка модулей.
    // Полностью пустой результат вместе с ошибкой показывать в отчёте нечего.
    if (!package.error.isEmpty() && package.cfgFiles.isEmpty() && package.modules.isEmpty())
    {
        QMessageBox::warning(
            &host,
            QString::fromWCharArray(L"PACKAGE_CHECK"),
            package.error);
        acutPrintf(L"\nPACKAGE_CHECK: %s\n", package.error.toStdWString().c_str());
        return;
    }

    QStringList catalog;
    QStringList menu;
    QStringList toolbars;
    QStringList cuixFiles;
    QStringList notes;
    bool hasErrors = !package.error.isEmpty();
    if (!package.error.isEmpty())
        notes.append(package.error);

    for (const QString& cfgPath : package.cfgFiles)
    {
        const ParsedCfg cfg = readCfgFile(cfgPath);
        appendUnique(catalog, cfg.catalog);
        appendUnique(menu, cfg.menu);
        appendUnique(toolbars, cfg.toolbars);
        appendUnique(cuixFiles, cfg.cuixFiles);
        if (!cfg.error.isEmpty())
        {
            notes.append(cfg.error);
            hasErrors = true;
        }
    }

    QStringList ribbon;
    // Отсутствие ленты — замечание, не ошибка разбора: пакет может быть без cuix.
    if (cuixFiles.isEmpty())
        notes.append(QString::fromWCharArray(L"Ссылка на cuix в cfg не найдена."));

    for (const QString& cuixPath : cuixFiles)
    {
        const ParsedCuix cuix = readCuixFile(cuixPath);
        appendUnique(ribbon, cuix.commands);
        if (!cuix.error.isEmpty())
        {
            notes.append(cuix.error);
            hasErrors = true;
        }
    }

    // Модули грузим после разбора файлов и в порядке пакета:
    // объектный модуль NRX должен успеть загрузиться раньше команд, которые на него ссылаются.
    // Неудачная загрузка не прерывает проверку: команды могли зарегистрироваться при старте nanoCAD.
    QList<ModuleLoadResult> loadedModules;
    for (const PackageModule& module : package.modules)
        loadedModules.append(loadPackageModule(module));

    const QStringList catalogMissing = missingCommands(catalog);
    const QStringList menuMissing = missingCommands(menu);
    const QStringList toolbarMissing = missingCommands(toolbars);
    const QStringList ribbonMissing = missingCommands(ribbon);

    QString report;
    report += QString::fromWCharArray(L"Пакет: %1\n")
                  .arg(package.name.isEmpty() ? QFileInfo(packagePath).fileName() : package.name);
    report += QString::fromWCharArray(L"Файл: %1\n\n").arg(packagePath);

    report += QString::fromWCharArray(L"Модули:\n");
    if (loadedModules.isEmpty())
        report += QString::fromWCharArray(L"  в пакете нет модулей\n");
    for (const ModuleLoadResult& module : loadedModules)
    {
        const QString status = module.loaded
            ? QString::fromWCharArray(L"загружен")
            : module.message;
        const QString type = module.type.isEmpty()
            ? QString::fromWCharArray(L"?")
            : module.type;
        report += QString::fromWCharArray(L"  [%1] %2 — %3\n").arg(type, module.path, status);
    }
    report += QLatin1Char('\n');

    if (!notes.isEmpty())
    {
        report += QString::fromWCharArray(L"Замечания:\n");
        for (const QString& note : notes)
            report += QString::fromWCharArray(L"  %1\n").arg(note);
        report += QLatin1Char('\n');
    }

    report += formatCommandSection(QString::fromWCharArray(L"Каталог cfg"), catalog, catalogMissing);
    report += formatCommandSection(QString::fromWCharArray(L"Меню"), menu, menuMissing);
    report += formatCommandSection(QString::fromWCharArray(L"Тулбары"), toolbars, toolbarMissing);
    report += formatCommandSection(QString::fromWCharArray(L"Лента"), ribbon, ribbonMissing);

    // В строке статуса числитель — сколько имён нашлось в стеке, знаменатель — сколько было в списке.
    const int catalogRegistered = catalog.size() - catalogMissing.size();
    const int menuRegistered = menu.size() - menuMissing.size();
    const int toolbarRegistered = toolbars.size() - toolbarMissing.size();
    const int ribbonRegistered = ribbon.size() - ribbonMissing.size();
    acutPrintf(
        L"\nPACKAGE_CHECK: каталог %d/%d, меню %d/%d, тулбары %d/%d, лента %d/%d\n",
        catalogRegistered,
        catalog.size(),
        menuRegistered,
        menu.size(),
        toolbarRegistered,
        toolbars.size(),
        ribbonRegistered,
        ribbon.size());

    bool loadFailed = false;
    for (const ModuleLoadResult& module : loadedModules)
        loadFailed = loadFailed || !module.loaded;
    if (loadFailed || hasErrors)
        acutPrintf(L"PACKAGE_CHECK: есть ошибки загрузки или разбора, подробности в окне отчёта.\n");

    showReport(&host, report);
}

} // namespace

package_check::package_check()
{
}

// Точка входа NRX. nanoCAD вызывает её при APPLOAD и при выгрузке модуля.
extern "C" __declspec(dllexport) AcRx::AppRetCode
acrxEntryPoint(AcRx::AppMsgCode msg, void* appId)
{
    switch (msg)
    {
    case AcRx::kInitAppMsg:
        // unlockApplication разрешает выгрузку командой UNLOAD.
        // registerAppMDIAware оставляет модуль загруженным при смене чертежа.
        acrxDynamicLinker->unlockApplication(appId);
        acrxDynamicLinker->registerAppMDIAware(appId);
        // Группа PACKAGE_CHECK, глобальное имя _PACKAGE_CHECK, локальное PACKAGE_CHECK.
        // Локальное имя — то, что набирают в командной строке.
        acedRegCmds->addCommand(
            L"PACKAGE_CHECK",
            L"_PACKAGE_CHECK",
            L"PACKAGE_CHECK",
            ACRX_CMD_TRANSPARENT,
            package_check_cmd);
        break;
    case AcRx::kUnloadAppMsg:
        acedRegCmds->removeGroup(L"PACKAGE_CHECK");
        break;
    default:
        break;
    }

    return AcRx::kRetOK;
}

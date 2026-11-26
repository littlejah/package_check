#include "stdafx.h"
#include "PackageParser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextCodec>
#include <QXmlStreamReader>

// Заголовки QuaZip по умолчанию помечают символы как dllimport.
// В проекте подключена статическая quazip1-qt5.lib, поэтому импорт отключаем.
#define QUAZIP_STATIC
#include <quazip.h>
#include <quazipfile.h>

namespace {

// Добавляет непустое имя, если такого ещё нет. Сравнение без регистра:
// в nanoCAD APP_INSERT_DM и app_insert_dm — одна команда.
void appendUnique(QStringList& list, const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty())
        return;
    if (!list.contains(trimmed, Qt::CaseInsensitive))
        list.append(trimmed);
}

// Имя атрибута в пакете и в cui не зависит от регистра.
QString xmlAttribute(const QXmlStreamAttributes& attributes, const char* name)
{
    const QLatin1String expected(name);
    for (const QXmlStreamAttribute& attribute : attributes)
    {
        if (attribute.name().compare(expected, Qt::CaseInsensitive) == 0)
            return attribute.value().toString();
    }
    return QString();
}

// Строгая проверка UTF-8. QString::fromUtf8 не сообщает об ошибке и
// подставляет символы замены, поэтому кодировку cfg определяем сами.
// Типичный cfg nanoCAD записан в Windows-1251: байты кириллицы (0xC0–0xFF)
// похожи на начало UTF-8-последовательности, но следующий байт не является
// продолжением, и такая проверка их отсекает. Чистый ASCII проходит как UTF-8.
bool isValidUtf8(const QByteArray& data)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.constData());
    const int size = data.size();
    int index = 0;
    while (index < size)
    {
        const unsigned char lead = bytes[index];
        int extra = 0;
        if (lead <= 0x7F)
        {
            ++index;
            continue;
        }
        if ((lead & 0xE0) == 0xC0)
        {
            // 0xC0 и 0xC1 кодируют ASCII длиннее, чем нужно, это не UTF-8.
            if (lead < 0xC2)
                return false;
            extra = 1;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            extra = 2;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            if (lead > 0xF4)
                return false;
            extra = 3;
        }
        else
        {
            return false;
        }

        if (index + extra >= size)
            return false;
        for (int shift = 1; shift <= extra; ++shift)
        {
            if ((bytes[index + shift] & 0xC0) != 0x80)
                return false;
        }
        // Отсекаем overlong-последовательности и суррогаты UTF-16.
        if (lead == 0xE0 && bytes[index + 1] < 0xA0)
            return false;
        if (lead == 0xED && bytes[index + 1] >= 0xA0)
            return false;
        if (lead == 0xF0 && bytes[index + 1] < 0x90)
            return false;
        if (lead == 0xF4 && bytes[index + 1] >= 0x90)
            return false;
        index += extra + 1;
    }
    return true;
}

// BOM имеет приоритет над эвристикой. Без BOM пробуем UTF-8,
// иначе Windows-1251 — обычная кодировка cfg в nanoCAD.
QString decodeTextFile(const QByteArray& data)
{
    if (data.startsWith("\xEF\xBB\xBF"))
        return QString::fromUtf8(data.constData() + 3, data.size() - 3);

    if (data.size() >= 2
        && static_cast<unsigned char>(data[0]) == 0xFF
        && static_cast<unsigned char>(data[1]) == 0xFE)
    {
        // mid() даёт новый буфер с выравниванием, безопасным для ushort.
        QByteArray payload = data.mid(2);
        if (payload.size() % 2 != 0)
            payload.chop(1);
        return QString::fromUtf16(
            reinterpret_cast<const ushort*>(payload.constData()),
            payload.size() / 2);
    }

    if (isValidUtf8(data))
        return QString::fromUtf8(data);

    if (QTextCodec* codec = QTextCodec::codecForName("Windows-1251"))
        return codec->toUnicode(data);
    return QString::fromLocal8Bit(data);
}

// Путь из пакета или cfg. "/config/app.cfg" в манифесте — не корень диска,
// а путь от каталога пакета, поэтому ведущие слэши снимаются.
// Абсолютным считается только путь с буквой диска или UNC (\\server\share).
QString resolveAgainstDirectory(const QString& directory, const QString& rawPath)
{
    QString path = rawPath.trimmed();
    path.replace('\\', '/');

    const bool unc = path.startsWith(QLatin1String("//"));
    const bool drive = path.size() >= 2 && path.at(1) == QLatin1Char(':');
    if (!unc && !drive)
    {
        while (path.startsWith(QLatin1Char('/')))
            path.remove(0, 1);
        path = directory + QLatin1Char('/') + path;
    }
    return QDir::cleanPath(path);
}

// В cfg путь к ленте записан как CUIX=s%CFG_PATH%\ribbon.cuix.
// %CFG_PATH% — каталог самого cfg, не каталог пакета.
QString expandCfgPath(const QString& cfgDirectory, const QString& rawValue)
{
    QString path = rawValue.trimmed();
    const QLatin1String marker("%CFG_PATH%");
    int position = 0;
    while ((position = path.indexOf(marker, position, Qt::CaseInsensitive)) >= 0)
    {
        path.replace(position, marker.size(), cfgDirectory);
        position += cfgDirectory.size();
    }
    return resolveAgainstDirectory(cfgDirectory, path);
}

// Значение cfg имеет вид <тип><данные>: s — строка, i — целое, f — флаг.
// Для Intername и CUIX нужен текст после префикса: sAPP_INSERT_DM -> APP_INSERT_DM.
QString decodeCfgValue(const QString& raw)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty())
        return trimmed;

    const QChar type = trimmed.at(0).toLower();
    if (type == QLatin1Char('s') || type == QLatin1Char('i') || type == QLatin1Char('f'))
        return trimmed.mid(1);
    return trimmed;
}

// [\ConfigMan\Commands] — группа, не команда. Команда — любая дочерняя секция:
// [\ConfigMan\Commands\Command_1] или [\configman\commands\HelloNRX].
bool isCatalogSection(const QString& section)
{
    return section.startsWith(
        QLatin1String("\\ConfigMan\\Commands\\"),
        Qt::CaseInsensitive);
}

bool isMenuSection(const QString& section)
{
    return section.compare(QLatin1String("\\Menu"), Qt::CaseInsensitive) == 0
        || section.startsWith(QLatin1String("\\Menu\\"), Qt::CaseInsensitive);
}

// [\Toolbars\GTM\ToolbarButton_100] хранит команду кнопки в Intername.
// [\ToolbarsPos\...] — только положение панелей, префикс \Toolbars\ ей не подходит,
// поэтому позиции в список команд не попадают. У разделителей [\toolbars\...\sepN]
// поля Intername нет.
bool isToolbarSection(const QString& section)
{
    return section.compare(QLatin1String("\\Toolbars"), Qt::CaseInsensitive) == 0
        || section.startsWith(QLatin1String("\\Toolbars\\"), Qt::CaseInsensitive);
}

} // namespace

PackageDescription readPackageFile(const QString& packagePath)
{
    PackageDescription result;
    result.directory = QFileInfo(packagePath).absolutePath();

    QFile file(packagePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        result.error = QString::fromWCharArray(L"Не удалось открыть файл пакета.");
        return result;
    }

    // QXmlStreamReader сам читает кодировку из объявления XML.
    // Пространство имён hostApplicationPackage/v01 на локальные имена не влияет.
    QXmlStreamReader xml(&file);
    while (!xml.atEnd())
    {
        xml.readNext();
        if (!xml.isStartElement())
            continue;

        const QString element = xml.name().toString();
        if (element.compare(QLatin1String("ApplicationPackage"), Qt::CaseInsensitive) == 0)
        {
            result.name = xmlAttribute(xml.attributes(), "Name");
            continue;
        }

        if (element.compare(QLatin1String("ConfigEntry"), Qt::CaseInsensitive) == 0)
        {
            const QString fileName = xmlAttribute(xml.attributes(), "FileName");
            if (!fileName.isEmpty())
                result.cfgFiles.append(resolveAgainstDirectory(result.directory, fileName));
            continue;
        }

        // Тип модуля решает, чем его грузить. Расширение файла для этого не используется:
        // в пакете SDK есть и .dll с ModuleType="NRX", и .dll с ModuleType="MGD".
        if (element.compare(QLatin1String("ComponentEntry"), Qt::CaseInsensitive) == 0)
        {
            const QString moduleName = xmlAttribute(xml.attributes(), "ModuleName");
            if (moduleName.isEmpty())
                continue;

            PackageModule module;
            module.type = xmlAttribute(xml.attributes(), "ModuleType").trimmed().toUpper();
            module.appName = xmlAttribute(xml.attributes(), "AppName").trimmed();
            module.path = resolveAgainstDirectory(result.directory, moduleName);
            result.modules.append(module);
        }
    }

    if (xml.hasError())
        result.error = xml.errorString();
    else if (result.cfgFiles.isEmpty() && result.modules.isEmpty())
        result.error = QString::fromWCharArray(L"В пакете нет cfg и модулей.");

    return result;
}

ParsedCfg readCfgFile(const QString& cfgPath)
{
    ParsedCfg result;
    result.path = cfgPath;

    QFile file(cfgPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        result.error = QString::fromWCharArray(L"Не удалось открыть cfg: %1").arg(cfgPath);
        return result;
    }

    const QString text = decodeTextFile(file.readAll());
    QString normalized = text;
    normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    // В cfg nanoCAD вертикальная черта — перевод строки:
    // [\menu\A] |name=sA |intername=sCmd  это секция и два поля.
    normalized.replace(QLatin1Char('|'), QLatin1Char('\n'));

    const QString cfgDirectory = QFileInfo(cfgPath).absolutePath();
    QString section;
    const QStringList lines = normalized.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines)
    {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;

        // Секция: [\Menu\MainMenu_2\Project\MenuItem_1]
        // В section остаётся путь без скобок, он действует до следующей секции.
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
        {
            section = line.mid(1, line.size() - 2);
            continue;
        }

        const int separator = line.indexOf(QLatin1Char('='));
        if (separator < 0)
            continue;

        // Пробелы вокруг '=' встречаются (Visible = f1), поэтому ключ обрезаем.
        const QString key = line.left(separator).trimmed();
        const QString value = decodeCfgValue(line.mid(separator + 1));
        if (key.compare(QLatin1String("Intername"), Qt::CaseInsensitive) == 0)
        {
            // Одна и та же строка Intername относится только к текущей секции.
            // Каталог, меню и тулбары — разные ветви cfg и не пересекаются.
            if (isCatalogSection(section))
                appendUnique(result.catalog, value);
            if (isMenuSection(section))
                appendUnique(result.menu, value);
            if (isToolbarSection(section))
                appendUnique(result.toolbars, value);
            continue;
        }

        // CUIX может стоять не только в [\ribbon\...]. Берём любой такой ключ:
        // это и есть ссылка на файл ленты.
        if (key.compare(QLatin1String("CUIX"), Qt::CaseInsensitive) == 0)
            appendUnique(result.cuixFiles, expandCfgPath(cfgDirectory, value));
    }

    return result;
}

ParsedCuix readCuixFile(const QString& cuixPath)
{
    ParsedCuix result;
    result.path = cuixPath;

    if (!QFileInfo::exists(cuixPath))
    {
        result.error = QString::fromWCharArray(L"Файл cuix не найден: %1").arg(cuixPath);
        return result;
    }

    QuaZip zip(cuixPath);
    if (!zip.open(QuaZip::mdUnzip))
    {
        result.error = QString::fromWCharArray(L"Не удалось открыть cuix: %1").arg(cuixPath);
        return result;
    }

    bool foundCui = false;
    QStringList errors;
    for (bool more = zip.goToFirstFile(); more; more = zip.goToNextFile())
    {
        const QString entryName = zip.getCurrentFileName();
        // В архиве также лежат [Content_Types].xml и Menu_Package_Info.xml.
        // Команды кнопок записаны только в *.cui (обычно RibbonRoot.cui).
        if (!entryName.endsWith(QLatin1String(".cui"), Qt::CaseInsensitive))
            continue;

        foundCui = true;
        QuaZipFile entry(&zip);
        if (!entry.open(QIODevice::ReadOnly))
        {
            errors.append(QString::fromWCharArray(L"Не удалось прочитать %1").arg(entryName));
            continue;
        }

        // Читаем файл целиком и закрываем его до перехода к следующему:
        // QuaZip не держит несколько записей архива открытыми одновременно.
        const QByteArray body = entry.readAll();
        entry.close();

        QXmlStreamReader xml(body);
        while (!xml.atEnd())
        {
            xml.readNext();
            if (!xml.isStartElement())
                continue;
            // MenuMacroID стоит и на обычной кнопке, и на кнопке внутри RibbonSplitButton.
            // При resolved="1" это локальное имя команды, а не ссылка на отдельный макрос.
            appendUnique(result.commands, xmlAttribute(xml.attributes(), "MenuMacroID"));
        }
        if (xml.hasError())
            errors.append(entryName + QLatin1String(": ") + xml.errorString());
    }
    zip.close();

    if (!foundCui)
        errors.append(QString::fromWCharArray(L"В архиве нет файлов .cui"));
    if (!errors.isEmpty())
        result.error = errors.join(QLatin1Char('\n'));
    return result;
}

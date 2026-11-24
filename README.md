# package_check

NRX-модуль для nanoCAD. Команда `PACKAGE_CHECK` проверяет пакет приложения: загружает его nrx-модули и показывает, какие команды из cfg и ленты не зарегистрированы в nanoCAD.

## Команда

В командной строке nanoCAD:

```text
PACKAGE_CHECK
```

Команда открывает диалог выбора файла `*.package`. Дальше она работает так:

1. Читает XML пакета и находит cfg и nrx-модули. Пути без диска считаются от каталога, где лежит `.package`. Ведущий `/` в `FileName` отбрасывается: `/config/app.cfg` — это `config/app.cfg` рядом с пакетом.
2. Разбирает cfg и собирает четыре списка имён:
   - каталог команд — секции `\ConfigMan\Commands\Command_*`, поле `Intername`;
   - меню — секции `\Menu`, поле `Intername`;
   - тулбары — секции `\Toolbars`, поле `Intername` у кнопок. Разделители и `\ToolbarsPos` пропускаются;
   - лента — если в cfg есть `CUIX`, архив открывается и из всех `*.cui` читается атрибут `MenuMacroID`.
3. Загружает каждый nrx через `acrxDynamicLinker->loadModule`. Если загрузка не удалась, это попадает в отчёт, а сверка команд всё равно выполняется: модуль мог быть уже загружен.
4. Считает имя зарегистрированным, если его находит `lookupLocalCmd` или `lookupGlobalCmd`, в том числе с префиксом `_`.

Окно отчёта перечисляет незарегистрированные команды каталога, меню, тулбаров и ленты. В командную строку пишется сводка:

```text
PACKAGE_CHECK: каталог 80/82, меню 70/74, тулбары 40/41, лента 60/63
```

Числа — сколько имён найдено в стеке команд и сколько имён было в списке.

## Формат пакета

Файл `.package` — XML, не архив.

```xml
<ApplicationPackage Name="Приложение">
  <Components>
    <ConfigEntry FileName="/config/app.cfg" FileType="CFG"/>
    <ComponentEntry AppName="Модуль приложения"
                    ModuleName="module.nrx"
                    ModuleType="NRX"/>
  </Components>
</ApplicationPackage>
```

Рядом с пакетом ожидается такая раскладка:

```text
app.package
module.nrx
config/
  app.cfg
  app_ribbon.cuix
```

В cfg значение начинается с буквы типа: `s` — строка, `i` — число, `f` — флаг. Пример кнопки тулбара:

```text
[\Toolbars\GTM\ToolbarButton_100]
Name=sДМ
Intername=sAPP_INSERT_DM
```

Ссылка на ленту:

```text
[\ribbon\MyRibbon]
CUIX=s%CFG_PATH%\app_ribbon.cuix
```

`%CFG_PATH%` заменяется каталогом cfg. Файл cuix — zip, внутри него `RibbonRoot.cui`. Кодировка cfg: UTF-8, при неверной последовательности байт — Windows-1251.

Имя в меню, тулбаре и ленте совпадает с локальным именем команды, которое модуль передаёт в `acedRegCmds->addCommand`.

## Сборка

Решение: `package_check.sln`, платформа x64. Конфигурации собраны под SDK nanoCAD 24, 24.1, 25 и 26, Debug и Release.

Результат сборки — `x64/<Configuration>/package_check.nrx`. Модуль загружается в nanoCAD командой `APPLOAD` или через автозагрузку. После загрузки доступна команда `PACKAGE_CHECK`.

Зависимости, пути к которым прописаны в `package_check.vcxproj`:

- SDK nanoCAD (`C:\SDK24`, `C:\SDK24.1`, `C:\SDK25`, `C:\SDK26_RC` — по конфигурации);
- Qt 5.15;
- QuaZip 1.4 (статическая библиотека `quazip1-qt5.lib`) и zlib.

## Исходники

| Файл | Назначение |
| --- | --- |
| `package_check/package_check.cpp` | Точка входа NRX, команда, загрузка модулей и окно отчёта |
| `package_check/PackageParser.h`, `PackageParser.cpp` | Разбор `.package`, cfg и cuix |

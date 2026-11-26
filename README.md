
# package_check
Задача: прочитать *.package файл - и проверить какие команды в зарегистрированных модулях отсутсвуют. 
Вызов 1: Распарсить .cfg файлы с командами, тулбарами и меню. Все прекрасно, но возникает геморрой с кодировками.
Вызов 2: распарсить quix - QuaZip и QtXml в помощь достаем все MenuMacroID.
Вызов 3: Проверить списки зарегистрированных команд в наноКад. Казалось бы acedRegCmds->iterator() и радуемся, но, как скажут радиолюбители - Николай,Иван,Харитон,Ульяна,Яков, или по нанокадовски - исключение NOT_IMPLEMENTED.  Поэтому для того, что грузится через nrx - acedRegCmds->lookupCmd (...), для всего остального проверка через lisp - (getcname ....)
Вызов 4: (getcname ....) в nanocad 24 всегда возвhащает значение: (getcname "fuckoff") -> fuckoff, а (getcname "_fuckoff") -> fuckoff. Большое спасибо что в nanocad 26 уже как надо (getcname "fuckoff") -> nil  

Нижеследующие описание подготовлено при помощи протеза мозга


NRX-модуль для nanoCAD. Команда `PACKAGE_CHECK` проверяет пакет приложения: загружает его модули и показывает, какие команды из cfg и ленты не зарегистрированы в nanoCAD.

## Команда

В командной строке nanoCAD:

```text
PACKAGE_CHECK
```

Команда открывает диалог выбора файла `*.package`. Дальше она работает так:

1. Читает XML пакета и находит cfg и модули `NRX`, `MGD`, `NSF`, `LISP`. Пути без диска считаются от каталога, где лежит `.package`. Ведущий `/` в `FileName` отбрасывается: `/config/app.cfg` — это `config/app.cfg` рядом с пакетом.
2. Разбирает cfg и собирает четыре списка имён:
   - каталог команд — секции под `\ConfigMan\Commands\`, поле `Intername`;
   - меню — секции `\Menu`, поле `Intername`;
   - тулбары — секции `\Toolbars`, поле `Intername` у кнопок. Разделители и `\ToolbarsPos` пропускаются;
   - лента — если в cfg есть `CUIX`, архив открывается и из всех `*.cui` читается атрибут `MenuMacroID`.
3. Загружает модули в порядке пакета. `NRX` — через `acrxDynamicLinker->loadModule`, `MGD` — командой `NETLOAD`, `NSF` — командой `NSF`, `LISP` — выражением `(load ...)`. Если загрузка не удалась, это попадает в отчёт, а сверка команд всё равно выполняется: модуль мог быть уже загружен.
4. Для каждого имени из каталога, меню, тулбаров и ленты вызывает `isCommandRegistered`.

Окно отчёта перечисляет только те имена, которые проверка не подтвердила. В командную строку пишется сводка:

```text
PACKAGE_CHECK: каталог 80/82, меню 70/74, тулбары 40/41, лента 60/63
```

Числа — сколько имён проверка признала зарегистрированными и сколько имён было в списке.

## Проверка регистрации

Имя берётся как оно записано в cfg или в `MenuMacroID`, без приведения к верхнему регистру. Глобальный вариант — то же имя с префиксом `_`. Для `APP_INSERT_DM` проверяются `APP_INSERT_DM` и `_APP_INSERT_DM`.

Сначала имя ищется в стеке NRX. Достаточно одного успешного вызова:

- `lookupLocalCmd` по локальному имени;
- `lookupGlobalCmd` по локальному имени;
- `lookupLocalCmd` по глобальному имени;
- `lookupGlobalCmd` по глобальному имени.

Этого хватает для команд, которые модуль зарегистрировал через `acedRegCmds->addCommand`. Команды .NET, NSF и LISP в этот стек не попадают, хотя в командной строке они работают.

Если стек NRX имя не нашёл, выполняется LISP-функция `getcname` — отдельно для локального имени и для глобального:

```text
(setq PKGCHECK_CNAME (getcname "APP_INSERT_DM"))
(setq PKGCHECK_CNAME (getcname "_APP_INSERT_DM"))
```

Результат читается из символа `PKGCHECK_CNAME`. Непустая строка значит, что nanoCAD знает такую команду. `nil` и пустая строка значат, что имени нет. Кавычки и обратные слэши в имени экранируются, чтобы выражение LISP не оборвалось.

`NcEdCommandStack::iterator` и `ncedGetCName` не вызываются: в nanoCAD они не реализованы и открывают окно API Logger с `NOT_IMPLEMENTED`.

Одинаковые имена в одном списке сравниваются без учёта регистра и в отчёт попадают один раз. Порядок строк — порядок появления в cfg или в cuix.

## Формат пакета

Файл `.package` — XML, не архив.

```xml
<ApplicationPackage Name="Приложение">
  <Components>
    <ConfigEntry FileName="/config/app.cfg" FileType="CFG"/>
    <ComponentEntry AppName="Модуль приложения"
                    ModuleName="module.nrx"
                    ModuleType="NRX"/>
    <ComponentEntry AppName="Сборка .NET"
                    ModuleName="module.dll"
                    ModuleType="MGD"/>
    <ComponentEntry AppName="Скрипты"
                    ModuleName="commands.nsf"
                    ModuleType="NSF"/>
    <ComponentEntry AppName="LISP"
                    ModuleName="commands.lsp"
                    ModuleType="LISP"/>
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

В cfg значение начинается с буквы типа: `s` — строка, `i` — число, `f` — флаг. Символ `|` означает перевод строки, поэтому секция и её поля могут стоять в одной физической строке. Пример кнопки тулбара:

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

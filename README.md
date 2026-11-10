# SevenPlasma

**Вигляд і програми Windows 7 для KDE Plasma 6** · *The Windows 7 experience for KDE Plasma 6* ([English below](#english))

> Тестова версія 0.1. Повідомлення про помилки — у [Issues](https://github.com/Vigurov/SevenPlasma/issues).
> *Early test release 0.1 — bug reports welcome in Issues.*

SevenPlasma перетворює KDE Plasma на Windows 7: не лише оформлення, а й самі програми — Панель керування з
десятками розділів, Провідник, Paint, Windows Media Player, Калькулятор, Ножиці, гаджети, Диспетчер завдань,
Керування комп'ютером, екрани завантаження, входу та завершення роботи, ігри Windows 7.

Проєкт побудовано на чудовому [AeroThemePlasma](https://gitgud.io/wackyideas/aerothemeplasma) від
**wackyideas** та учасників AeroShell: оболонка, панель задач, меню «Пуск», рамки вікон (SMOD), ефекти Aero,
екран входу й UAC — їхня робота, до якої SevenPlasma додає свої зміни та програми.

## Що входить

| | |
|---|---|
| **Оболонка** | AeroThemePlasma зі змінами: меню «Пуск», панель задач, трей, підказки, робочий стіл, екрани входу й блокування (зокрема облікові записи без пароля, як у Win7) |
| **Провідник** | Dolphin і KIO зі змінами: панель команд, бібліотеки, адресний рядок, діалоги «Відкрити / Зберегти», «Властивості», «Відкрити за допомогою», мініатюри папок |
| **Панель керування** | звук, мікшер, екран, персоналізація, мережа, брандмауер, Windows Update (apt), центр підтримки, облікові записи, архівація, шрифти, індексування, спеціальні можливості, швидкодія та індекс продуктивності, пристрої й принтери… |
| **Адміністрування** | Керування комп'ютером, Перегляд подій, Керування дисками, Брандмауер у режимі підвищеної безпеки, Диспетчер пристроїв, Диспетчер завдань, Монітор ресурсів |
| **Стандартні програми** | Калькулятор, Блокнот, Paint, Ножиці, Таблиця символів, Засіб перегляду фотографій, Windows Media Player 12, гаджети, «Виконати» |
| **Відеокарти** | панель NVIDIA, AMD Catalyst Control Center, Intel HD Graphics — залежно від відеокарти |
| **Завантаження** | «Запуск Windows», «Ласкаво просимо», «Завершення роботи…» |
| **Ігри** | Сапер, Косинка, Павук, Шахи, Маджонг, Purble Place та інші — з вашого образу Windows 7 через Wine |

## Чому потрібен образ Windows 7

У SevenPlasma **немає жодного файлу Microsoft**: лише код. Іконки, звуки, шрифти Segoe UI, шпалери, анімація
завантаження, картинки програм та ігри витягуються **на вашому комп'ютері з вашої власної копії Windows 7**
(`install.wim` з ISO) за рецептами з `extract/recipes.json`. Без образу тема теж встановиться — з іконками й
звуками AeroThemePlasma та вільним шрифтом Selawik, але без ігор.

## Встановлення

Перевірено на **Kubuntu 26.04** (Plasma 6.6, KDE Frameworks 6.24, Dolphin 25.12, Kvantum 1.1.5), сесія **X11**.

```bash
git clone https://github.com/Vigurov/SevenPlasma.git
cd SevenPlasma
./install.sh --iso ~/Завантаження/Windows7.iso
```

Скрипт спитає пароль адміністратора, встановить пакети, збере AeroThemePlasma, змінені Dolphin / KIO / Kvantum
і програми та витягне ресурси з образу (10–40 хвилин). Потім перезавантажте комп'ютер: на екрані входу вже
вибрано сеанс «AeroThemePlasma (X11)», а під час першого входу тема застосується сама — без майстрів і запитань.

Параметри: `--no-games` (без ігор), `--no-forks` (без змінених Dolphin / KIO / Kvantum), `--yes` (без запитань).
Якщо версії Dolphin / KIO / Kvantum у системі інші, ніж перевірені, ці частини пропускаються з попередженням.

Видалення: `./uninstall.sh`.

## Будова репозиторію

- `apps/` — наші програми (Qt 6 / KDE Frameworks 6) і віджети Plasma
- `patches/` — наші зміни до AeroThemePlasma, Dolphin, KIO, Kvantum, libaero-qt; версії, на які вони
  накладаються, — у `patches/BASES.txt`
- `extract/` — витягування ресурсів з образу Windows 7 (`seven-extract.py`, `recipes.json`), тем Kvantum
  (`kvbuild.py`) і заставки (`plymouth-build.py`)
- `kvantum/` — теми Kvantum без картинок (їх вставляє `kvbuild.py`)
- `boot/`, `data/` — заставка, ярлики, служби, налаштування
- `tools/` — інструменти розробника (пошук джерел ресурсів у Windows, генерація латок)

## Відомі обмеження

- Лише X11 і Plasma 6.6; інші версії Plasma поки не підтримуються.
- Кілька десятків дрібних елементів інтерфейсу ще не мають рецепта витягування (див. `extract/TODO-assets.txt`).
- Інтерфейс програм — українською.

## Подяки та ліцензія

[AeroThemePlasma](https://gitgud.io/wackyideas/aerothemeplasma) і репозиторії [AeroShell](https://gitgud.io/aeroshell)
(wackyideas, catpswin56 та інші), [dolphin-aero і libaero-qt](https://gitgud.io/atmk), KDE, [Kvantum](https://github.com/tsujan/Kvantum).

SevenPlasma поширюється за ліцензією **GNU AGPL-3.0** (див. `LICENSE`), як і AeroThemePlasma.
Microsoft, Windows та Windows 7 — товарні знаки Microsoft Corporation; SevenPlasma не пов'язаний з Microsoft.
Усі ресурси Windows належать Microsoft і використовуються лише з вашої власної копії Windows.

---

## English

SevenPlasma turns KDE Plasma 6 into Windows 7 — not only the look but the programs as well: a Control Panel with
dozens of pages, Explorer, Paint, Windows Media Player, Calculator, Snipping Tool, gadgets, Task Manager,
Computer Management, the boot / logon / shutdown screens and the Windows 7 games. It is built on
[AeroThemePlasma](https://gitgud.io/wackyideas/aerothemeplasma) by wackyideas and the AeroShell contributors.

**No Microsoft files are included.** Icons, sounds, the Segoe UI fonts, wallpapers, the boot animation, program
pictures and the games are extracted on your machine from **your own Windows 7 image** (`install.wim`), following
`extract/recipes.json`. Without an image the theme still installs with AeroThemePlasma's icons and sounds and
the free Selawik font.

Tested on a clean Kubuntu 26.04 (Plasma 6.6, X11). After the install, reboot: the logon screen preselects the
"AeroThemePlasma (X11)" session and the theme applies itself at the first login:

```bash
git clone https://github.com/Vigurov/SevenPlasma.git && cd SevenPlasma
./install.sh --iso ~/Downloads/Windows7.iso
```

The programs' user interface is in Ukrainian for now. Licence: GNU AGPL-3.0. Windows is a trademark of Microsoft
Corporation; this project is not affiliated with Microsoft.

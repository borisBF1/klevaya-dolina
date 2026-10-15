# Update 3 — hot-fix of the interface (smooth scrolling, swipes, guide)

Project root (ASCII junction): `C:\UEProjects\FishingValley`. Read `Docs/UPDATE_HOME.md` §0 for the general rules
(file ownership, compile with Tools\CompileFile.bat, unique namespaces, both orientations, Russian text, no OS input).

## The user's requests (verbatim, Russian)
1. Взаимодействие по простому нажатию: когда предмет выделен, обычное нажатие по экрану считается взаимодействием с
   этим (выделенным) предметом — не нужно попадать точно по предмету, лавке или двери.
2. Все меню, которые можно листать, — плавные (сейчас шаг = одна плитка, иногда даже непонятно, что меню листается).
3. Кнопки над интерфейсом (вкладки, например достижения «Все / В процессе / Выполнены») переключаются свайпом влево/вправо.
   Рюкзак и Садок объединены в одну кнопку «Инвентарь»: открывает рюкзак, если последнее полученное ушло в рюкзак, и
   садок, если в садок; между ними тоже свайп. Внизу 5 кнопок: Карта, Инвентарь, Гардероб, Коллекция, Достижения —
   просвет как сейчас, сами кнопки больше (и в высоту).
4. Убрать подсказки снизу экрана. Вместо них рядом с настройками — мини-учебник с текстом и картинками о пользе
   предметов и функций; без информации о недоступном по уровню, раскрывается постепенно.
5. Коллекция рыб: блок кнопок локаций и видов рыб — внизу, подробное описание и полный вид рыбы — сверху (телефон).
6. Лавка: вкладки переключаются свайпом.
7. Все свайпы анимированы: одна страница уезжает, другая появляется; красиво; туда-обратно без ожидания конца анимации;
   при нажатии на кнопку вкладки анимация тоже проигрывается.

## The new UI foundation (already implemented — use it, do not re-invent)
* `Private/FVUIKit.h` (+ .cpp): `FFVScrollView` (kinetic vertical scrolling: drag 1:1, fling, rubber band, smooth
  wheel, `ScrollToShow`, scrollbar + edge fades) and `FFVPager` (swipeable sliding pages with interruptible animation,
  `GoTo` for tab buttons, `DrawPages` translates + clips each page), `FVKit::TabStrip` (standard tab row with an
  animated indicator), `FVKit::SwipeHint`. Read the header comment — it is the usage guide.
* `Public/FVDraw.h` "update 3": `PushClip/PopClip`, `PushTranslate/PopTranslate`, gesture state
  (`IsPointerDown`, `PressPos`, `DragDelta`, `IsDragGesture`, `DragAxis`, `PointerVelocity`, `ClaimDrag`),
  `FrameDt()`. **`Clicked()` now fires on RELEASE** (press and release over the widget, no drag). `PressedIn()` =
  the old press semantics (sliders, instant mini-game input). `Mouse()` returns local coords (translation removed).
* Inside a pager page: draw in local coords as if the page were at rest, use `D.Mouse()` (never
  `D.Input().Mouse`), keep per-page state (selection, scroll view per tab).
* FVTouch will no longer turn finger drags into `In.Wheel` steps: scroll views read the drag themselves. Remove any
  code that relied on Wheel for touch scrolling, on row-step scrolling (`WheelRows`, `First` row indices) or on tap
  trackers (drags never click now). Mouse wheel still arrives as `In.Wheel` (FFVScrollView handles it).
* Style: lists scroll inside `D.PushClip(Area)`; call `Scroll.DrawBar` (and `DrawFades` over parchment) so it is
  obvious that a list scrolls. Tab rows highlight `Pager.Page`; ←/→ keys call `Pager.GoTo(Pager.Page ± 1)`.
* Everything must work in landscape (640x360, mouse) and portrait (360 x 600..900, touch + mouse).

## The guide («Учебник», EFVScreen::Tutorial, FVScreenTutorial.cpp)
Entries = title + icon + blocks (paragraphs, pictures: item icons / fish / station sprites / UI icons, bullet lists,
small "tip" boxes). Entry unlock rules use the profile (fishing level, unlocked locations, built stations, owned
items...). Items/stations/recipes inside an entry are listed only if available at the player's level (MinLevel).
Unseen unlocked entries are marked «Новое» and counted by `FVTutorialUnseenCount` (HUD badge). Opening an entry adds
its id to `UFVSaveGame::SeenTutorial` (+ `NotifyProfileChanged`).

## Ownership (update 3)
| Agent | Files |
|---|---|
| Core | FVGame.cpp, FVGameHome.cpp, FVTouch.cpp/.h, FVSceneHome.cpp (+ FVScene.cpp if needed), FVFramework.cpp, FVAutoTest.cpp/.h |
| Trade | FVScreenInventory.cpp, FVScreenShop.cpp, FVScreenStorage.cpp, FVScreenCommon.h (additive) |
| Collection | FVScreenCollection.cpp, FVScreenWardrobe.cpp |
| Home | FVScreenStation.cpp, FVScreenOrders.cpp, FVScreenNet.cpp, FVScreenDecor.cpp, FVScreenHome.h |
| Guide | NEW FVScreenTutorial.cpp, FVScreenMenus.cpp, FVScreenMap.cpp, FVMiniGames.cpp (+ FVMiniGames.h) |
FVDraw.*, FVUIKit.*, Public/*.h: integrator only (list needed changes under "Requests").

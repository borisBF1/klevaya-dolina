# Update 4 — «Профиль рыбака и таблица лидеров» (online) + fixes

Project root (ASCII junction): `C:\UEProjects\FishingValley`. General rules: `Docs/UPDATE_HOME.md` §0 (ownership,
CompileFile.bat, unique namespaces, both orientations, Russian text, no OS input). UI kit: `Private/FVUIKit.h`
(smooth scroll `FFVScrollView`, swipe tabs `FFVPager`, `FVKit::TabStrip`), `Public/FVDraw.h` (release-clicks,
`PushClip`, gestures). Client contract: `Public/FVOnline.h`, `Private/FVTextInput.h`, save fields in `Public/FVSaveGame.h`
("update 4"), screens in `Public/FVTypes.h` / `Public/FVScreens.h`.

## 1. What the user asked for
* A fisherman profile: a name (changeable any time), level, favourite location, best catch and similar stats; the
  character's look, a status/motto, a showcase of 3 trophies, the start date and the time in game.
* Names are free (duplicates allowed); under the name a small tag «#1042» tells players apart. Search finds players
  by name OR by number in one search field.
* A leaderboard with all players: categories all-time + a 3-day tournament. When a tournament ends: top 25% of its
  participants get a good reward (scaled by level), top 50% a normal one, top 75% a symbolic one.
* Server on the user's shared virtual hosting (PHP + MySQL) under the owner's site — a NEW folder only, never touch
  the existing site files. The server stores every player's name, secret key and save backups.
* Restore a save by a one-time code (admin makes it in the admin panel; the player enters it in Settings).
* A small admin panel on the site: block / unblock a player, give coins / XP / level, send messages, download saves,
  create restore codes, reset a name, see suspicious players.
* Basic anti-cheat: signed requests, plausibility checks, rate limits, manual bans.
* Fixes: tapping the level badge shows what this level unlocked (scroll down = previous levels); a second swipe during
  the slide animation must work (FIXED in FVUIKit by the integrator); the collection's fish-description animation is
  removed (details appear instantly); the lure workshop becomes a regular workshop tab, highlighted with its own colour.

## 2. Server layout (on hosting: the folder `Fishing-game/` the user created in the site root → https://example.com/Fishing-game/ — the name is case-sensitive)
```
Server/Fishing-game/       (in the repo; uploaded as-is into the hosting folder Fishing-game/)
  api.php                  JSON API (all game requests)
  install.php              creates tables (guarded by config install_key; delete after use)
  config.sample.php        copy to config.php and fill in (DB credentials, admin password, install key)
  .htaccess                deny lib/, config.php, *.sqlite, logs; no directory listing; UTF-8
  lib/db.php lib/auth.php lib/validate.php lib/season.php lib/fish.php (id => name, max size cm, location, legendary)
  admin/index.php (+ admin/style.css)   the admin panel (session login, CSRF)
```
PHP 8.x, PDO. Drivers: MySQL (production, utf8mb4) and SQLite (local tests) — `config.php` 'db_dsn'. All SQL must work
on both (DDL per driver in install.php). Prepared statements only. Responses: `Content-Type: application/json;
charset=utf-8`, HTTP 200, body `{"ok":true,...}` or `{"ok":false,"error":"<code>","message":"<Russian text>"}`.

## 3. API (POST `api.php?a=<action>`, JSON body, UTF-8)
Signed actions carry `"pid"` (int), `"ts"` (unix seconds, client uses server_time offset), `"nonce"` (8 hex) and the
header `X-FV-Sig: <hex HMAC-SHA1(secret, raw body bytes)>`. The server verifies with the stored secret, rejects
|ts - now| > 3600 (`bad_time`), wrong signature (`bad_sig`), banned players (`banned`, message contains the reason).
Rate limit: max 60 signed requests / 10 min per player, 10 registrations / hour and 20 restore attempts / hour per IP
(`rate_limited`).

| action | signed | request | response |
|---|---|---|---|
| ping | no | {} | server_time, season {id, starts, ends} |
| register | no | secret (64 hex), name, platform ("win"/"android"), ver | pid, tag, name, server_time |
| sync | yes | stats{}, profile{}, season_best {fish, size, q} or null | server_time, name, tag, banned=false, mail_unread, season {id, ends, participants, my {rank_fish, rank_earned, rank_xp, rank_biggest, best_pct, tier}} |
| save_upload | yes | save_b64 (≤ 700 KB decoded), save_version | stored=true |
| set_name | yes | name | name (normalised) |
| set_status | yes | status | status |
| board | no (pid optional to mark «me») | cat, period ("all"/"season"), q (search), offset, limit (≤ 50), pid | total, entries[{rank, pid, name, tag, level, value, fish, size, q}], me {…} or null, season {id, ends} |
| profile | no | id | profile {pid, name, tag, status, level, xp, fish_total, species, legendary, achievements, coins_earned, perfect, playtime, stations_built, favorite_location, best_catch{fish,size,q}, showcase[{fish,size,q}], appearance{hat,outfit,bobber,pet}, start_date (unix), created (unix), last_seen (unix), ranks{level,fish,biggest,legend,species,earned,achievements}} |
| mail | yes | {} | mail[{id, kind ("admin"/"season"/"system"), title, body, coins, xp, set_level, items[{id,n}], created, claimed}] (newest 50) |
| mail_claim | yes | id | claimed=true (a second claim → `already_claimed`) |
| restore | no | code | save_b64, pid, name (the code is one-time; expired/used → `bad_code`) |

`stats` = {level (1..10), xp, fish_total, legendary, species (≤ 40), coins_earned, achievements, perfect, playtime (s),
stations_built}. `profile` = {status, favorite_location (location id), best_catch{fish,size,q}, showcase[≤3],
appearance{hat,outfit,bobber,pet}, start_date}. Names: 1..16 chars after trim (letters of any alphabet, digits, space,
`_`, `-`), collapse spaces, profanity filter (Russian + English word list, whole-word and obvious masks) →
`bad_name`. Status: ≤ 40 chars, same filter (`bad_status`). Tag = 1000 + pid (unique, shown as «#1042»).
Search `q`: digits (optionally with «#») → tag match; otherwise name LIKE %q% (case-insensitive).

Board categories: all-time `level` (xp), `fish` (fish_total), `biggest` (best size cm, with fish id), `legend`
(legendary), `species`, `earned` (coins_earned), `achievements`; season `s_fish`, `s_earned`, `s_xp`, `s_biggest`.
Banned players never appear. Ties → earlier achiever (lower `updated_at`) first.

Plausibility (basic anti-cheat): level 1..10 and consistent with xp (FVDB::XpForLevel table), counters never
decrease (except after a restore, which resets the baseline), fish_total growth ≤ 40 per minute since the previous sync
(+ 40), coins_earned growth ≤ 60 000 per minute (+ 100 000), species ≤ 40, achievements ≤ 51, sizes ≤ the species max
(lib/fish.php) + 0.5 cm. A violating field is not applied, the player's `suspicious` counter grows and a note is stored
(admin sees it).

## 4. Seasons (3-day tournaments)
Season id = floor((now − EPOCH) / 259200), EPOCH = 2026-09-28 00:00 Europe/Moscow. A player participates once he syncs
during the season with season fish > 0. Baselines (fish_total, coins_earned, xp) are taken at the player's first sync
of the season; season values = current − baseline; `s_biggest` = max `season_best.size` sent during the season.
Closing is lazy: any request after the end closes every unclosed past season inside a transaction guarded by an atomic
flag. **Rewards are given PER CATEGORY (user's decision, update 4b)**: in each of the 4 season categories the participants
with value > 0 are ranked (N = their count); percentile = rank / N → tier ≤ 25 % gold (1), ≤ 50 % silver (2),
≤ 75 % bronze (3), else none. A player can win up to 4 rewards per tournament; all of them arrive in ONE mail (kind
"season", title «Итоги турнира №S», body = a line per category: place / participants / tier / reward). L = the player's
level, Span = XP needed from the start of level L to L+1 (FVDB XpForLevel table; at level 10 Span = 3100).
| category | gold | silver | bronze |
|---|---|---|---|
| s_fish (рыба) → хорошие приманки | bait_magic × (3 + ⌈L/2⌉), bait_lure × 10 | bait_lure × (4 + ⌈L/2⌉), bait_nightcrawler × 10 | bait_grub × 10 |
| s_earned (монеты) → предметы, количество от уровня и места | 3 kinds × (4 + L) | 2 kinds × (2 + ⌈L/2⌉) | 1 kind × (1 + ⌈L/4⌉) |
| s_xp (опыт) → монеты от уровня | 300 + 200·L coins | 150 + 100·L coins | 50 + 30·L coins |
| s_biggest (самая крупная) → опыт от уровня | 30 % of Span XP (min 150) | 15 % of Span XP (min 100) | 6 % of Span XP (min 50) |
Item kinds for s_earned, chosen deterministically (seed = season, player) from the pool of the player's level band:
L1-3 mat_board, mat_rope, mat_glue, res_resin, res_feather; L4-6 mat_glitter, mat_sinker, mat_glue, mat_rope, float_feather;
L7-10 mat_glitter, float_sliding, line_braided, mat_glue, net_small (nets/tackle count capped at 2). Results are stored in
`seasons.results_json`. The client shows the same table for the player's level (UFVOnline::SeasonRewardText).

## 5. Database (install.php creates; MySQL types shown)
players(id PK AI, secret CHAR(64), name VARCHAR(32), status VARCHAR(64), platform, ver, created INT, last_seen INT,
updated_at INT, banned TINYINT, ban_reason VARCHAR(200), suspicious INT, notes TEXT, level INT, xp INT, fish_total INT,
legendary INT, species INT, coins_earned BIGINT, achievements INT, perfect INT, playtime INT, stations_built INT,
best_size FLOAT, best_fish VARCHAR(40), best_q TINYINT, profile_json TEXT, last_sync INT) + indexes on the board columns.
saves(id PK AI, player_id, created INT, size INT, version INT, data MEDIUMBLOB) — keep the newest 12 per player.
restore_codes(code CHAR(8) PK, player_id, save_id, created INT, expires INT, used INT NULL, used_ip).
mail(id PK AI, player_id, kind, title, body TEXT, coins INT, xp INT, set_level INT, items_json TEXT, created INT, claimed INT NULL).
seasons(id PK, starts INT, ends INT, closing TINYINT, closed INT NULL, results_json MEDIUMTEXT).
season_stats(season_id, player_id, base_fish, base_earned, base_xp, fish, earned, xp, best_size, best_fish, best_q,
PRIMARY KEY(season_id, player_id)).
rate_log(k VARCHAR(80), ts INT) (pruned), admin_log(id, ts, action, player_id, details).

## 6. Admin panel (admin/index.php)
Login with the password from config.php (`admin_password`; compare with hash_equals; if it looks like a password_hash,
use password_verify), session + CSRF tokens on every form, login rate limit, `X-Robots-Tag: noindex`. Pages:
dashboard (players, active today/week, current season countdown + leaders, suspicious list); players (search by name /
#tag / pid, sortable); player page (profile & stats, notes, block/unblock + reason, give coins / XP / set level → a mail
with attachments, send a message → mail, reset name, saves list with «скачать .sav» and «создать код восстановления»
(8 chars, no ambiguous letters, valid 7 days), mail history); seasons (past results, participants, tiers). Russian UI,
simple clean CSS, works on a phone browser. Every admin action is logged.

## 7. Client (see Public/FVOnline.h)
* UFVOnline (owned by UFVGame, ticked every frame). Server URL: `Config/DefaultGame.ini` [FishingValley.Online]
  ServerUrl=https://example.com/Fishing-game/api.php, overridden by `-fvserver=<url>`; the autotest is offline unless
  `-fvserver` is given. UE HTTP module (FHttpModule), JSON (FJsonSerializer), HMAC-SHA1 (FSHA1::HMACBuffer).
* Identity: `OnlineSecret` (64 hex from FGuid/FPlatformMath random) created on registration, stored in the save
  together with OnlineId/OnlineTag/PlayerName. First run: the Profile screen opens in "setup" mode to choose a name
  (Core triggers it when PlayerName is empty); registration happens as soon as there is a name and the server answers.
* Sync: on start, every 5 min of play, after a level-up / legendary / achievement, after sleeping, when the app pauses
  or quits (best effort). Save backup: every 15 min of play and after sleeping if the save changed. Offline = silent
  retries with back-off; the game never blocks on the network.
* Mail: fetched on sync; claiming applies coins (Earn(..., false)), XP (AddXp), set_level (raise XP to the level),
  items (GiveItem) exactly once (`ClaimedMail` ids in the save).
* Restore: RestoreByCode downloads the save, writes it to the slot file, reloads the profile and re-enters the world.
* Banned: status Banned, the profile/leaderboard show the reason; the game itself stays playable offline.

## 8. UI
* **Profile** (EFVScreen::Profile; context None = own, "setup" = first-run name choice, "<pid>" = another player from
  the server): character look (UFVSprites::Character from the cosmetic ids + pet), big name + small «#1042» under it,
  status, level + XP, favourite location, best catch (sprite, size, quality stars), showcase of 3 trophies, stats
  (fish, species x/40, legendaries, achievements, perfect catches, coins earned, stations), «Рыбачит с дд.мм.гггг»,
  time in game, ranks. Own profile: edit name / status (FVKit::TextField), buttons «Рейтинг», «Почта» (badge), player
  number. Smooth scrolling, both orientations.
* **Leaderboard** (EFVScreen::Leaderboard): period switch «За всё время / Турнир (3 дня)», category pages (FFVPager with
  swipe + TabStrip), smooth list, medals for 1-3, name + small #tag, level, formatted value, own row highlighted and
  pinned at the bottom when not visible, search field (name or number), tap a row → that player's Profile. Tournament
  page: countdown, reward tiers explained for the player's level, the player's current places / projected tier.
  Offline / error states are friendly.
* **Mail** (EFVScreen::Mail): letters (admin messages, tournament results) with attachments and «Забрать».
* **Levels** (EFVScreen::Levels, from the HUD level badge): the current level's unlocks first
  (FVDB::UnlocksAtLevel + HomeUnlocksAtLevel), scrolling down shows previous levels; a small banner with the next
  level's XP left.
* **RestoreSave** (EFVScreen::RestoreSave, from Settings): explanation, code field, confirm dialog (replaces the
  current progress), progress & result.
* HUD: tapping the level badge → Levels; a Profile button (person icon) in the HUD (landscape icon row + portrait top
  strip) with the unread-mail badge; Settings get «Восстановить сохранение» and the player number / online status.

## 9. Ownership (update 4)
| Agent | Files |
|---|---|
| Server | NEW Server/Fishing-game/** (PHP), NEW Docs/SERVER.md (deployment guide for the user), local tests with PHP |
| Online | NEW Private/FVOnline.cpp (+ private helpers), Config/DefaultGame.ini (the [FishingValley.Online] section) |
| Core | FVGame.cpp, FVGameHome.cpp, FVFramework.cpp, FVTouch.cpp/.h, NEW Private/FVTextInput.cpp, FVScreenMenus.cpp, FVSaveGame.cpp (fixups of the update-4 fields), FVAutoTest.cpp/.h |
| Social UI | NEW FVScreenProfile.cpp, FVScreenLeaderboard.cpp, FVScreenMail.cpp, FVScreenRestore.cpp |
| Fixes | FVScreenCollection.cpp, FVScreenStation.cpp, FVMiniGames.cpp/.h, NEW FVScreenLevels.cpp |
Public/*.h, FVDraw.*, FVUIKit.*, Private/FVTextInput.h, Build.cs: integrator only (write needed changes under Requests).

## 10. Update 4b — versions, download page, start-up gate, offline mode (user's decisions)
* The server keeps `latest_build`, `latest_name` ("0.4"), `min_build`, `download_url`, `changelog` (admin panel → «Версия
  игры»; stored in a `settings(k PRIMARY KEY, v TEXT)` table). `ping` returns them as `version{latest_build, latest_name,
  min_build, download_url, changelog}`. Only an Android APK is distributed: the admin uploads it into
  `Fishing-game/download/` via the hosting file manager and types its URL (default
  https://example.com/Fishing-game/download/KlevayaDolina.apk).
* Download page `Fishing-game/index.php`: a small friendly Russian landing page (game title, current version, changelog,
  big «Скачать для Android» button, how to install an APK). Works on phones.
* Client build number: `UFVOnline::ClientBuild()` (a constant in FVOnline.cpp, now 4) + `ClientVersionName()` ("0.4");
  keep Android StoreVersion/VersionDisplayName in DefaultEngine.ini in step (integrator).
* Start-up gate (main menu): `StartupCheck()` → state Checking («Проверка обновлений…» / «Синхронизация…») → ping +
  version + (if registered) sync + mail. Then:
  - build < min_build → UpdateRequired: play is blocked; «Скачать новую версию» (OpenDownloadPage) + changelog;
  - build < latest_build → UpdateAvailable: «Обновить» / «Позже» (SkipOptionalUpdate → Ready);
  - server unreachable → NoConnection: «Повторить» and «Играть офлайн» (EnterOfflineMode) — offline mode is offered ONLY
    without a connection; in offline mode online features (board, other profiles, mail, restore, backups) are unavailable
    and show «Офлайн-режим» + «Подключиться» (RetryOnline → StartupCheck; the gate applies again);
  - banned → Banned: the reason + «Играть офлайн»;
  - otherwise Ready. «Продолжить» / «Новая игра» are enabled only in Ready, Disabled (no URL / autotest) or offline mode.

## 11. Update 4b — first steps at home: practice pond + onboarding (user's request)
* New players start at the Home yard (not the pond). Existing saves keep their location; old saves get OnboardingStep=100.
* The yard gets a small PRACTICE POND (both orientations; pretty, clearly a small pond with a little wooden plank):
  standing at its edge the player can cast (same controls). There are no real fish: a «учебная рыбка» bites after
  2–4 s, the minigame runs with an easy difficulty (≈15, Mixed) and a wider bar; caught → a friendly card «Отлично!
  Теперь вы умеете рыбачить» (no fish, XP, bait use, records, keepnet); escaped → «Не беда, попробуйте ещё раз».
* Onboarding (UFVSaveGame::OnboardingStep, private FVOnboarding API below) shows big friendly coach marks (speech bubble
  + bouncing arrow, both orientations, touch & keyboard wording) — only while OnboardingStep < 100, never in the autotest
  unless it forces it: 1 «Подойдите к учебному пруду» (arrow to the pond) → 2 «Зажмите и отпустите, чтобы забросить» →
  3 «Ждите поклёвку. Когда появится «!» — нажмите!» → 4 «Держите рыбку в зелёной полосе: зажали — полоса вверх,
  отпустили — вниз» → 5 success card → 6 «Настоящая рыба ждёт на Деревенском пруду: откройте Карту» (arrow to the Map
  button) → done when the player travels to the pond. A small «Пропустить обучение» button is always available.
  API (Private/FVOnboarding.h, implemented by the World+Fishing agent, called by Core from UFVGame::Tick/Draw):
  `namespace FVOnboarding { void Tick(UFVGame* G, float Dt); void Draw(UFVGame* G, FVDraw& D); bool IsActive(UFVGame* G);
  void Skip(UFVGame* G); }` — Draw runs after the world HUD, before screens.
* Tournament numbering: EPOCH = 2026-09-01 00:00 MSK (so ids are positive now); the UI shows «Турнир №<id>». Each
  season category has its own tier in `sync.season.my`: `tier_fish, tier_earned, tier_xp, tier_biggest` (0..3) plus the
  places `rank_*`; rewards per §4 table.

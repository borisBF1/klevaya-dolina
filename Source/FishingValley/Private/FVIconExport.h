// Art export mode for the web site and the admin panel (Server/Fishing-game/img): command line -fvexportart=<dir>.
// After start-up it writes every item icon the game draws (fish incl. expert fish and legends, junk, baits and lures,
// rods, items, decor, tools, cosmetics, a few UI icons and character sprites) as PNG scaled x4 with nearest-neighbour
// pixels into <dir>/icons and <dir>/sprites, plus <dir>/manifest.json (id, name, kind, size, location...).
// With -fvexportgif it then fishes once at the village pond (the fishing bot, a forced bite) and requests a UI
// screenshot every ~0.1 s (Saved/<-fvshotdir>/gif_NNNN.png, the frame states in <dir>/gif_frames.txt), then quits.
// The mode uses the autotest save slot and never talks to the server (-fvserver=off is appended to the command line).
// Started by one line in AFVPlayerController::BeginPlay (FVFramework.cpp); run it with Tools/ExportArt.ps1.

#pragma once

/** Checks the command line; in export mode switches to the autotest save slot and registers the export ticker. */
void FVIconExportMaybeStart();

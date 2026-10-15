<?php
// Fishing Valley server: schema upgrades that run by themselves on the first request after new server files are
// uploaded (install.php runs them too), so the server works before anyone re-opens install.php.
// Version 2 = update 6 (Docs/UPDATE6_FOREST_CITY.md §9, §14): players.xp_earned («опыт заработанный», the tournament
// «Опыт»), xp_earned_seen (the client's counter as reported last, -1 = never), xp_seniority («стаж»), xp_migrated (the
// one-off XP trim is used up). The version is kept in settings.schema_version (one cached query per request).
if (!defined('FV_ROOT')) { http_response_code(403); exit; }

require_once __DIR__ . '/settings.php';

const FV_SCHEMA_VERSION = 2;
/** New players columns: name => [MySQL type, SQLite type]. */
const FV_SCHEMA_U6_COLUMNS = [
	'xp_earned' => ['BIGINT NOT NULL DEFAULT 0', 'INTEGER NOT NULL DEFAULT 0'],
	'xp_earned_seen' => ['BIGINT NOT NULL DEFAULT -1', 'INTEGER NOT NULL DEFAULT -1'],
	'xp_seniority' => ['INT NOT NULL DEFAULT 0', 'INTEGER NOT NULL DEFAULT 0'],
	'xp_migrated' => ['TINYINT NOT NULL DEFAULT 0', 'INTEGER NOT NULL DEFAULT 0'],
];

/** true when the column (or, for '*', the table) exists. */
function fv_column_exists(string $table, string $col): bool
{
	try {
		fv_db()->query('SELECT ' . $col . ' FROM ' . $table . ' LIMIT 0');
		return true;
	} catch (Throwable $e) {
		return false;
	}
}

/**
 * Brings the tables to FV_SCHEMA_VERSION. Returns the report lines (empty when nothing was done). Does nothing before
 * install.php created the tables. A new xp_earned starts equal to xp, so running tournaments keep their baselines.
 */
function fv_schema_upgrade(): array
{
	static $done = false;
	if ($done) { return []; }
	$done = true;
	if ((int)fv_setting('schema_version') >= FV_SCHEMA_VERSION) { return []; }
	if (!fv_column_exists('players', 'id')) { return []; }
	$report = [];
	$mysql = fv_driver() === 'mysql';
	foreach (FV_SCHEMA_U6_COLUMNS as $col => [$my, $lite]) {
		if (fv_column_exists('players', $col)) { continue; }
		try {
			fv_db()->exec('ALTER TABLE players ADD COLUMN ' . $col . ' ' . ($mysql ? $my : $lite));
		} catch (Throwable $e) {
			if (fv_column_exists('players', $col)) { continue; }   // a parallel request added it
			throw $e;
		}
		if ($col === 'xp_earned') { fv_db()->exec('UPDATE players SET xp_earned = xp'); }
		$report[] = 'players.' . $col;
	}
	try {
		fv_setting_set('schema_version', (string)FV_SCHEMA_VERSION);
	} catch (Throwable $e) {
		// no settings table yet (install.php of 4b not run): the columns are checked again on the next request
	}
	return $report;
}

/** true when the update-6 columns exist (the sync writes them only then; the tournament falls back to xp). */
function fv_schema_u6_ready(): bool
{
	static $ready = null;
	if ($ready === null) {
		$ready = (int)fv_setting('schema_version') >= FV_SCHEMA_VERSION || fv_column_exists('players', 'xp_migrated');
	}
	return $ready;
}

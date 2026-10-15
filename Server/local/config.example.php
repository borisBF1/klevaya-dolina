<?php
// LOCAL development config — TEMPLATE. Copy this file to Server/local/config.php (that file is git-ignored).
// lib/db.php finds Server/local/config.php automatically as the fallback config (or point the FV_CONFIG environment
// variable at any config file). Debug settings below are honoured only for local configs like this one.
// Then: php -S 127.0.0.1:8766 -t Server/Fishing-game  →  http://127.0.0.1:8766/install.php?key=<install_key>
return [
	'db_dsn' => 'sqlite:' . __DIR__ . '/fishing.sqlite',
	'db_user' => '',
	'db_pass' => '',
	'admin_password' => 'CHANGE_ME_local_admin',   // admin panel password for the local server (8+ characters)
	'install_key' => 'CHANGE_ME_install_key',      // key for install.php?key=...
	// shift the server clock (seconds) by writing a number into Server/local/time_offset.txt
	'debug_time_offset' => (int)@file_get_contents(__DIR__ . '/time_offset.txt'),
];

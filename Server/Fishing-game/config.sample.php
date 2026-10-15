<?php
// Fishing Valley server settings — TEMPLATE.
// 1. Copy this file to config.php in the same folder (Fishing-game/config.php).
// 2. Fill in the values below (DB name / user / password from your hosting panel, your admin password, an install key).
// 3. Never publish config.php. The .htaccess file blocks it from the web.
return [
	// MySQL: on shared hosting the host is usually "localhost"; the database and user names often look like u1234567_fishing.
	'db_dsn' => 'mysql:host=localhost;dbname=ИМЯ_БАЗЫ;charset=utf8mb4',
	'db_user' => 'ПОЛЬЗОВАТЕЛЬ_БАЗЫ',
	'db_pass' => 'ПАРОЛЬ_БАЗЫ',

	// Admin panel password (admin/). At least 8 characters. You may also put a password_hash() value here.
	'admin_password' => '',

	// Key for install.php?key=... (any string of 8+ characters). Delete install.php after the installation.
	'install_key' => 'ЗАМЕНИТЕ_НА_СВОЙ_КЛЮЧ',

	// Optional limits: [max requests, window in seconds].
	// 'rate_signed' => [60, 600],      // signed game requests per player
	// 'rate_register' => [10, 3600],   // registrations per IP
	// 'rate_restore' => [20, 3600],    // restore-code attempts per IP
	// 'rate_unsigned' => [1200, 600],  // leaderboard / profile / ping per IP
	// 'rate_admin_login' => [10, 900], // failed admin logins per IP
];

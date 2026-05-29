<?php
if (session_status() === PHP_SESSION_NONE) {
    session_start();
}
require_once __DIR__ . '/lang_init.php';
require_once __DIR__ . '/auth.php';

if (!is_logged_in() || ($_SESSION['role'] !== 'admin' && $_SESSION['role'] !== 'user') ) {
    header("Location: login.php");
    exit;
}

$fwverFile = "/var/lib/k180/firmware_ver_info.json";
$apiverFile = "/var/lib/k180/api_ver_info.json";

if (!file_exists($fwverFile) || !file_exists($apiverFile) ) {
    die("<p class='error'>" . htmlspecialchars($lang['error_config_not_found']) . "</p>");
}

$fp = fopen($fwverFile, "r");
if (!$fp) {
    die($lang['error_open']);
}
if (!flock($fp, LOCK_EX)) {
    fclose($fp);
    die($lang['error_lock']);
}
$FW_version_Data = json_decode(stream_get_contents($fp), true);
if (!isset($FW_version_Data['sysinfo']['fwver'])) {
	die("<p class='error'>" . htmlspecialchars($lang['error_config_format']) . "</p>");
}

$php_version = "1.0";
$firmware_version = (string) $FW_version_Data['sysinfo']['fwver'];
flock($fp, LOCK_UN);
fclose($fp);

$api_fp = fopen($apiverFile, "r");
if (!$api_fp) {
    die($lang['error_open']);
}
if (!flock($api_fp, LOCK_EX)) {
    fclose($api_fp);
    die($lang['error_lock']);
}
$API_version_Data = json_decode(stream_get_contents($api_fp), true);
if (!isset($API_version_Data['apiinfo']['ver'])) {
	die("<p class='error'>" . htmlspecialchars($lang['error_config_format']) . "</p>");
}

$api_version = (string) $API_version_Data['apiinfo']['ver'];
flock($api_fp, LOCK_UN);
fclose($api_fp);
?>

<!DOCTYPE html>
<html lang="<?= htmlspecialchars($lang['lang_code']) ?>">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title><?= $lang['nav_sysinfo'] ?></title>
<style>
    body {
        font-family: Arial, sans-serif;
        background-color: #000000;
        margin: 0;
        padding: 0;
        text-align: center;
    }
    .navbar {
        background-color: #333;
        padding: 1rem 0;
    }
    .navbar a {
        color: white;
        margin: 0 1rem;
        text-decoration: none;
        font-size: 1.2rem;
    }
    .navbar a:hover {
        text-decoration: underline;
    }
    h1 {
        margin-top: 2rem;
        color: #333;
    }
    .container {
        background-color: #333;
        max-width: 400px;
        margin: 2rem auto;
        padding: 2rem;
        border-radius: 10px;
        box-shadow: 0 0 10px rgba(0,0,0,0.1);
        font-size: 1.2rem;
        color: #ffffff;
    }
    .error {
        color: red;
        font-weight: bold;
        margin-top: 2rem;
    }
</style>
</head>
<body>

<div class="navbar">
    <a href="setting.php"><?= $lang['nav_setting'] ?></a> |
    <a href="upload.php"><?= $lang['nav_upload'] ?></a> |
    <a href="recordings.php"><?= $lang['nav_recordings'] ?></a> |
    <a href="sysinfo.php"><?= $lang['nav_sysinfo'] ?></a> |
    <?php if (isset($_SESSION['role']) && $_SESSION['role'] === 'admin'): ?>
        <a href="user_manage.php"><?= $lang['nav_user_manage'] ?></a> |
    <?php endif; ?>
    <a href="logout.php"><?= $lang['nav_logout'] ?></a>
</div>

<div class="container">
    <p><?= $lang['label_system_time'] ?><br><strong><?= date('Y-m-d H:i:s') ?></strong></p>
    <p><?= $lang['label_web_version'] ?><strong>    <?= htmlspecialchars($php_version) ?></strong></p>
    <p><?= $lang['label_api_version'] ?><strong>    <?= htmlspecialchars($api_version) ?></strong></p>
    <p><?= $lang['label_firmware_version'] ?><strong>    <?= htmlspecialchars($firmware_version) ?></strong></p>
</div>

</body>
</html>

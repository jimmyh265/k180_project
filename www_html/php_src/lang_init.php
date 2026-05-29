<?php
if (session_status() === PHP_SESSION_NONE) {
    session_start();
}

// 若 GET 有指定語言，更新 Session 中的語言
if (isset($_GET['lang']) && in_array($_GET['lang'], ['zh', 'en'])) {
    $_SESSION['lang'] = $_GET['lang'];
}

// 預設語言為 zh
$langCode = $_SESSION['lang'] ?? 'zh';
$langFile = __DIR__ . "/../lang/{$langCode}.php";

// 載入語言檔
if (file_exists($langFile)) {
    require_once $langFile;
} else {
    require_once __DIR__ . "/../lang/zh.php"; // fallback
}

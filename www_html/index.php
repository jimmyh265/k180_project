<?php
if (session_status() === PHP_SESSION_NONE) {
    session_start();
}
if (!isset($_SESSION['username'])) {
    header("Location: /php_src/login.php");
    exit;
}
else {
    header("Location: /php_src/setting.php");
    exit;
}

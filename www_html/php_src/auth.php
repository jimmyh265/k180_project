<?php
if (session_status() === PHP_SESSION_NONE) {
    session_start();
}

function is_logged_in(): bool {
    if (!isset($_SESSION['username'])) {
        return false;
    }

    // 若超過 5 分鐘沒操作，自動登出
    if (isset($_SESSION['last_activity']) && time() - $_SESSION['last_activity'] > 300) {
        // 清除 cookie（client）
        if (ini_get("session.use_cookies")) {
            $params = session_get_cookie_params();
            setcookie(session_name(), '', time() - 42000,
                $params["path"], $params["domain"],
                $params["secure"], $params["httponly"]
            );
        }

        // 清除 server 的 session
        session_unset();
        session_destroy();
        return false;
    }

    // 更新活動時間
    $_SESSION['last_activity'] = time();
    return true;
}
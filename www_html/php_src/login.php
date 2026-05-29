<?php
if (session_status() === PHP_SESSION_NONE) {
    session_start();
}

require_once __DIR__ . '/lang_init.php';

$error = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $usersFile = '/var/www/private/users.json';
    $username = trim($_POST['username'] ?? '');
    $password = trim($_POST['password'] ?? '');

    if (file_exists($usersFile)) {
        $users = json_decode(file_get_contents($usersFile), true);
        foreach ($users as $user) {
            if ($user['username'] === $username && password_verify($password, $user['password_hash'])) {
                $_SESSION['username'] = $user['username'];
                $_SESSION['role'] = $user['role'];
                $_SESSION['last_activity'] = time();
                header('Location: setting.php');
                exit;
            }
        }
        $error = $lang['error_invalid'];
    } else {
        $error = $lang['error_not_found'];
    }
}
?>

<!DOCTYPE html>
<html lang="<?= htmlspecialchars($lang['lang_code']) ?>">
<head>
    <meta charset="UTF-8">
    <title><?= htmlspecialchars($lang['title']) ?></title>
    <link rel="stylesheet" href="style.css">
    <style>
        body {
            font-family: "Segoe UI", sans-serif;
            background: linear-gradient(to right, #000000, #000000);
            display: flex;
            align-items: center;
            justify-content: center;
            height: 100vh;
            margin: 0;
        }
        .login-container {
            background-color: #333;
            border-radius: 12px;
            padding: 30px 40px;
            box-shadow: 0 4px 20px rgba(0, 0, 0, 0.1);
            width: 360px;
        }
        .login-container h2 {
            text-align: center;
            margin-bottom: 24px;
            color: #f4f4f4;
        }
        .login-container input[type="text"],
        .login-container input[type="password"] {
            width: 100%;
            padding: 12px;
            margin: 10px 0 16px;
            border: 1px solid #ccc;
            border-radius: 8px;
            box-sizing: border-box;
        }
        .login-container button {
            width: 100%;
            padding: 12px;
            background-color: #5b6cb2;
            border: none;
            margin: 10px 0 16px;
            border-radius: 8px;
            color: white;
            font-size: 16px;
            cursor: pointer;
            box-sizing: border-box;
        }
        .login-container button:hover {
            background-color: #4454a3;
        }
        .error {
            color: red;
            text-align: center;
            margin-bottom: 16px;
        }
        .lang-switch {
            text-align: right;
            margin-top: -20px;
            margin-bottom: 10px;
        }
        .lang-switch a {
            color: #ccc;
            margin-left: 10px;
            text-decoration: none;
        }
        .lang-switch a:hover {
            text-decoration: underline;
        }
    </style>
</head>
<body>
<div class="login-container">
    <div class="lang-switch">
        <a href="?lang=zh">中文</a> | <a href="?lang=en">English</a>
    </div>

    <h2><?= htmlspecialchars($lang['please_login']) ?></h2>

    <form method="post">
        <input type="text" name="username" placeholder="<?= htmlspecialchars($lang['username']) ?>" required>
        <input type="password" name="password" placeholder="<?= htmlspecialchars($lang['password']) ?>" required>
        <button type="submit"><?= htmlspecialchars($lang['login']) ?></button>
    </form>

    <?php if ($error): ?>
        <div class="error"><?= htmlspecialchars($error) ?></div>
    <?php endif; ?>
</div>
</body>
</html>

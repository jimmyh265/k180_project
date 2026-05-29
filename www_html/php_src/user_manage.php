<?php
if (session_status() === PHP_SESSION_NONE) {
    session_start();
}
require_once __DIR__ . '/lang_init.php';
require_once __DIR__ . '/auth.php';

// 檢查是否為已登入的 admin
if (!is_logged_in() || $_SESSION['role'] !== 'admin') {
    header("Location: login.php");
    exit;
}

$usersFile = '/var/www/private/users.json';
$users = file_exists($usersFile) ? json_decode(file_get_contents($usersFile), true) : [];

$message = '';

if (
    empty($_SESSION['csrf_token']) ||
    empty($_SESSION['csrf_token_time']) ||
    time() - $_SESSION['csrf_token_time'] > 600
) {
    $_SESSION['csrf_token'] = bin2hex(random_bytes(32));
    $_SESSION['csrf_token_time'] = time();
}

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
	
    if (
        empty($_POST['csrf_token']) ||
        empty($_SESSION['csrf_token']) ||
        !hash_equals($_SESSION['csrf_token'], $_POST['csrf_token']) ||
        empty($_SESSION['csrf_token_time']) ||
        time() - $_SESSION['csrf_token_time'] > 600
    ) {
        die($lang['error_csrf']);
    }
	
    if (isset($_POST['create'])) {
        $newUser = trim($_POST['username']);
        $newPass = $_POST['password'];

        if (empty($newUser) || empty($newPass)) {
            $message = "<p class='error'>{$lang['error_empty_username_password']}</p>";
        } elseif (array_filter($users, fn($u) => $u['username'] === $newUser)) {
            $message = "<p class='error'>{$lang['error_user_exists']}</p>";
        } else {
            $users[] = [
                'username' => $newUser,
                'password_hash' => password_hash($newPass, PASSWORD_DEFAULT),
                'role' => 'user'
            ];
            file_put_contents($usersFile, json_encode($users, JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE));
            $message = "<p class='success'>{$lang['user_add_success']}</p>";
        }
    }

    if (isset($_POST['delete'])) {
        $deleteUser = $_POST['delete_user'];
        $users = array_filter($users, fn($u) => $u['username'] !== $deleteUser);
        file_put_contents($usersFile, json_encode(array_values($users), JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE));
        $message = "<p class='success'>{$lang['user_delete_success']} ：$deleteUser</p>";
    }
}
?>

<!DOCTYPE html>
<html lang="<?= htmlspecialchars($lang['lang_code']) ?>">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title><?= $lang['nav_user_manage'] ?></title>
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
      color: #ffffff;
    }
    .container {
      background-color: #333;
      max-width: 600px;
      margin: 2rem auto;
      padding: 2rem;
      border-radius: 10px;
      box-shadow: 0 0 10px rgba(0,0,0,0.1);
      text-align: left;
	  color: #ffffff;
    }
    form label {
      display: block;
      margin-top: 1rem;
      font-size: 1.1rem;
    }
    input[type="text"], input[type="password"] {
      width: 60%;
      padding: 0.5rem;
      margin-top: 0.25rem;
      font-size: 1rem;
      border: 1px solid #ccc;
      border-radius: 5px;
    }
    input[type="submit"], button {
      margin-top: 1rem;
      padding: 0.5rem 1.5rem;
      font-size: 1rem;
      background-color: #5b6cb2;
      color: white;
      border: none;
      border-radius: 5px;
      cursor: pointer;
    }
    input[type="submit"]:hover {
      background-color: #4454a3;
    }
    .user-list ul {
      list-style: none;
      padding: 0;
    }
    .user-list li {
      margin: 0.5rem 0;
    }
    .success {
        color: green;
        font-weight: bold;
        margin-top: 1rem;
    }
    .error {
        color: red;
        font-weight: bold;
        margin-top: 1rem;
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

  <h1><?= $lang['nav_user_manage'] ?></h1>

<div class="container">

  <?= $message ?>
  <form method="post">
    <h2><?= $lang['user_add'] ?></h2>
    <input type="hidden" name="csrf_token" value="<?= htmlspecialchars($_SESSION['csrf_token']) ?>">
    <label><?= $lang['username'] ?>： <input type="text" name="username" minlength="3" maxlength="10" pattern="[A-Za-z0-9]{3,10}" title="<?= $lang['username_hint'] ?>" required></label>
    <label><?= $lang['password'] ?>： <input type="password" name="password"
	   minlength="4" maxlength="10" 
       pattern="[A-Za-z0-9!-/:-@\[\\\]_`{-~]{4,10}"
       title="<?= $lang['password_hint'] ?>"
       required></label>
    <input type="submit" name="create" value=<?= $lang['btn_add_user'] ?>>
  </form>
  <br>
  <div class="user-list">
    <h2><?= $lang['user_list'] ?></h2>
    <ul>
      <?php foreach ($users as $u): ?>
        <li>
          <?= htmlspecialchars($u['username']) ?> (<?= htmlspecialchars($u['role']) ?>)
          <?php if ($u['username'] !== $_SESSION['username']): ?>
            <form method="post" style="display:inline">
              <input type="hidden" name="csrf_token" value="<?= htmlspecialchars($_SESSION['csrf_token']) ?>">
              <input type="hidden" name="delete_user" value="<?= htmlspecialchars($u['username']) ?>">
              <input type="submit" name="delete" value="<?= $lang['delete'] ?>" onclick="return confirm('<?= $lang['confirm_delete_user'] ?> <?= htmlspecialchars($u['username']) ?>?');">
            </form>
          <?php endif; ?>
        </li>
      <?php endforeach; ?>
    </ul>
  </div>
</div>
 
</body>
</html>

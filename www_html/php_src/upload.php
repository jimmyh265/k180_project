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

if (
    empty($_SESSION['csrf_token']) ||
    empty($_SESSION['csrf_token_time']) ||
    time() - $_SESSION['csrf_token_time'] > 600
) {
    $_SESSION['csrf_token'] = bin2hex(random_bytes(32));
    $_SESSION['csrf_token_time'] = time();
}

$message = '';

$uploadDir = '/var/lib/k180/tmp_fw';
$allowedExtensions = ['hhc']; // 只允許 .hhc 檔案

if (isset($_POST['upload_button']) && isset($_FILES['upload_file'])) {
	
    if (
        empty($_POST['csrf_token']) ||
        empty($_SESSION['csrf_token']) ||
        !hash_equals($_SESSION['csrf_token'], $_POST['csrf_token']) ||
        empty($_SESSION['csrf_token_time']) ||
        time() - $_SESSION['csrf_token_time'] > 600
    ) {
        die($lang['error_csrf']);
    }
	
	$ttl = 120;
	foreach (glob($uploadDir . '/*.hhc') as $oldhhc) {
		if (is_file($oldhhc) && time() - filemtime($oldhhc) > $ttl) {
			@unlink($oldhhc);
		}
	}
					
    $fileTmpPath = $_FILES['upload_file']['tmp_name'];
    $originalFileName = $_FILES['upload_file']['name'];
    $fileSize = $_FILES['upload_file']['size'];
    $fileError = $_FILES['upload_file']['error'];

	
    if ($fileError === UPLOAD_ERR_OK) {
        $fileExtension = strtolower(pathinfo($originalFileName, PATHINFO_EXTENSION));

        if (in_array($fileExtension, $allowedExtensions)) {
            // 避免使用者上傳同名檔案，產生唯一檔名
            $newFileName = uniqid('fw_', true) . '.' . $fileExtension;
            $destination = $uploadDir . '/' . $newFileName;

            if (move_uploaded_file($fileTmpPath, $destination)) {
				$output = [];
				$return_var = 0;
				$cmd = '/usr/local/libexec/k180/gen_fw_dec';
				if (!is_file($cmd) || !is_executable($cmd)) {
					die(htmlspecialchars($lang['error_notexist_notexe']));
				}
				$fullCmd = 'sudo ' . escapeshellcmd($cmd). ' ' .escapeshellarg($destination);
                exec($fullCmd, $output, $return_var);
				if ($return_var === 0) {
					$message = "<p class='success'>{$lang['fw_upgrade_ok']}</p>";
				} else {
					$message = "<p class='error'>{$lang['fw_upgrade_fail']}</p>";
				}
            } else {
                $message = "<p class='error'>{$lang['move_failed']}</p>";
            }
        } else {
            $message = "<p class='error'>{$lang['invalid_extension']}</p>";
        }
    } else {
        $message = "<p class='error'>{$lang['upload_error']} (Code: $fileError)</p>";
    }
}
?>

<!DOCTYPE html>
<html lang="<?= htmlspecialchars($lang['lang_code']) ?>">
<head>
<meta charset="UTF-8">
<title><?= $lang['nav_upload'] ?></title>
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
        max-width: 400px;
        margin: 2rem auto;
        padding: 2rem;
        border-radius: 10px;
        box-shadow: 0 0 10px rgba(0,0,0,0.1);
    }
    label {
        display: block;
        margin-bottom: 1rem;
        font-size: 1rem;
        color: #333;
    }
    input[type="file"] {
        margin-top: 0.5rem;
		font-weight: bold;
		color: #e0dede;
    }
    button {
        background-color: #5b6cb2;
        color: white;
        padding: 0.75rem 2rem;
        border: none;
        border-radius: 5px;
        font-size: 1rem;
        cursor: pointer;
        margin-top: 1rem;
    }
    button:hover {
        background-color: #4454a3;
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

<div class="container">
    <form method="post" enctype="multipart/form-data">
        <input type="file" name="upload_file" required><br><br>
        <input type="hidden" name="csrf_token" value="<?= htmlspecialchars($_SESSION['csrf_token']) ?>">
        <button type="submit" name="upload_button"><?= $lang['upload_button'] ?></button>
    </form>

    <?= $message ?>
</div>

</body>
</html>

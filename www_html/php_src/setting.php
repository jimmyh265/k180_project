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

// 設定檔案路徑
$jsonFile = "/etc/k180/user_def_setting.json";
$fwverFile = "/var/lib/k180/firmware_ver_info.json";

function get_max_stream_fps($fwverFile) {
    $defaultMaxFps = 30;

    if (!file_exists($fwverFile)) {
        return $defaultMaxFps;
    }

    $fp = fopen($fwverFile, "r");
    if (!$fp) {
        return $defaultMaxFps;
    }

    if (!flock($fp, LOCK_SH)) {
        fclose($fp);
        return $defaultMaxFps;
    }

    $profileData = json_decode(stream_get_contents($fp), true);
    flock($fp, LOCK_UN);
    fclose($fp);

    if (!isset($profileData['sysinfo']['max_stream_fps'])) {
        return $defaultMaxFps;
    }

    $maxFps = intval($profileData['sysinfo']['max_stream_fps']);
    return in_array($maxFps, [30, 60], true) ? $maxFps : $defaultMaxFps;
}

$maxStreamFps = get_max_stream_fps($fwverFile);
$fpsOptions = [];
foreach ([60, 30, 20, 15, 10, 5, 1] as $fpsOption) {
    if ($fpsOption <= $maxStreamFps) {
        $fpsOptions[] = $fpsOption;
    }
}

// 防止 JSON 檔案不存在
if (!file_exists($jsonFile)) {
    die(htmlspecialchars($lang['error_config_not_exist']));
}

// CSRF Token 生成
if (
    empty($_SESSION['csrf_token']) ||
    empty($_SESSION['csrf_token_time']) ||
    time() - $_SESSION['csrf_token_time'] > 600
) {
    $_SESSION['csrf_token'] = bin2hex(random_bytes(32));
    $_SESSION['csrf_token_time'] = time();
}

if ($_SERVER['REQUEST_METHOD'] == 'POST') {
    // 驗證 CSRF Token
    if (
        empty($_POST['csrf_token']) ||
        empty($_SESSION['csrf_token']) ||
        !hash_equals($_SESSION['csrf_token'], $_POST['csrf_token']) ||
        empty($_SESSION['csrf_token_time']) ||
        time() - $_SESSION['csrf_token_time'] > 600
    ) {
        die($lang['error_csrf']);
    }
	
    // 以 r+ 模式開啟檔案（可讀寫）
    $fp = fopen($jsonFile, "r+");
    if (!$fp) {
        die($lang['error_open']);
    }

    // 加入排他鎖，避免同時讀寫
    if (!flock($fp, LOCK_EX)) {
        fclose($fp);
        die($lang['error_lock']);
    }
	
	// 讀取 JSON 設定
	$jsonData = json_decode(stream_get_contents($fp), true);
    if ($jsonData === null) {
        flock($fp, LOCK_UN);
        fclose($fp);
        die($lang['error_json']);
    }
	
    $old_jsonData = $jsonData;
    $stream_cfg_change = false;
    $sys_cfg_change = false;
	
    // 驗證輸入
    $valid_netmasks = [8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32];
    $local_resolution = intval($_POST['res_1']);
    $local_fps = intval($_POST['fps_1']);
    $local_datarate = floatval($_POST['datarate_1']);
    $remote_resolution = intval($_POST['res_2']);
    $remote_fps = intval($_POST['fps_2']);
    $remote_datarate = floatval($_POST['datarate_2']);
    $local_recorded = intval($_POST['local_recorded']);
    $local_rotate180 = intval($_POST['local_rotate180']);
    $sys_ip = $_POST['sys_ip'];
    $sys_netmask = intval($_POST['sys_netmask']);
    $sys_gw = $_POST['sys_gw'];

    if (!in_array($local_fps, $fpsOptions, true) ||
        !in_array($remote_fps, $fpsOptions, true)) {
        flock($fp, LOCK_UN);
        fclose($fp);
        die($lang['error_config_format']);
    }

    // 更新 Local 設定
    if (
        $local_resolution != $old_jsonData['stream_1']['resolution'] ||
        $local_fps != $old_jsonData['stream_1']['fps'] ||
        $local_datarate != $old_jsonData['stream_1']['datarate'] ||
        $remote_resolution != $old_jsonData['stream_2']['resolution'] ||
        $remote_fps != $old_jsonData['stream_2']['fps'] ||
        $remote_datarate != $old_jsonData['stream_2']['datarate'] ||
        $local_recorded != $old_jsonData['system_cfg']['recorded'] ||
        $local_rotate180 != $old_jsonData['system_cfg']['rotate180']
    ) {
        $jsonData['stream_1']['resolution'] = $local_resolution;
        $jsonData['stream_1']['fps'] = $local_fps;
        $jsonData['stream_1']['datarate'] = $local_datarate;
        $jsonData['stream_2']['resolution'] = $remote_resolution;
        $jsonData['stream_2']['fps'] = $remote_fps;
        $jsonData['stream_2']['datarate'] = $remote_datarate;
        $jsonData['system_cfg']['recorded'] = $local_recorded;
        $jsonData['system_cfg']['rotate180'] = $local_rotate180;
        $stream_cfg_change = 1;
    }

    // 更新 System 設定
    if (
        $sys_ip != $old_jsonData['system_cfg']['ipaddress'] ||
        $sys_netmask != $old_jsonData['system_cfg']['netmask'] ||
        $sys_gw != $old_jsonData['system_cfg']['gateway']
    ) {
        if (
            !filter_var($sys_ip, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4) ||
            !filter_var($sys_gw, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4) ||
            !in_array($sys_netmask, $valid_netmasks, true)
        ) {
            header("Location: " . htmlspecialchars($_SERVER['PHP_SELF']));
            exit;
        } else {
            $jsonData['system_cfg']['ipaddress'] = $sys_ip;
            $jsonData['system_cfg']['netmask'] = $sys_netmask;
            $jsonData['system_cfg']['gateway'] = $sys_gw;
            $sys_cfg_change = 1;
        }
    }

    // 儲存設定
    if ($stream_cfg_change || $sys_cfg_change) {
        ftruncate($fp, 0);        // 清空檔案
        rewind($fp);              // 重設指標
        fwrite($fp, json_encode($jsonData, JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE));
        fflush($fp);
	}
	
    // 解鎖與關閉檔案
    flock($fp, LOCK_UN);
    fclose($fp);
	
        if ($sys_cfg_change) {
			$cmd = '/usr/local/libexec/k180/modify_sysip_netplan';
			$cmd2 = '/usr/local/libexec/k180/reboot_system';
			$fullCmd = 'sudo ' . escapeshellcmd($cmd) ;
			$fullCmd2 = 'sudo ' . escapeshellcmd($cmd2) ;
        } else {
            $cmd = '/usr/local/libexec/k180/restart_program_camera';
			$fullCmd = 'sudo ' . escapeshellcmd($cmd);
        }

        // 檢查指令檔存在且可執行
        if (!is_file($cmd) || !is_executable($cmd)) {
            die(htmlspecialchars($lang['error_notexist_notexe']));
        }
		
		shell_exec(escapeshellcmd($fullCmd));
		
        if ($sys_cfg_change) {
			shell_exec(escapeshellcmd($fullCmd2));
        }

		if ($sys_cfg_change) {
			echo "
			<html>
			<head>
			<meta http-equiv='refresh' content='40;url=" . htmlspecialchars($_SERVER['PHP_SELF']) . "'>
			<title>{$lang['rebooting']}</title>
			<style>
				body 
				{ 
					text-align: center; 
					margin-top: 100px; 
					font-size: 24px; 
				}
			</style>
			</head
			<body>
			<p>{$lang['rebooting_msg']}</p>
			<p>40 {$lang['auto_back']}</p>
			</body>
			</html>";
			exit;
		} else {
			header("Location: " . htmlspecialchars($_SERVER['PHP_SELF']));
			exit;
		}
	
}

// === GET 時讀取設定檔 ===
$fp = fopen($jsonFile, "r");
if (!$fp) {
    die($lang['error_open']);
}
if (!flock($fp, LOCK_EX)) {
    fclose($fp);
    die($lang['error_lock']);
}
$jsonData = json_decode(stream_get_contents($fp), true);
flock($fp, LOCK_UN);
fclose($fp);

if ($jsonData === null) {
    die($lang['error_json']);
}

?>

<!DOCTYPE html>
<html lang="<?= htmlspecialchars($lang['lang_code']) ?>">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title><?= $lang['nav_setting'] ?></title>
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
        max-width: 600px;
        margin: 2rem auto;
        padding: 2rem;
        border-radius: 10px;
        box-shadow: 0 0 10px rgba(0,0,0,0.1);
        text-align: left;		
    }
    form label {
        display: block;
        margin-top: 1rem;
        font-size: 1.1rem;
    }
    label span {
        display: inline-block;
        width: 120px;
        font-weight: bold;
		color: #e0dede;
    }
    input[type="text"], select {
        width: 60%;
        padding: 0.5rem;
        margin-top: 0.25rem;
        font-size: 1rem;
        border: 1px solid #ccc;
        border-radius: 5px;
    }
    button {
        margin-top: 2rem;
        padding: 0.75rem 2rem;
        font-size: 1.2rem;
        background-color: #5b6cb2;
        color: white;
        border: none;
        border-radius: 5px;
        cursor: pointer;
    }
    button:hover {
        background-color: #4454a3;
    }
    .section-title {
        margin-top: 2rem;
        font-size: 1.4rem;
        color: #f4f4f4;
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
<form method="post">

    <input type="hidden" name="csrf_token" value="<?= htmlspecialchars($_SESSION['csrf_token']) ?>">

    <div class="section-title">System</div>
    <label><span>IP Address:</span><input type="text" name="sys_ip" maxlength="15" value="<?= htmlspecialchars($jsonData['system_cfg']['ipaddress']) ?>"></label>

    <label><span>Netmask:</span>
    <select name="sys_netmask">
        <?php
		$options = [32 => "255.255.255.255", 
		31 => "255.255.255.254", 
		30 => "255.255.255.252", 
		29 => "255.255.255.248", 
		28 => "255.255.255.240", 
		27 => "255.255.255.224", 
		26 => "255.255.255.192", 
		25 => "255.255.255.128", 
		24 => "255.255.255.0", 
		23 => "255.255.254.0", 
		22 => "255.255.252.0", 
		21 => "255.255.248.0", 
		20 => "255.255.240.0", 
		19 => "255.255.224.0", 
		18 => "255.255.192.0", 
		17 => "255.255.128.0", 
		16 => "255.255.0.0", 
		15 => "255.254.0.0", 
		14 => "255.252.0.0", 
		13 => "255.248.0.0", 
		12 => "255.240.0.0", 
		11 => "255.224.0.0", 
		10 => "255.192.0.0", 
		9 => "255.128.0.0", 
		8 => "255.0.0.0"];
        foreach ($options as $value => $label) {
            echo '<option value="' . $value . '" ' . ($jsonData['system_cfg']['netmask'] == $value ? 'selected' : '') . '>' . htmlspecialchars($label) . '</option>';
        }
        ?>
    </select>
    </label>

    <label><span>Gateway:</span><input type="text" name="sys_gw" maxlength="15" value="<?= htmlspecialchars($jsonData['system_cfg']['gateway']) ?>"></label>

    <label><span><?= $lang['record'] ?>:</span>
    <select name="local_recorded">
        <?php
        $options = [0 => "OFF", 1 => "1 & 2 & 3 & 4", 2 => "1 / 2 / 3 / 4"];
        foreach ($options as $value => $label) {
            echo '<option value="' . $value . '" ' . ($jsonData['system_cfg']['recorded'] == $value ? 'selected' : '') . '>' . htmlspecialchars($label) . '</option>';
        }
        ?>
    </select>
    </label>

    <label><span><?= $lang['rotate_180'] ?>:</span>
    <select name="local_rotate180">
        <?php
        $options = [0 => "0", 1 => "180"];
        foreach ($options as $value => $label) {
            echo '<option value="' . $value . '" ' . ($jsonData['system_cfg']['rotate180'] == $value ? 'selected' : '') . '>' . htmlspecialchars($label) . '</option>';
        }
        ?>
    </select>
    </label>
	<br>
    <div class="section-title">Stream 1</div>
    <label><span>Resolution:</span>
    <select name="res_1">
        <?php foreach ([1080, 720] as $option): ?>
            <option value="<?= $option ?>" <?= ($jsonData['stream_1']['resolution'] == $option ? 'selected' : '') ?>><?= $option ?>P</option>
        <?php endforeach; ?>
    </select>
    </label>

    <label><span>FPS:</span>
    <select name="fps_1">
        <?php foreach ($fpsOptions as $option): ?>
            <option value="<?= $option ?>" <?= ($jsonData['stream_1']['fps'] == $option ? 'selected' : '') ?>><?= $option ?></option>
        <?php endforeach; ?>
    </select>
    </label>

    <label><span>Datarate:</span>
    <select name="datarate_1">
        <?php foreach ([8, 6, 4, 3, 2, 1.6, 1] as $option): ?>
            <option value="<?= $option ?>" <?= ($jsonData['stream_1']['datarate'] == $option ? 'selected' : '') ?>><?= $option ?>Mbps</option>
        <?php endforeach; ?>
    </select>
    </label>
	<br>
    <div class="section-title">Stream 2</div>
    <label><span>Resolution:</span>
    <select name="res_2">
        <?php foreach ([1080, 720] as $option): ?>
            <option value="<?= $option ?>" <?= ($jsonData['stream_2']['resolution'] == $option ? 'selected' : '') ?>><?= $option ?>P</option>
        <?php endforeach; ?>
    </select>
    </label>

    <label><span>FPS:</span>
    <select name="fps_2">
        <?php foreach ($fpsOptions as $option): ?>
            <option value="<?= $option ?>" <?= ($jsonData['stream_2']['fps'] == $option ? 'selected' : '') ?>><?= $option ?></option>
        <?php endforeach; ?>
    </select>
    </label>

    <label><span>Datarate:</span>
    <select name="datarate_2">
        <?php foreach ([8, 6, 4, 3, 2, 1.6, 1] as $option): ?>
            <option value="<?= $option ?>" <?= ($jsonData['stream_2']['datarate'] == $option ? 'selected' : '') ?>><?= $option ?>Mbps</option>
        <?php endforeach; ?>
    </select>
    </label>

	<button type="submit"><?= $lang['save_button'] ?></button>
</form>
</div>

</body>
</html>

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

// 產生或更新 CSRF Token（每30分鐘過期）
if (
    empty($_SESSION['csrf_token']) ||
    empty($_SESSION['csrf_token_time']) ||
    time() - $_SESSION['csrf_token_time'] > 600
) {
    $_SESSION['csrf_token'] = bin2hex(random_bytes(32));
    $_SESSION['csrf_token_time'] = time();
}

$directory = '/data';
$tmpDirectory = '/var/lib/k180/tmp_rec_zip';
$files = [];
$fileGroups = [];
$message = '';
if (isset($_SESSION['flash_message'])) {
    $message = $_SESSION['flash_message'];
    unset($_SESSION['flash_message']);
}

if (is_dir($directory)) {
    $files = array_filter(scandir($directory), function($file) use ($directory) {
        return is_file($directory . DIRECTORY_SEPARATOR . $file);
    });

    foreach ($files as $file) {
        if (preg_match('/^S1_ch\d+_(\d{8})_\d{4}\.mp4$/i', $file, $matches)) {
            $date = $matches[1];
            if (!isset($fileGroups[$date])) {
                $fileGroups[$date] = [];
            }
            $fileGroups[$date][] = $file;
        }
    }
    krsort($fileGroups);
}

// 處理下載/刪除請求
if ($_SERVER['REQUEST_METHOD'] === 'POST') {

    // 檢查 CSRF Token
    if (
        empty($_POST['csrf_token']) ||
        empty($_SESSION['csrf_token']) ||
        !hash_equals($_SESSION['csrf_token'], $_POST['csrf_token']) ||
        empty($_SESSION['csrf_token_time']) ||
        time() - $_SESSION['csrf_token_time'] > 600
    ) {
        die($lang['error_csrf']);
    }

    $selectedFiles = $_POST['videoFile'] ?? [];
    if (!is_array($selectedFiles)) {
        $selectedFiles = [];
    }
	$action = $_POST['action'] ?? '';

	if (empty($selectedFiles)) {
		$message = "<p class='error'>{$lang['error_no_selection']}</p>";
	} else if ($action === 'delete') {
			$deleted = 0;
			foreach ($selectedFiles as $file) {
				$safeFile = basename($file);
				$filePath = realpath($directory . DIRECTORY_SEPARATOR . $safeFile);
				if ($filePath !== false && strpos($filePath, realpath($directory)) === 0 && is_file($filePath)) {
					if (@unlink($filePath)) {
						$deleted++;
					}
				}
			}
			if ($deleted > 0) {
				$message = "<p class='success'>{$lang['delete_success']} ($deleted)</p>";
			} else {
				$message = "<p class='error'>{$lang['delete_fail']}</p>";
			}

		// 重新整理畫面：避免重新送出 POST
		$_SESSION['flash_message'] = $message;
		header("Location: " . $_SERVER['PHP_SELF']);
		exit;
	} else if ($action === 'download') {
			$downloadToken = $_POST['download_token'] ?? '';
			$downloadToken = is_string($downloadToken) && preg_match('/^[A-Fa-f0-9]{32}$/', $downloadToken) ? $downloadToken : '';

			// 檢查選取檔案數量，最多500個
			$ttl = 120;
			foreach (glob($tmpDirectory . '/*.zip*') as $oldZip) {
				if (is_file($oldZip) && time() - filemtime($oldZip) > $ttl) {
					@unlink($oldZip);
			}
		}
					
		if (count($selectedFiles) > 500) {
			$message = "<p class='error'>{$lang['error_too_many']}</p>";
		} else {
			$totalSize = 0;
			$validFiles = [];
			$realDirectory = realpath($directory);
			ini_set('max_execution_time', 180);
			foreach ($selectedFiles as $file) {
				$safeFile = basename($file);

				// 再次驗證檔名格式
				if (!preg_match('/^S1_ch\d+_\d{8}_\d{4}\.mp4$/i', $safeFile)) {
					continue;
				}

				$filePath = realpath($directory . DIRECTORY_SEPARATOR . $safeFile);

				// 確保 realpath 出來的路徑仍在正確的資料夾內
				if ($filePath !== false && strpos($filePath, $realDirectory) === 0 && is_file($filePath)) {
					$totalSize += filesize($filePath);
					$validFiles[] = $safeFile;
				}
			}

			$maxSize = 1000 * 1024 * 1024; // 1000MB
			if ($totalSize > $maxSize) {
				$message = "<p class='error'>{$lang['error_file_size']}</p>";
			} elseif (empty($validFiles)) {
				$message = "<p class='error'>{$lang['error_no_valid']}</p>";
			} else {
					if (count($validFiles) === 1) {
						// 單檔案直接下載
						$file = $validFiles[0];
						$filePath = realpath($directory . DIRECTORY_SEPARATOR . $file);
						if ($filePath !== false && strpos($filePath, $realDirectory) === 0 && is_file($filePath)) {
							if ($downloadToken !== '') {
								setcookie('recording_download_token', $downloadToken, [
									'expires' => time() + 300,
									'path' => '/',
									'samesite' => 'Strict',
								]);
							}
							header('Content-Description: File Transfer');
							header('Content-Type: application/octet-stream');
							header('Content-Disposition: attachment; filename="' . rawurlencode($file) . '"');
							header('Expires: 0');
							header('Cache-Control: must-revalidate');
						header('Pragma: public');
						header('Content-Length: ' . filesize($filePath));
						readfile($filePath);
						exit;
					} else {
						$message = "<p class='error'>{$lang['error_file_not_found']}</p>";
					}
				} else {
					
					$lock_fp = fopen($tmpDirectory . '/zip_batch.lock', 'w');
					if (flock($lock_fp, LOCK_EX)) {
						// 多檔案打包成 ZIP
						$zip = new ZipArchive();

						$dates = [];
						foreach ($validFiles as $file) {
							if (preg_match('/S1_ch\d+_(\d{8})_\d{4}\.mp4$/i', $file, $matches)) {
								$dates[] = $matches[1];
							}
						}
						sort($dates);

						if (count($dates) === 1) {
							$dateRange = substr($dates[0], 0, 4) . '-' . substr($dates[0], 4, 2) . '-' . substr($dates[0], 6, 2);
						} elseif (count($dates) >= 2) {
							$start = substr($dates[0], 0, 4) . '-' . substr($dates[0], 4, 2) . '-' . substr($dates[0], 6, 2);
							$end = substr($dates[count($dates) - 1], 0, 4) . '-' . substr($dates[count($dates) - 1], 4, 2) . '-' . substr($dates[count($dates) - 1], 6, 2);
							$dateRange = $start . '_to_' . $end;
						} else {
							$dateRange = date('Y-m-d_His');
						}

						$zipName = $dateRange . '.zip';
						$zipPath = $tmpDirectory . DIRECTORY_SEPARATOR . $zipName;



						if ($zip->open($zipPath, ZipArchive::CREATE) === true) {
							foreach ($validFiles as $file) {
								$filePath = realpath($directory . DIRECTORY_SEPARATOR . $file);
								if ($filePath !== false && strpos($filePath, $realDirectory) === 0 && is_file($filePath)) {
									$zip->addFile($filePath, $file);
								}
							}
							$zip->close();

								if (file_exists($zipPath)) {
									if ($downloadToken !== '') {
										setcookie('recording_download_token', $downloadToken, [
											'expires' => time() + 300,
											'path' => '/',
											'samesite' => 'Strict',
										]);
									}
									header('Content-Type: application/zip');
									header('Content-Disposition: attachment; filename="' . rawurlencode($zipName) . '"');
									header('Content-Length: ' . filesize($zipPath));
									readfile($zipPath);
									unlink($zipPath); // 清除暫存ZIP
								exit;
							} else {
								$message = "<p class='error'>{$lang['error_zip_fail']}</p>";
							}
						} else {
							$message =  "<p class='error'>{$lang['error_zip_fail']}</p>";
						}
					
						flock($lock_fp, LOCK_UN);
					} else {
						$message =  "<p class='error'>{$lang['error_zip_fail']}</p>";
					}
					fclose($lock_fp);

				}
			}
		}
	}

}
?>

<!DOCTYPE html>
<html lang="<?= htmlspecialchars($lang['lang_code']) ?>">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title><?= $lang['nav_recordings'] ?></title>
<style>
    body {
        overflow-x: hidden;
        padding-bottom: 40px;
        font-family: Arial, sans-serif;
        background-color: #000000;
        margin: 0;
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
        width: 100%;
        text-align: center;
        margin-bottom: 4rem;
    }
    form {
        text-align: left;
        display: inline-block;
        margin-top: 2rem;
        width: 90%;
        max-width: 600px;
    }
    .date-group {
        background: #fff;
        margin-bottom: 1rem;
        border-radius: 8px;
        box-shadow: 0 0 5px rgba(0,0,0,0.1);
        overflow: hidden;
        width: 100%;
    }
    .date-header {
        background-color: #5b6cb2;
        color: #fff;
        padding: 1rem;
        cursor: pointer;
        font-size: 1.25rem;
        text-align: center;
        user-select: none;
    }
    .file-list {
        padding: 1rem;
        display: none;
        background-color: white;
        width: 100%;
        box-sizing: border-box;
    }
    .file-list.show {
        display: block;
    }
    .file-list label {
        display: block;
        margin-bottom: 0.5rem;
        font-size: 1.1rem;
        cursor: pointer;
    }
    button {
        font-size: 1.5em;
        margin-top: 2rem;
        background-color: #5b6cb2;
        color: white;
        padding: 0.75rem 2rem;
        border: none;
        border-radius: 5px;
        cursor: pointer;
    }
    button:hover {
        background-color: #4454a3;
    }
    .footer-text {
        position: fixed;
        bottom: 0;
        width: 100%;
        text-align: center;
        font-size: 18px;
        color: #000;
        padding: 0.5rem 0;
        background-color: #fff;
        border-top: 1px solid #ccc;
    }
    .error-message {
        color: red;
        margin-top: 1rem;
    }
	.select-buttons {
		margin-bottom: 0.5rem;
		text-align: right;
		padding: 0 1rem;
	}

	.select-buttons button {
		font-size: 0.9rem;
		padding: 0.3rem 0.8rem;
		margin-left: 0.5rem;
		background-color: #5b6cb2;
		color: white;
		border: none;
		border-radius: 4px;
		cursor: pointer;
	}

	.select-buttons button:hover {
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
<script>
function toggleFileList(id) {
    var elem = document.getElementById(id);
    if (elem.classList.contains('show')) {
        elem.classList.remove('show');
    } else {
        elem.classList.add('show');
    }
}
</script>
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

<?= $message ?>
<?php if (count($fileGroups) > 0): ?>
<div class="container">
	    <form id="rec_download" method="post" action="recordings.php">
		<input type="hidden" name="csrf_token" value="<?= htmlspecialchars($_SESSION['csrf_token']) ?>">
		<input type="hidden" name="action" id="actionInput" value="">
		<input type="hidden" name="download_token" id="downloadTokenInput" value="">
	
        <?php foreach ($fileGroups as $date => $files): ?>
            <div class="date-group">
                <div class="date-header" onclick="toggleFileList('file-list-<?= $date ?>')">
                    <?= htmlspecialchars(substr($date, 0, 4) . '/' . substr($date, 4, 2) . '/' . substr($date, 6, 2)) ?>
                </div>
<div class="file-list" id="file-list-<?= $date ?>">
    <div class="select-buttons">
        <button type="button" onclick="selectAll('file-list-<?= $date ?>')"><?= $lang['select_all'] ?></button>
        <button type="button" onclick="deselectAll('file-list-<?= $date ?>')"><?= $lang['deselect_all'] ?></button>
    </div>
    <?php foreach ($files as $file): ?>
        <label>
            <input type="checkbox" name="videoFile[]" value="<?= htmlspecialchars($file) ?>">
            <?= htmlspecialchars($file) ?>
        </label>
    <?php endforeach; ?>
</div>
            </div>
        <?php endforeach; ?>
		<div style="display: flex; justify-content: space-between;">
			<button type="button" id="downloadBtn" onclick="handleDownload()" data-default-text="<?= htmlspecialchars($lang['download_selected'], ENT_QUOTES) ?>"><?= $lang['download_selected'] ?></button>
		<button type="submit" name="action" value="delete" onclick="return confirm('<?= $lang['confirm_delete_files'] ?>')"><?= $lang['delete'] ?></button>
		</div>
    </form>
</div>
<?php else: ?>
<p><?= $lang['no_files'] ?></p>
<?php endif; ?>
<script>
function toggleFileList(id) {
    var elem = document.getElementById(id);
    if (elem.classList.contains('show')) {
        elem.classList.remove('show');
    } else {
        elem.classList.add('show');
    }
}

function selectAll(listId) {
    var list = document.getElementById(listId);
    var checkboxes = list.querySelectorAll('input[type="checkbox"]');
    checkboxes.forEach(function(checkbox) {
        checkbox.checked = true;
    });
}

function deselectAll(listId) {
    var list = document.getElementById(listId);
    var checkboxes = list.querySelectorAll('input[type="checkbox"]');
    checkboxes.forEach(function(checkbox) {
        checkbox.checked = false;
    });
}

function createDownloadToken() {
  const bytes = new Uint8Array(16);
  if (window.crypto && window.crypto.getRandomValues) {
    window.crypto.getRandomValues(bytes);
  } else {
    for (let i = 0; i < bytes.length; i++) {
      bytes[i] = Math.floor(Math.random() * 256);
    }
  }
  return Array.from(bytes, function(byte) {
    return byte.toString(16).padStart(2, '0');
  }).join('');
}

function getCookie(name) {
  const cookie = document.cookie.split('; ').find(function(row) {
    return row.startsWith(name + '=');
  });
  return cookie ? decodeURIComponent(cookie.split('=').slice(1).join('=')) : '';
}

function clearCookie(name) {
  document.cookie = name + '=; Max-Age=0; path=/; SameSite=Strict';
}

function resetDownloadState() {
  const btn = document.getElementById('downloadBtn');
  if (btn) {
    btn.disabled = false;
    btn.innerText = btn.dataset.defaultText;
  }

  document.querySelectorAll('input[name="videoFile[]"]').forEach(function(checkbox) {
    checkbox.checked = false;
  });

  const actionInput = document.getElementById('actionInput');
  if (actionInput) {
    actionInput.value = '';
  }

  const downloadTokenInput = document.getElementById('downloadTokenInput');
  if (downloadTokenInput) {
    downloadTokenInput.value = '';
  }
}

function handleDownload() {
  const form = document.getElementById('rec_download');
  const actionInput = document.getElementById('actionInput');
  const downloadTokenInput = document.getElementById('downloadTokenInput');

  // 設定欲執行的動作
  actionInput.value = 'download';
  const downloadToken = createDownloadToken();
  downloadTokenInput.value = downloadToken;

  // 防止重複點擊
  const btn = document.getElementById('downloadBtn');
  btn.disabled = true;
  btn.innerText = 'Compressing...';

  const cookieName = 'recording_download_token';
  let pollInterval;
  const resetTimeout = window.setTimeout(function() {
    window.clearInterval(pollInterval);
    resetDownloadState();
  }, 180000);

  pollInterval = window.setInterval(function() {
    if (getCookie(cookieName) === downloadToken) {
      window.clearInterval(pollInterval);
      window.clearTimeout(resetTimeout);
      clearCookie(cookieName);
      resetDownloadState();
    }
  }, 500);

  // 提交表單
  form.submit();
}

</script>

</body>
</html>

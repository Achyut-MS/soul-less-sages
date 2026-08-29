# tests/integration_tests.ps1
# 5 curl-based integration tests proving routing behavior (Windows PowerShell version)
param(
    [switch]$NoBuild
)

$port = 28080

if (-not $NoBuild) {
    Write-Host "=== Building executable ==="
    mingw32-make clean
    mingw32-make all
}

Write-Host "=== Launching local server on port $port ==="
# Spawn the server in the background
$serverProcess = Start-Process -FilePath "./mdview.exe" -ArgumentList "-p $port" -PassThru -NoNewWindow
Start-Sleep -Seconds 2

$failed = 0

# Test 1: GET / (serve index.html)
Write-Host -NoNewline "Test 1: GET / ... "
try {
    $resp = & curl.exe --% -s -i http://127.0.0.1:28080/
    if ($resp -match "200 OK" -and $resp -match "SoullessSages") {
        Write-Host "PASSED" -ForegroundColor Green
    } else {
        Write-Host "FAILED" -ForegroundColor Red
        $failed++
    }
} catch {
    Write-Host "FAILED ($_)" -ForegroundColor Red
    $failed++
}

# Test 2: GET /static/styles.css
Write-Host -NoNewline "Test 2: GET /static/styles.css ... "
try {
    $resp = & curl.exe --% -s -i http://127.0.0.1:28080/static/styles.css
    if ($resp -match "200 OK" -and $resp -match "text/css") {
        Write-Host "PASSED" -ForegroundColor Green
    } else {
        Write-Host "FAILED" -ForegroundColor Red
        $failed++
    }
} catch {
    Write-Host "FAILED ($_)" -ForegroundColor Red
    $failed++
}

# Test 3: POST /render (Success)
Write-Host -NoNewline "Test 3: POST /render (Success) ... "
try {
    $resp = & curl.exe --% -s -i -X POST -H "Content-Type: application/json" -d "{\"md\":\"# Hello\"}" http://127.0.0.1:28080/render
    if ($resp -match "200 OK" -and $resp -match "Hello") {
        Write-Host "PASSED" -ForegroundColor Green
    } else {
        Write-Host "FAILED" -ForegroundColor Red
        $failed++
    }
} catch {
    Write-Host "FAILED ($_)" -ForegroundColor Red
    $failed++
}

# Test 4: POST /serialize (Success)
Write-Host -NoNewline "Test 4: POST /serialize (Success) ... "
try {
    $resp = & curl.exe --% -s -i -X POST -H "Content-Type: application/json" -d "{\"html\":\"<p>Stub</p>\"}" http://127.0.0.1:28080/serialize
    if ($resp -match "200 OK" -and $resp -match "Stub") {
        Write-Host "PASSED" -ForegroundColor Green
    } else {
        Write-Host "FAILED" -ForegroundColor Red
        $failed++
    }
} catch {
    Write-Host "FAILED ($_)" -ForegroundColor Red
    $failed++
}

# Test 5: GET /nonexistent (404)
Write-Host -NoNewline "Test 5: GET /nonexistent (404) ... "
try {
    $resp = & curl.exe --% -s -i http://127.0.0.1:28080/nonexistent
    if ($resp -match "404 Not Found") {
        Write-Host "PASSED" -ForegroundColor Green
    } else {
        Write-Host "FAILED" -ForegroundColor Red
        $failed++
    }
} catch {
    Write-Host "FAILED ($_)" -ForegroundColor Red
    $failed++
}

# Test 6: POST /render (413 Request Entity Too Large)
Write-Host -NoNewline "Test 6: POST /render (413 Payload Too Large) ... "
try {
    $resp = & curl.exe --% -s -i -X POST -H "Content-Type: application/json" -H "Content-Length: 9000000" -d "" http://127.0.0.1:28080/render
    if ($resp -match "413 Request Entity Too Large") {
        Write-Host "PASSED" -ForegroundColor Green
    } else {
        Write-Host "FAILED" -ForegroundColor Red
        $failed++
    }
} catch {
    Write-Host "FAILED ($_)" -ForegroundColor Red
    $failed++
}

# Test 7: GET /file (Retrieve initial file payload)
Write-Host -NoNewline "Test 7: GET /file ... "
try {
    $resp = & curl.exe --% -s -i http://127.0.0.1:28080/file
    if ($resp -match "200 OK" -and $resp -match "content") {
        Write-Host "PASSED" -ForegroundColor Green
    } else {
        Write-Host "FAILED" -ForegroundColor Red
        $failed++
    }
} catch {
    Write-Host "FAILED ($_)" -ForegroundColor Red
    $failed++
}

# Test 8: POST /save (Save content)
Write-Host -NoNewline "Test 8: POST /save ... "
try {
    $resp = & curl.exe --% -s -i -X POST -H "Content-Type: application/json" -d "{\"content\":\"# Saved Title\"}" http://127.0.0.1:28080/save
    if ($resp -match "200 OK" -and $resp -match "ok") {
        Write-Host "PASSED" -ForegroundColor Green
    } else {
        Write-Host "FAILED" -ForegroundColor Red
        $failed++
    }
} catch {
    Write-Host "FAILED ($_)" -ForegroundColor Red
    $failed++
}

Write-Host "=== Tearing down server ==="
try {
    $null = & curl.exe --% -s -X POST http://127.0.0.1:28080/shutdown
} catch {}
Start-Sleep -Seconds 1
if (-not $serverProcess.HasExited) {
    Stop-Process -Id $serverProcess.Id -Force
}

# Test 9: CLI -h help display
Write-Host -NoNewline "Test 9: CLI -h flag ... "
try {
    $helpOut = & ./mdview.exe -h 2>&1
    if ($helpOut -match "Usage:") {
        Write-Host "PASSED" -ForegroundColor Green
    } else {
        Write-Host "FAILED" -ForegroundColor Red
        $failed++
    }
} catch {
    Write-Host "FAILED ($_)" -ForegroundColor Red
    $failed++
}

# Test 10: CLI invalid flag error handling
Write-Host -NoNewline "Test 10: CLI invalid flag ... "
try {
    $invalidOut = & ./mdview.exe -z 2>&1
    if ($invalidOut -match "Usage:") {
        Write-Host "PASSED" -ForegroundColor Green
    } else {
        Write-Host "FAILED" -ForegroundColor Red
        $failed++
    }
} catch {
    Write-Host "FAILED ($_)" -ForegroundColor Red
    $failed++
}

if ($failed -eq 0) {
    Write-Host "All integration tests passed successfully!" -ForegroundColor Green
    exit 0
} else {
    Write-Host "$failed integration tests failed." -ForegroundColor Red
    exit 1
}

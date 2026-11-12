# Two-player MK8 Deluxe native Online Play test on one Windows PC.
# Runs the room service, the native adapter (TCP 443, NAT UDP 10025/10125) and two patched Eden
# instances, each a portable copy with its own user folder (Windows Eden ignores %APPDATA% overrides).
#
#   1. tools\dev-scripts\build-switch-core.ps1
#   2. switch-native-test.ps1 -Setup -Keys <your prod.keys>
#        Opens player 1's Eden: install firmware and the game update (4.0.1), close it.
#        Player 2 then gets a copy of that user folder.
#   3. switch-native-test.ps1 -Game <your MK8 Deluxe .nsp/.xci>
#        Host: Online Play > Friends > Create Room. Player 2: enter the room code.
#      Add -Pad to drive both games by script: player n gets a DSU pad on UDP 2675(9+n), commanded over
#      TCP 127.0.0.1:2677n with dsu-pad.py's commands (tap CIRCLE 150, dpad DOWN 150, ...).
# Everything lives under build\switch-native (ignored by git). Logs: build\switch-native\*.log
param(
    [string]$Game,
    [switch]$Setup,
    [string]$Keys,
    [switch]$Pad
)
$ErrorActionPreference = 'Continue'  # PS 5.1 makes native stderr fatal under Stop; exit codes are checked instead
$repo = (Resolve-Path "$PSScriptRoot\..\..").Path
$core = "$repo\build\switch-core\bin"
$work = "$repo\build\switch-native"
$native = "$repo\uscl-switch-online\native"
if (-not (Test-Path "$core\eden.exe")) { throw "Build Eden first: tools\dev-scripts\build-switch-core.ps1" }
New-Item -ItemType Directory -Force $work | Out-Null

function Player($n) {
    $dir = "$work\p$n"
    robocopy $core $dir /E /XD user /NFL /NDL /NJH /NJS | Out-Null  # refresh binaries, keep user\
    if ($LASTEXITCODE -ge 8) { throw "copy to $dir failed" }
    return $dir
}

if ($Setup) {
    $p1 = Player 1
    New-Item -ItemType Directory -Force "$p1\user\keys" | Out-Null
    if ($Keys) { Copy-Item $Keys "$p1\user\keys\prod.keys" }
    if (-not (Test-Path "$p1\user\keys\prod.keys")) { throw 'Pass -Keys <path to your prod.keys>' }
    Write-Host 'Install firmware and the MK8 Deluxe update in this window, then close it.'
    Start-Process "$p1\eden.exe" -Wait
    $p2 = Player 2
    robocopy "$p1\user" "$p2\user" /MIR /NFL /NDL /NJH /NJS | Out-Null
    if ($LASTEXITCODE -ge 8) { throw 'copy of player 1 profile failed' }
    Write-Host 'Player 2 profile copied from player 1. Next: -Game <path>.'
    return
}
if (-not $Game) { throw 'Pass -Game <path to MK8 Deluxe .nsp/.xci>, or -Setup first.' }
foreach ($n in 1, 2) {
    if (-not (Test-Path "$work\p$n\user\keys\prod.keys")) { throw 'Run -Setup first.' }
    Player $n | Out-Null  # pick up a rebuilt eden.exe
    # Eden saves absolute nand/sdmc/... paths; player 2's copied config would point at player 1's folders.
    $config = "$work\p$n\user\config\qt-config.ini"
    if (Test-Path $config) {
        $text = [IO.File]::ReadAllText($config)
        [IO.File]::WriteAllText($config, ($text -replace 'switch-native/p\d/', "switch-native/p$n/"))
    }
    if ($Pad) {
        & python "$PSScriptRoot\eden-dsu-config.py" $config (26759 + $n)
        if ($LASTEXITCODE) { throw 'pad binding failed' }
    }
}

$python = (Get-Command python).Source
$control = "$repo\build\switch-online-target\debug\uscl-switch-online.exe"
if (-not (Test-Path $control)) {
    cargo build --locked --manifest-path "$repo\uscl-switch-online\Cargo.toml" --target-dir "$repo\build\switch-online-target"
    if ($LASTEXITCODE) { throw 'room service build failed' }
}
foreach ($n in 1, 2) {
    if (-not (Test-Path "$work\p$n.identity")) { & $python "$native\identity.py" "$work\p$n.identity" }
}
if (-not (Test-Path "$work\localhost.pem")) {
    # Git for Windows ships openssl; the certificate is local-only and never committed.
    $openssl = (Get-Command openssl -ErrorAction SilentlyContinue).Source
    if (-not $openssl) { $openssl = "$env:ProgramFiles\Git\usr\bin\openssl.exe" }
    & $openssl req -x509 -newkey rsa:2048 -nodes -days 30 -subj /CN=localhost `
        -keyout "$work\localhost.key" -out "$work\localhost.pem" 2>$null
    if ($LASTEXITCODE) { throw 'certificate creation failed' }
}

$procs = @()
try {
    $procs += Start-Process $control -ArgumentList '--port', '24873' -PassThru -NoNewWindow `
        -RedirectStandardOutput "$work\control.log" -RedirectStandardError "$work\control.err.log"
    Start-Sleep -Seconds 1
    $procs += Start-Process $python -WorkingDirectory $native -PassThru -NoNewWindow `
        -ArgumentList '-u', 'adapter.py', '--cert', "`"$work\localhost.pem`"", '--key', "`"$work\localhost.key`"",
            '--account', "`"$work\p1.identity`"", '--account', "`"$work\p2.identity`"", '--control-port', '24873', '--nat' `
        -RedirectStandardOutput "$work\adapter.log" -RedirectStandardError "$work\adapter.err.log"
    if ($Pad) {
        foreach ($n in 1, 2) {
            $procs += Start-Process $python -PassThru -NoNewWindow -ArgumentList '-u', "`"$PSScriptRoot\dsu-pad.py`"", (26770 + $n), (26759 + $n) `
                -RedirectStandardOutput "$work\pad$n.log" -RedirectStandardError "$work\pad$n.err.log"
        }
        $env:USCL_UDP_POLL_MS = '10'  # Eden patch 0007: poll the pad often enough for menu taps
    }
    Start-Sleep -Seconds 2
    if ($procs | Where-Object HasExited) { throw "a service exited; see $work\*.log" }

    $env:USCL_MK8_NATIVE_DIAGNOSTIC = '1'
    $eden = foreach ($n in 1, 2) {
        $env:USCL_MK8_NATIVE_CREDENTIAL_FILE = "$work\p$n.identity"
        $env:USCL_MK8_NATIVE_LOCAL_ADDRESS = "127.0.0.$(10 + $n)"  # own address per console (Eden patch 0010)
        Start-Process "$work\p$n\eden.exe" -ArgumentList '-g', "`"$Game`"" -PassThru
        Start-Sleep -Seconds 8  # two Eden instances loading their pipeline caches at once crashed (0xc0000005)
    }
    Write-Host "Both players running. Adapter events: $work\adapter.log. Close both games to stop."
    $eden | Wait-Process
}
finally {
    Remove-Item Env:USCL_MK8_NATIVE_DIAGNOSTIC, Env:USCL_MK8_NATIVE_CREDENTIAL_FILE, Env:USCL_MK8_NATIVE_LOCAL_ADDRESS, Env:USCL_UDP_POLL_MS -ErrorAction SilentlyContinue
    $procs | Where-Object { -not $_.HasExited } | Stop-Process -Force
    Write-Host 'Join events:'
    Select-String -Path "$work\adapter.log" -Pattern 'native_room_code_resolved|native_room_joined|native_session_urls|native_room_join_denied' |
        ForEach-Object Line
}

# probe-status.ps1 — live ARMED/DISARMED indicator for parry-tell-probe v6.4+
#
# Tails the active session's .log.txt file and prints any F11: armed /
# F11: disarmed transitions to a console window with a timestamp.
#
# Usage: from PowerShell on the station box:
#   .\probe-status.ps1
# Optional: -LogDir <path>   (default: C:\Projects\elden-ring\logs)
#
# Run in a second monitor / sidebar window during gameplay. Audible beeps
# from the probe itself (Beep at 660 Hz × 2 for ARMED, 1320 Hz × 1 for
# DISARMED) are the primary feedback; this script is the visual confirm.

param(
    [string]$LogDir = "C:\Projects\elden-ring\logs"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $LogDir)) {
    Write-Host "log dir not found: $LogDir" -ForegroundColor Red
    Write-Host "make sure the probe has been run at least once to create the dir"
    exit 1
}

Write-Host "parry-tell-probe status tail — watching $LogDir" -ForegroundColor Cyan
Write-Host "press Ctrl+C to stop" -ForegroundColor DarkGray
Write-Host ""

$state = "UNKNOWN"
$lastLog = $null
$lastSize = 0
$lastCreationTime = $null
$partialLine = ""

while ($true) {
    # find the newest .log.txt under LogDir
    $latest = Get-ChildItem -Path $LogDir -Filter "*.log.txt" -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending | Select-Object -First 1

    if ($null -eq $latest) {
        Start-Sleep -Milliseconds 500
        continue
    }

    # Detect (a) different file path, (b) same path but creation time
    # changed (probe re-opened the file), (c) same path but file is
    # SMALLER than what we've read (truncate / wb reopen on same name).
    # Any of these = new session, reset our position.
    $isNewSession = $false
    if ($latest.FullName -ne $lastLog) {
        $isNewSession = $true
    }
    elseif ($null -ne $lastCreationTime -and
            $latest.CreationTime -ne $lastCreationTime) {
        $isNewSession = $true
    }
    elseif ($latest.Length -lt $lastSize) {
        $isNewSession = $true
    }

    if ($isNewSession) {
        Write-Host "[$(Get-Date -Format 'HH:mm:ss')] new session log: $($latest.Name)" -ForegroundColor Yellow
        $lastLog = $latest.FullName
        $lastSize = 0
        $lastCreationTime = $latest.CreationTime
        $state = "UNKNOWN"
        $partialLine = ""
    }

    if ($latest.Length -gt $lastSize) {
        $newPos = $lastSize
        $stream = [System.IO.File]::Open($latest.FullName, 'Open', 'Read', 'ReadWrite')
        try {
            $stream.Seek($lastSize, 'Begin') | Out-Null
            $reader = New-Object System.IO.StreamReader($stream)
            $newContent = $reader.ReadToEnd()
            # Use the ACTUAL stream position after reading (not the
            # stale FileInfo.Length snapshot). Avoids replay if the
            # writer appended between our metadata read and our open.
            $newPos = $stream.Position
            $reader.Close()
        } finally {
            $stream.Close()
        }
        $lastSize = $newPos

        # Carry any partial trailing line (no \n yet) across polls so
        # we never half-parse a record being written.
        $buf = $partialLine + $newContent
        $lines = $buf -split "`r?`n"
        # last element after split is either "" (clean newline ending) or
        # a partial line; keep it for the next poll.
        # Edge case: if $lines.Length == 1 the buffer contained NO newline,
        # so the whole buffer is a single partial line — don't index 0..-1
        # which PowerShell happily evaluates and would re-parse the partial.
        if ($lines.Length -lt 2) {
            $partialLine = $buf
            $completeLines = @()
        } else {
            $partialLine = $lines[-1]
            $completeLines = $lines[0..($lines.Length - 2)]
        }

        foreach ($line in $completeLines) {
            if ($line -match "F11: armed$") {
                $state = "ARMED"
                Write-Host "[$(Get-Date -Format 'HH:mm:ss')] ARMED   (recording)" -ForegroundColor Green
            }
            elseif ($line -match "F11: disarmed$") {
                $state = "DISARMED"
                Write-Host "[$(Get-Date -Format 'HH:mm:ss')] DISARMED (paused)  " -ForegroundColor Red
            }
            elseif ($line -match "F11: roster ENABLED on retry") {
                Write-Host "[$(Get-Date -Format 'HH:mm:ss')]  roster enabled" -ForegroundColor DarkCyan
            }
            elseif ($line -match "F11: roster recheck FAILED") {
                Write-Host "[$(Get-Date -Format 'HH:mm:ss')]  roster recheck FAILED" -ForegroundColor DarkYellow
            }
        }
    }

    Start-Sleep -Milliseconds 250
}

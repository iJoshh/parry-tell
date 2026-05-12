# launch-monitor.ps1 -- capture a timeline of what happens during an Elden Ring launch
#
# Run BEFORE you double-click the launcher. It will:
#   1. Snapshot the current process list as a baseline
#   2. Start an ETW trace capturing process spawn + DLL load events
#   3. Poll every 500ms for new eldenring.exe-tree processes and log them
#   4. When eldenring.exe goes away (or the user hits Ctrl-C), stop the trace
#      and emit a CSV timeline next to this script.
#
# Output:
#   C:\Projects\elden-ring\launch-timeline-YYYYMMDD-HHMMSS.csv     (process timeline)
#   C:\Projects\elden-ring\launch-trace-YYYYMMDD-HHMMSS.etl       (raw ETW dump)
#   C:\Projects\elden-ring\launch-dlls-YYYYMMDD-HHMMSS.csv        (DLL load timeline,
#                                                                  extracted from .etl)
#
# Stop conditions:
#   - Ctrl-C in the PowerShell window
#   - eldenring.exe has been running for >=60s then disappears (game closed)
#   - 30-minute hard cap (safety guard against accidentally leaving it running)
#
# This script must be launched from an ELEVATED PowerShell prompt (ETW kernel
# providers require admin). The station-ssh helper runs as the "claude" user
# which is non-admin, so Josh starts this manually from his own elevated shell.

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

$OutDir = 'C:\Projects\elden-ring'
if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir | Out-Null
}

$Stamp = (Get-Date).ToString('yyyyMMdd-HHmmss')
$TimelinePath = Join-Path $OutDir "launch-timeline-$Stamp.csv"
$EtlPath      = Join-Path $OutDir "launch-trace-$Stamp.etl"
$DllPath      = Join-Path $OutDir "launch-dlls-$Stamp.csv"
$SessionName  = "ParryTellLaunchTrace"

Write-Host "Output timeline: $TimelinePath"
Write-Host "Output ETW dump: $EtlPath"
Write-Host "Session name   : $SessionName"
Write-Host ""

# ---------------------------------------------------------------------------
# Admin check
# ---------------------------------------------------------------------------

$IsAdmin = ([Security.Principal.WindowsPrincipal] `
            [Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $IsAdmin) {
    Write-Host "ERROR: must run from an ELEVATED PowerShell prompt." -ForegroundColor Red
    Write-Host "       Kernel ETW providers (process spawn, image load) require admin." -ForegroundColor Red
    Write-Host ""
    Write-Host "       Right-click PowerShell -> Run as Administrator, then:" -ForegroundColor Yellow
    Write-Host "           cd C:\Projects\elden-ring" -ForegroundColor Yellow
    Write-Host "           powershell -ExecutionPolicy Bypass -File .\launch-monitor.ps1" -ForegroundColor Yellow
    exit 1
}

# ---------------------------------------------------------------------------
# Clean up any stale session from a previous run
# ---------------------------------------------------------------------------

$Existing = logman query $SessionName 2>$null
if ($LASTEXITCODE -eq 0) {
    Write-Host "Stopping stale ETW session '$SessionName'..."
    logman stop $SessionName -ets 2>&1 | Out-Null
}

# ---------------------------------------------------------------------------
# Start ETW trace
#   - Microsoft-Windows-Kernel-Process gives us process spawn/exit + image loads
#   - We use the Event Trace Session ("-ets") mode so we don't need to register
#     a real-time data collector -- the .etl is written directly.
# ---------------------------------------------------------------------------

Write-Host "Starting ETW trace..."

# 0x10 = WINEVENT_KEYWORD_PROCESS, 0x40 = WINEVENT_KEYWORD_IMAGE
# Together they give us process spawn/exit AND every DLL load event.
$StartResult = logman start $SessionName `
    -p "Microsoft-Windows-Kernel-Process" 0x50 0xff `
    -o $EtlPath `
    -ets 2>&1

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: failed to start ETW trace:" -ForegroundColor Red
    $StartResult | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    exit 1
}

Write-Host "ETW trace running."
Write-Host ""

# ---------------------------------------------------------------------------
# Process snapshot loop
# ---------------------------------------------------------------------------

$LaunchStart = Get-Date
$LastEldenStart = $null      # time we first saw eldenring.exe
$LastEldenEnd   = $null      # time eldenring.exe disappeared after launching
$HardCapMinutes = 30

# Process names we explicitly care about (anything new under these gets logged).
# Anything else launched during the window is also captured but tagged 'other'.
$KeyProcesses = @(
    'eldenring.exe',
    'eldenring_x64.exe',
    'start_protected_game.exe',
    'modengine2_launcher.exe',
    'me3_launcher.exe',
    'launchmod_eldenring.bat',
    'EldenRingSeamlessCoop.exe',
    'EasyAntiCheat.exe',
    'EasyAntiCheat_x64.exe',
    'EpicOnlineServicesUserHelper.exe',
    'EOSOverlayRenderer-Win64-Shipping.exe',
    'steam.exe',
    'steamwebhelper.exe'
)

# Initial baseline so we only log NEW processes
$Baseline = @{}
Get-Process | ForEach-Object { $Baseline[$_.Id] = $true }

# Open timeline CSV with header
"timestamp_iso,ms_since_start,event,process_name,pid,parent_pid,start_time,extra" |
    Out-File -FilePath $TimelinePath -Encoding ASCII

# CSV-safe quote: double up internal quotes and wrap in quotes if the field
# contains a comma, double-quote, or newline. Empty strings stay empty.
function Quote-CsvField {
    param([string]$Field)
    if ([string]::IsNullOrEmpty($Field)) { return '' }
    if ($Field -match '[",\r\n]') {
        return '"' + ($Field -replace '"', '""') + '"'
    }
    return $Field
}

function Write-Event {
    param([string]$EventName, [hashtable]$Data)
    $now = Get-Date
    $msSinceStart = [int]($now - $script:LaunchStart).TotalMilliseconds
    $iso = $now.ToString('yyyy-MM-ddTHH:mm:ss.fff')
    $fields = @(
        $iso,
        "$msSinceStart",
        $EventName,
        ($Data.process_name -as [string]),
        ($Data.pid          -as [string]),
        ($Data.parent_pid   -as [string]),
        ($Data.start_time   -as [string]),
        ($Data.extra        -as [string])
    )
    $escaped = $fields | ForEach-Object { Quote-CsvField $_ }
    $line = $escaped -join ','
    Add-Content -Path $TimelinePath -Value $line
    Write-Host "  [$([int]$msSinceStart) ms] $EventName : $($Data.process_name) (pid=$($Data.pid))"
}

Write-Event 'monitor_start' @{ extra = "snapshot_baseline_pids=$($Baseline.Count)" }

Write-Host "Monitoring. Launch the game now. Ctrl-C when done (or wait for game to close)."
Write-Host ""

# Hook Ctrl-C so we still stop the ETW session and flush the CSV.
$CtrlC = $false
$null = Register-EngineEvent -SourceIdentifier ConsoleCancelEventHandler -Action {
    $script:CtrlC = $true
} -ErrorAction SilentlyContinue

# Older PS versions: also trap Ctrl-C via [Console]::TreatControlCAsInput
[Console]::TreatControlCAsInput = $true

try {
    while ($true) {
        Start-Sleep -Milliseconds 500

        # Handle Ctrl-C via the input-read trick (since we set TreatControlCAsInput)
        if ([Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            if ($key.Modifiers -eq 'Control' -and $key.Key -eq 'C') {
                Write-Host ""
                Write-Host "Ctrl-C: stopping..."
                $CtrlC = $true
            }
        }

        # Hard cap
        $elapsedMin = ((Get-Date) - $LaunchStart).TotalMinutes
        if ($elapsedMin -gt $HardCapMinutes) {
            Write-Host ""
            Write-Host "Hard cap of $HardCapMinutes minutes reached -- stopping."
            break
        }

        # Poll process list
        $Current = Get-Process -ErrorAction SilentlyContinue

        # Detect new processes
        foreach ($p in $Current) {
            if (-not $Baseline.ContainsKey($p.Id)) {
                $Baseline[$p.Id] = $true
                $startTime = $null
                try { $startTime = $p.StartTime.ToString('o') } catch { $startTime = 'unknown' }
                $parentPid = $null
                try {
                    $parentPid = (Get-CimInstance Win32_Process -Filter "ProcessId=$($p.Id)" `
                                  -ErrorAction SilentlyContinue).ParentProcessId
                } catch { }

                $isKey = $KeyProcesses -contains "$($p.ProcessName).exe"
                $eventName = if ($isKey) { 'process_spawn_key' } else { 'process_spawn_other' }

                Write-Event $eventName @{
                    process_name = "$($p.ProcessName).exe"
                    pid          = $p.Id
                    parent_pid   = $parentPid
                    start_time   = $startTime
                }

                if ($p.ProcessName -eq 'eldenring') {
                    if (-not $LastEldenStart) { $script:LastEldenStart = Get-Date }
                }
            }
        }

        # Detect eldenring.exe exit (only ONCE, the first time we see it gone
        # after having seen it running). Without the $LastEldenEnd guard, this
        # block would emit an eldenring_exit row every 500ms and the >=60s
        # auto-stop branch would always eventually fire (uptime is recomputed
        # against `Get-Date` each iteration). Latch it.
        if ($LastEldenStart -and -not $LastEldenEnd) {
            $elden = $Current | Where-Object { $_.ProcessName -eq 'eldenring' }
            if (-not $elden) {
                $script:LastEldenEnd = Get-Date
                $uptime = ($LastEldenEnd - $LastEldenStart).TotalSeconds
                Write-Event 'eldenring_exit' @{
                    process_name = 'eldenring.exe'
                    extra        = "uptime_s=$([int]$uptime)"
                }
                if ($uptime -ge 60) {
                    Write-Host ""
                    Write-Host "eldenring.exe exited after running >=60s -- auto-stopping monitor."
                    break
                }
                # If uptime < 60s, fall through and keep monitoring in case the
                # launcher re-spawns eldenring.exe (Mod Engine 2 sometimes does
                # this after EAC handshake failures).
            }
        }

        if ($CtrlC) { break }
    }
}
finally {
    [Console]::TreatControlCAsInput = $false

    Write-Host ""
    Write-Host "Stopping ETW trace..."
    logman stop $SessionName -ets 2>&1 | Out-Null
    Write-Host "ETW trace stopped."

    # Try to extract DLL load events from the ETL into a CSV. This step is
    # best-effort -- if tracerpt isn't available or the .etl is too large,
    # we skip and tell the user where the raw file is.
    if (Test-Path $EtlPath) {
        Write-Host "Extracting DLL load events from .etl (tracerpt)..."
        $TracerptArgs = @(
            $EtlPath,
            '-o', $DllPath,
            '-of', 'CSV',
            '-y'  # overwrite
        )
        & tracerpt.exe @TracerptArgs 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0 -and (Test-Path $DllPath)) {
            $dllCount = (Get-Content $DllPath | Measure-Object -Line).Lines - 1
            Write-Host "DLL load CSV: $DllPath ($dllCount events)"
        } else {
            Write-Host "tracerpt failed or produced no output -- raw .etl preserved." -ForegroundColor Yellow
        }
    }

    Write-Host ""
    Write-Host "Outputs:"
    Write-Host "  $TimelinePath"
    Write-Host "  $EtlPath"
    if (Test-Path $DllPath) {
        Write-Host "  $DllPath"
    }
    Write-Host ""
    Write-Host "Send these paths to Claude for analysis."
}

param([int[]]$ClockHz = @(50000000, 100000000, 125000000, 200000000))
$ErrorActionPreference = 'Stop'
$ipRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$simDir = Join-Path $ipRoot 'build\sim'
New-Item -ItemType Directory -Force -Path $simDir | Out-Null
Push-Location $simDir
try {
    & xvlog --sv (Join-Path $ipRoot 'hdl\board_ctrl_axi.v') (Join-Path $ipRoot 'hdl\board_ctrl_ws2812.v') (Join-Path $ipRoot 'hdl\board_ctrl_pwm.v') (Join-Path $ipRoot 'tb\tb_board_ctrl_axi.sv')
    if ($LASTEXITCODE -ne 0) { throw 'xvlog failed' }
    foreach ($hz in $ClockHz) {
        $snapshot = "board_ctrl_$hz"
        # An options file preserves '=' through Vivado's Windows batch wrappers.
        $options = "elab_$hz.args"
        @("tb_board_ctrl_axi", "-generic_top CLK_HZ=$hz", "-s $snapshot", '-debug typical') | Set-Content -LiteralPath $options -Encoding Ascii
        & xelab -f $options
        if ($LASTEXITCODE -ne 0) { throw "xelab failed at $hz Hz" }
        $log = "sim_$hz.log"
        & xsim $snapshot -runall -log $log
        if ($LASTEXITCODE -ne 0 -or !(Select-String -LiteralPath $log -SimpleMatch "ALL_TESTS_PASSED CLK_HZ=$hz" -Quiet)) {
            throw "Simulation failed at $hz Hz; inspect $simDir\$log"
        }
    }
} finally { Pop-Location }

param(
    [ValidateSet("Install", "Gui", "Cli", "Mcp")][string]$Mode = "Install",
    [switch]$Physical,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$ClientArgs
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$venv = Join-Path $root ".venv"
$python = Join-Path $venv "Scripts\python.exe"
if ($Mode -eq "Install") {
    Get-Content (Join-Path $root "SHA256SUMS") | ForEach-Object {
        if ($_ -notmatch '^([0-9a-f]{64})  (.+)$') { throw "Invalid manifest line" }
        $expected = $Matches[1]
        $path = Join-Path $root $Matches[2]
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLower() -ne $expected) {
            throw "Checksum failed: $path"
        }
    }
    & py -3.12 -m venv $venv
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    $wheelhouse = Join-Path $root "wheels"
    $requirements = Join-Path $root "requirements.lock"
    & $python -m pip install --no-index --only-binary=:all: --require-hashes --find-links $wheelhouse -r $requirements
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    $sdk = Get-Item -LiteralPath (Join-Path $wheelhouse "usb2xxx-0.1.0-py3-none-any.whl")
    & $python -m pip install --no-index --no-deps $sdk.FullName
    exit $LASTEXITCODE
}
$env:USB2XXX_MOCK_DEVICE = Join-Path $root "mock-device.exe"
$env:USB2XXX_LIBUSB = Join-Path $root "bin\libusb-1.0.dll"
$entry = @{ Gui = "gui"; Cli = "cli"; Mcp = "mcp_server" }[$Mode]
if (!$Physical) { $ClientArgs = @("--mock") + $ClientArgs }
& $python -m "usb2xxx.$entry" @ClientArgs
exit $LASTEXITCODE

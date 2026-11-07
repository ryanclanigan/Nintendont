param([string]$Compiler = 'gcc', [string]$CompilerInclude = '')
$ErrorActionPreference = 'Stop'
$kernelRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$testOutput = Join-Path ([System.IO.Path]::GetTempPath()) ('asix-socket-' + [guid]::NewGuid() + '.exe')
$argsList = @('-DASIX_HOST_TEST')
$argsList += '-I' + (Join-Path $PSScriptRoot 'stubs')
if ($CompilerInclude) { $argsList += '-I' + $CompilerInclude }
$argsList += '-I' + (Join-Path $kernelRoot 'asix/port')
$argsList += '-I' + (Join-Path $kernelRoot 'asix/lwip/src/include')
if ([System.IO.Path]::GetFileNameWithoutExtension($Compiler) -eq 'tcc') {
    $argsList += '-I' + $kernelRoot
} else {
    $argsList += '-idirafter'
    $argsList += $kernelRoot
}
$sources = @((Join-Path $PSScriptRoot 'socket_test.c'))
$sources += Get-ChildItem (Join-Path $kernelRoot 'asix/lwip/src/core/*.c'), (Join-Path $kernelRoot 'asix/lwip/src/core/ipv4/*.c') | ForEach-Object FullName
$sources += Join-Path $kernelRoot 'asix/lwip/src/netif/ethernet.c'
try {
    & $Compiler @argsList '-o' $testOutput @sources
    if ($LASTEXITCODE -ne 0) { throw 'Socket test compilation failed.' }
    & $testOutput
    if ($LASTEXITCODE -ne 0) { throw 'Socket test failed.' }
} finally {
    if (Test-Path -LiteralPath $testOutput) { Remove-Item -LiteralPath $testOutput }
}

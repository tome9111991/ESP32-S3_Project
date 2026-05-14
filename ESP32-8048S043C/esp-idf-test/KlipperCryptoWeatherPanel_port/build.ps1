param([Parameter(ValueFromRemainingArguments=$true)]$IdfArgs)

$BuildDir = "C:\espbuild\kwp_8048"
$Port = "COM4"

if (-not $IdfArgs -or $IdfArgs.Count -eq 0) {
    $IdfArgs = @("build")
}

& idf.py -B $BuildDir -p $Port @IdfArgs
exit $LASTEXITCODE

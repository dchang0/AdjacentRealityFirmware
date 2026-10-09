<#
check-invariants.ps1 - static checks for the bug classes that shipped broken in 1.0.3.

Run:  powershell -ExecutionPolicy Bypass -File .\check-invariants.ps1 [-Elf|-Hex <path>]

CHECK 1 - UNBOUNDED while LOOPS                                    (FAIL)
  Everything is one translation unit included from main.cpp, so a while-loop
  in any source file is reachable from loop() unless it is only called from
  setup(). A while whose condition has no counter and no relational test can
  spin forever: loop() stops, buttons go dead and playDrums() stops (it only
  runs from loop()), while the Timer0 and WDT ISRs keep running. The unit
  makes noise and does nothing else - exactly the 1.0.3 lockup.  The only
  legal long-running waits are hardware polls with a guard counter.

CHECK 2 - RANGE-CLAIM COMMENTS                                      (REVIEW)
  A comment asserting a numeric range next to shift/arithmetic is a promise
  the code has to keep.  "octaveOffset = (lightLevel >> 7);  // 0 or 1"
  shipped as 0..4 because nobody re-did the arithmetic, and the result was a
  ~10 kHz whine.  These are printed for review rather than failed, because a
  comment may describe intent instead of the result.

CHECK 3 - FLASH BUDGET                                              (FAIL)
  ATtiny85 has 8192 bytes.  Summed straight from the Intel HEX data records
  (= .text + .data), so no toolchain is needed.

Exit code 1 if any FAIL check trips, 0 otherwise.
#>

param(
    [string]$Hex,
    [string]$Root = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'
$script:failCount = 0
$script:reviewCount = 0

$exclude = '\\(Release|Debug|\.vs|node_modules)\\'
$sources = Get-ChildItem -Path $Root -Recurse -File -Include *.cpp, *.h |
    Where-Object { $_.FullName -notmatch $exclude } |
    Sort-Object FullName

if (-not $sources) { throw "No source files found under $Root" }
if (-not $Hex)     { $Hex = Join-Path $Root 'Release\AdjacentReality.hex' }

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

# Blank out comments while preserving every newline, so line numbers stay
# accurate for the loop scan and CHECK 2 still sees the original text.
function Remove-Comments([string]$text) {
    $text = [regex]::Replace($text, '(?s)/\*.*?\*/', {
        param($m)
        "`n" * [regex]::Matches($m.Value, "`n").Count
    })
    return [regex]::Replace($text, '//[^\r\n]*', '')
}

function Get-WhileLoops([string]$code, [string]$path) {
    $found = @()
    $offset = 0
    while ($true) {
        $m = [regex]::Match($code.Substring($offset), '\bwhile\s*\(')
        if (-not $m.Success) { break }

        $open   = $offset + $m.Index + $m.Length   # just past the '('
        $depth  = 1
        $i      = $open
        while ($i -lt $code.Length -and $depth -gt 0) {
            if ($code[$i] -eq '(')     { $depth++ }
            elseif ($code[$i] -eq ')') { $depth-- }
            $i++
        }
        if ($depth -ne 0) { break }   # unbalanced - bail out cleanly

        $cond = $code.Substring($open, $i - 1 - $open)
        $body = $code.Substring($i, [Math]::Min(600, $code.Length - $i))
        $line = ([regex]::Matches($code.Substring(0, $offset + $m.Index), "`n").Count) + 1

        $found += [pscustomobject]@{ Path = $path; Line = $line; Cond = $cond; Body = $body }
        $offset = $open
    }
    return $found
}

# A while-loop is bounded if it tests a counter, decrements one in its
# condition, or increments/decrements something its condition reads.
function Test-Bounded([pscustomobject]$loop) {
    $cond = $loop.Cond

    $withoutShifts = $cond -replace '<<', '' -replace '>>', ''
    if ($withoutShifts -match '[<>]')  { return $true }   # relational test
    if ($cond -match '\+\+|--')        { return $true }   # e.g. while (ms--)

    $ids = [regex]::Matches($cond, '[A-Za-z_][A-Za-z0-9_]*') |
        ForEach-Object { $_.Value } | Select-Object -Unique
    foreach ($id in $ids) {
        $e = [regex]::Escape($id)
        if ($loop.Body -match "\b$e\s*(?:\+\+|--)") { return $true }
        if ($loop.Body -match "(?:\+\+|--)\s*\b$e\b") { return $true }
    }
    return $false
}

function Report-Section([string]$title) { Write-Host ''; Write-Host "== $title ==" }

# ---------------------------------------------------------------------------
# CHECK 1 - unbounded while loops
# ---------------------------------------------------------------------------
Report-Section 'CHECK 1: unbounded while loops'
$allLoops = @()
foreach ($src in $sources) {
    $code = Remove-Comments ([IO.File]::ReadAllText($src.FullName))
    $allLoops += Get-WhileLoops $code $src.FullName
}

if ($allLoops.Count -eq 0) {
    Write-Host '  none found' -ForegroundColor Green
}
foreach ($loop in $allLoops) {
    $rel = $loop.Path.Substring($Root.Length).TrimStart('\')
    if (Test-Bounded $loop) {
        Write-Host ("  ok    {0}:{1}" -f $rel, $loop.Line) -ForegroundColor DarkGray
    } else {
        $script:failCount++
        Write-Host ("  FAIL  {0}:{1}  while ({2})" -f $rel, $loop.Line, $loop.Cond.Trim()) -ForegroundColor Red
        Write-Host '        no counter and no comparison - this can spin forever inside loop()' -ForegroundColor Red
    }
}

# ---------------------------------------------------------------------------
# CHECK 2 - range-claim comments next to arithmetic
# ---------------------------------------------------------------------------
Report-Section 'CHECK 2: range-claim comments on arithmetic (review)'
$rangeClaim = '\d\s*(?:or|to|thru|through|-|–|—)\s*\d'
foreach ($src in $sources) {
    $rel = $src.FullName.Substring($Root.Length).TrimStart('\')
    $lines = [IO.File]::ReadAllLines($src.FullName)
    for ($n = 0; $n -lt $lines.Count; $n++) {
        $line = $lines[$n]
        $idx  = $line.IndexOf('//')
        if ($idx -lt 0) { continue }

        $code = $line.Substring(0, $idx)
        $cmt  = $line.Substring($idx + 2)

        # A range claim, or any digit on a line doing a RIGHT shift - a
        # right shift is where a claimed small result silently becomes a
        # large one (lightLevel >> 7 is 0..4, not the "0 or 1" it said).
        # Left shifts of constants into register masks are not interesting.
        $isShift   = $code -match '>>'
        $isClaim   = $cmt -match $rangeClaim
        $isArith   = $code -match 'random\(|[*/]|<<|>>'
        if (-not ($isArith -and ($isClaim -or ($isShift -and $cmt -match '\d')))) { continue }

        $script:reviewCount++
        Write-Host ("  {0}:{1}" -f $rel, ($n + 1)) -ForegroundColor Yellow
        Write-Host ("        {0}" -f $line.Trim()) -ForegroundColor Yellow
    }
}
if ($reviewCount -eq 0) { Write-Host '  none found' -ForegroundColor Green }

# ---------------------------------------------------------------------------
# CHECK 3 - flash budget
# ---------------------------------------------------------------------------
Report-Section 'CHECK 3: flash budget'
$FLASH_LIMIT = 8192
if (-not (Test-Path $Hex)) {
    $script:failCount++
    Write-Host "  FAIL  hex not found: $Hex" -ForegroundColor Red
} else {
    $used = 0
    foreach ($raw in [IO.File]::ReadAllLines($Hex)) {
        $l = $raw.Trim()
        if (-not $l.StartsWith(':')) { continue }
        $n = [Convert]::ToByte($l.Substring(1, 2), 16)
        $t = [Convert]::ToByte($l.Substring(7, 2), 16)
        if ($t -eq 0) { $used += $n }   # type 00 = data record
    }
    $free = $FLASH_LIMIT - $used
    $colour = if ($used -gt $FLASH_LIMIT) { 'Red' } elseif ($free -lt 64) { 'Yellow' } else { 'Green' }
    Write-Host ("  {0}/{1} flash, {2} free  ({3})" -f $used, $FLASH_LIMIT, $free, (Split-Path $Hex -Leaf)) -ForegroundColor $colour
    if ($used -gt $FLASH_LIMIT) {
        $script:failCount++
        Write-Host "  FAIL  over the ATtiny85 limit by $($used - $FLASH_LIMIT) bytes" -ForegroundColor Red
    }
}

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
Write-Host ''
if ($failCount -gt 0) {
    Write-Host ("RESULT: FAIL - {0} blocking issue(s), {1} comment(s) to review" -f $failCount, $reviewCount) -ForegroundColor Red
    exit 1
}
Write-Host ("RESULT: PASS - 0 blocking issues, {0} comment(s) to review" -f $reviewCount) -ForegroundColor Green
if ($reviewCount -gt 0) { Write-Host '         (review lines are advisory - confirm the arithmetic matches the claim)' -ForegroundColor DarkGray }
exit 0

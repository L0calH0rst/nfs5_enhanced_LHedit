# Build a complete NFS5 Enhanced with Essentials V2 Integration release.
[CmdletBinding()]
param(
    [string]$RepositoryPath = (Split-Path -Parent $PSScriptRoot),
    [string]$BuildDirectory = '',
    [Parameter(Mandatory = $true)]
    [string]$AsiLoaderPath,
    [string]$OutputDirectory = (Join-Path (Join-Path $env:USERPROFILE 'Documents') 'NFS5_EssentialsV2_Release_Output')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Need-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file not found: $Path"
    }
}

function Need-X86-PE([string]$Path) {
    Need-File $Path
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 256 -or $bytes[0] -ne 77 -or $bytes[1] -ne 90) {
        throw "Not an executable PE image: $Path"
    }
    $offset = [System.BitConverter]::ToInt32($bytes, 0x3c)
    if ($offset -lt 64 -or ($offset + 6) -ge $bytes.Length -or
        $bytes[$offset] -ne 80 -or $bytes[$offset + 1] -ne 69) {
        throw "Invalid PE header: $Path"
    }
    $machine = [System.BitConverter]::ToUInt16($bytes, $offset + 4)
    if ($machine -ne 0x014c) {
        throw ("Expected 32-bit x86 PE file, got machine 0x{0:X4}: {1}" -f $machine, $Path)
    }
}

$repo = (Resolve-Path -LiteralPath $RepositoryPath).Path
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $repo 'build_redirect_vs2026'
}
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$loader = (Resolve-Path -LiteralPath $AsiLoaderPath).Path

# Absolutely prohibit writing into the permanent game directory.
$protectedGame = 'C:\Games\NFS5USA'
$canonicalOut = [System.IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
if ($canonicalOut.Equals($protectedGame, [StringComparison]::OrdinalIgnoreCase) -or
    $canonicalOut.StartsWith($protectedGame + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $canonicalOut.Equals($repo, [StringComparison]::OrdinalIgnoreCase) -or
    $canonicalOut.StartsWith($repo.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Output folder cannot be the permanent game directory or inside the Git repository.'
}

$manifestPath = Join-Path $repo 'docs\ESSENTIALS_V2_MANIFEST.csv'
$fixRoot = Join-Path $repo 'GameplayFixes'
$ini = Join-Path $repo 'config\nfs5_enhanced.ini'
Need-File $manifestPath
Need-File $ini
if (-not (Test-Path -LiteralPath $fixRoot -PathType Container)) { throw 'GameplayFixes is missing.' }

$rows = @(Import-Csv -LiteralPath $manifestPath)
$assets = @(Get-ChildItem -LiteralPath $fixRoot -File -Recurse)
if ($rows.Count -ne 102 -or $assets.Count -ne 102) {
    throw "Expected 102 manifest entries and 102 files; found $($rows.Count) / $($assets.Count)."
}
$seen = @{}
foreach ($row in $rows) {
    if ($row.Relative -notmatch '^(FEData|GameData)\\' -or
        $row.Relative.Contains('..') -or $row.Relative.Contains(':') -or
        $seen.ContainsKey($row.Relative.ToLowerInvariant())) {
        throw "Invalid or duplicate manifest path: $($row.Relative)"
    }
    $seen[$row.Relative.ToLowerInvariant()] = $true
    $asset = Join-Path $fixRoot $row.Relative
    Need-File $asset
    $actualHash = (Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash
    $actualSize = (Get-Item -LiteralPath $asset).Length
    if ($actualHash -ine $row.SHA256 -or $actualSize -ne [long]$row.SizeBytes) {
        throw "Asset validation failed: $($row.Relative)"
    }
}
$bad = @($assets | Where-Object { $_.Extension -ieq '.cop' -or $_.Name -match '^monaco' })
if ($bad.Count -ne 0) { throw 'Monaco or COP files unexpectedly found.' }
Write-Host 'Verified 102 Essentials V2 files (sizes + SHA-256); no Monaco assets.' -ForegroundColor Green

$iniText = [System.IO.File]::ReadAllText($ini)
if ([regex]::Matches($iniText, '(?m)^GameplayRedirect\s*=\s*1\s*\r?$').Count -ne 1 -or
    [regex]::Matches($iniText, '(?m)^GameplayRedirectTrace\s*=\s*0\s*\r?$').Count -ne 1) {
    throw 'INI must contain exactly one GameplayRedirect = 1 and one GameplayRedirectTrace = 0.'
}

# Rebuild to avoid packaging an older ASI after source edits.
& cmake --build $build --config Release --target nfs5_enhanced dx11z
if ($LASTEXITCODE -ne 0) { throw "CMake build failed ($LASTEXITCODE)." }

$asi = Join-Path $build 'Release\nfs5_enhanced.asi'
$sdl = Join-Path $build 'Release\SDL3.dll'
$dx11 = Join-Path $build 'drivers\dx11\Release\dx11z.dll'
foreach ($path in @($asi, $sdl, $dx11, $loader)) { Need-X86-PE $path }

$name = 'NFS5 Enhanced with Essentials V2 Integration'
$target = Join-Path $canonicalOut $name
$zip = Join-Path $canonicalOut ($name + '.zip')
if ((Test-Path -LiteralPath $target) -or (Test-Path -LiteralPath $zip)) {
    throw 'Output folder or ZIP already exists. Delete or rename it manually only after reviewing its contents.'
}
New-Item -ItemType Directory -Path $target -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $target 'Drivers') -Force | Out-Null
try {
    Copy-Item -LiteralPath $asi -Destination (Join-Path $target 'nfs5_enhanced.asi') -ErrorAction Stop
    Copy-Item -LiteralPath $sdl -Destination (Join-Path $target 'SDL3.dll') -ErrorAction Stop
    Copy-Item -LiteralPath $dx11 -Destination (Join-Path $target 'Drivers\dx11z.dll') -ErrorAction Stop
    Copy-Item -LiteralPath $loader -Destination (Join-Path $target 'dinput.dll') -ErrorAction Stop
    Copy-Item -LiteralPath $ini -Destination (Join-Path $target 'nfs5_enhanced.ini') -ErrorAction Stop
    Copy-Item -LiteralPath $fixRoot -Destination $target -Recurse -ErrorAction Stop
    Copy-Item -LiteralPath (Join-Path $repo 'docs') -Destination $target -Recurse -ErrorAction Stop

    $readme = @'
NFS5 ENHANCED WITH ESSENTIALS V2 INTEGRATION
===========================================


REQUIREMENTS
- Your legally installed NFS5 Porsche Unleashed USA, patched to v3.5.
- Windows 10/11. Do not replace Porsche.exe.

INSTALLATION
1. Make a separate clean copy of the game folder for testing.
2. Copy the contents of this ZIP into that game's folder (next to Porsche.exe),
   preserving the Drivers and GameplayFixes directories.
3. Disable SilentPatchNFS90s.asi if it is present (avoid overlapping hooks).
4. Run the game with the Direct3D 11 Thrash driver as in NFS5 Enhanced.

The included nfs5_enhanced.ini already has GameplayRedirect = 1 and
GameplayRedirectTrace = 0. All 102 Essentials files load from GameplayFixes.
Original FEData and GameData are not replaced.

Disable the overlay: set GameplayRedirect = 0 in nfs5_enhanced.ini.

CREDITS
- ND4SPD Racer: original NFS5 Essentials V2 mod.
- iArtorias and contributors: NFS5 Enhanced.
- ThirteenAG: Ultimate ASI Loader, MIT License (see accompanying license).
- Porsche and EA assets belong to their respective rights holders.

KNOWN LIMITATIONS
- Porsche 959 and 928 GTS Quick Race were verified in earlier local tests.
- Full 102-file gameplay regression and GT2/GT3 tests remain pending.
- This is a patch for an existing game, not a standalone game distribution.
'@
    [System.IO.File]::WriteAllText((Join-Path $target 'README_INSTALL.txt'), $readme,
        [System.Text.Encoding]::ASCII)

    $license = @'
MIT License

Copyright (c) 2023 ThirteenAG

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
'@
    [System.IO.File]::WriteAllText((Join-Path $target 'LICENSE_Ultimate_ASI_Loader.txt'),
        $license, [System.Text.Encoding]::ASCII)

    # Update the release copy of the integration notes to match the release INI.
    $doc = Join-Path $target 'docs\ESSENTIALS_V2_INTEGRATION.md'
    $docText = [System.IO.File]::ReadAllText($doc)
    $docText = $docText.Replace('default: disabled', 'default in this distribution: enabled')
    $docText = $docText.Replace('`GameplayRedirect = 0` and `GameplayRedirectTrace = 0`',
        '`GameplayRedirect = 1` and `GameplayRedirectTrace = 0`')
    $docText = $docText.Replace('The default `GameplayRedirect = 0` retains original NFS5 Enhanced behavior.',
        'This distribution enables the overlay by default (`GameplayRedirect = 1`). Set it to `0` to disable the overlay.')
    [System.IO.File]::WriteAllText($doc, $docText, [System.Text.UTF8Encoding]::new($false))

    $copied = @(Get-ChildItem -LiteralPath (Join-Path $target 'GameplayFixes') -File -Recurse)
    if ($copied.Count -ne 102) { throw 'Release payload lost Essentials files.' }

    # Write standards-compliant ZIP entry names with forward slashes, including
    # when building on Windows PowerShell 5.1 (Compress-Archive may store '\').
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::Open($zip, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in @(Get-ChildItem -LiteralPath $target -Recurse -File)) {
            $relative = $file.FullName.Substring($target.Length + 1).Replace('\', '/')
            [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $archive, $file.FullName, $relative, [System.IO.Compression.CompressionLevel]::Optimal)
        }
    } finally { $archive.Dispose() }

    $archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $entryNames = @($archive.Entries | ForEach-Object { $_.FullName })
        if (@($entryNames | Where-Object { $_.Contains('\') }).Count -ne 0) {
            throw 'ZIP entries use backslashes instead of forward slashes.'
        }
        $assetEntries = @($entryNames | Where-Object {
            $_.StartsWith('GameplayFixes/', [System.StringComparison]::OrdinalIgnoreCase) -and
            -not $_.EndsWith('/')
        })
        if ($assetEntries.Count -ne 102) { throw "ZIP contains $($assetEntries.Count) Essentials assets, expected 102." }
        foreach ($required in @('nfs5_enhanced.asi', 'nfs5_enhanced.ini', 'dinput.dll', 'SDL3.dll', 'Drivers/dx11z.dll')) {
            if ($entryNames -notcontains $required) {
                throw "ZIP missing: $required"
            }
        }
    } finally { $archive.Dispose() }

    Write-Host 'RELEASE ZIP CREATED SUCCESSFULLY' -ForegroundColor Green
    Write-Host "ZIP: $zip"
    Write-Host "SHA256: $((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash)"
    Write-Host "Essentials assets: $($copied.Count), Monaco .cop: 0"
    Write-Host 'GameplayRedirect=1; GameplayRedirectTrace=0; ASI loader and DX11 included.'
} catch {
    Write-Warning 'Packaging failed. The output folder may be incomplete; inspect it manually.'
    throw
}

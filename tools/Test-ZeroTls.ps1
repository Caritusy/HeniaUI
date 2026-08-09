param(
    [Parameter(Mandatory = $true)]
    [string]$RepositoryRoot,

    [string]$ImagePath = ""
)

$ErrorActionPreference = "Stop"

$resolvedRepository = [System.IO.Path]::GetFullPath($RepositoryRoot)
$sourceExtensions = @(".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx")
$tlsPattern = '\bthread_local\b|__declspec\s*\(\s*thread\s*\)|\b_Thread_local\b|\b__thread\b'
$excludedPathPattern = '\\(?:\.git|\.tools|build(?:-[^\\]+)?|out|\.verify-bin|vcpkg_installed)\\'

$tlsMatches = Get-ChildItem -LiteralPath $resolvedRepository -Recurse -File |
    Where-Object {
        $sourceExtensions -contains $_.Extension.ToLowerInvariant() -and
        $_.FullName -notmatch $excludedPathPattern
    } |
    Select-String -Pattern $tlsPattern

if ($tlsMatches) {
    $locations = $tlsMatches | ForEach-Object {
        "$($_.Path):$($_.LineNumber): $($_.Line.Trim())"
    }
    throw "Explicit TLS storage is forbidden:`n$($locations -join [Environment]::NewLine)"
}

if ([string]::IsNullOrWhiteSpace($ImagePath)) {
    Write-Host "Zero-TLS source verification passed: $resolvedRepository"
    exit 0
}

function Assert-Range {
    param([int]$Offset, [int]$Length, [int]$Limit, [string]$Description)
    if ($Offset -lt 0 -or $Length -lt 0 -or $Offset -gt $Limit - $Length) {
        throw "$Description is outside the PE image."
    }
}

$resolvedImage = [System.IO.Path]::GetFullPath($ImagePath)
if (-not (Test-Path -LiteralPath $resolvedImage -PathType Leaf)) {
    throw "PE image was not found: $resolvedImage"
}

[byte[]]$image = [System.IO.File]::ReadAllBytes($resolvedImage)
try {
    Assert-Range 0 64 $image.Length "DOS header"
    if ($image[0] -ne 0x4D -or $image[1] -ne 0x5A) {
        throw "Image does not contain an MZ header: $resolvedImage"
    }
    $peOffset = [BitConverter]::ToInt32($image, 0x3C)
    Assert-Range $peOffset 24 $image.Length "PE header"
    if ([BitConverter]::ToUInt32($image, $peOffset) -ne 0x00004550) {
        throw "Image does not contain a PE signature: $resolvedImage"
    }

    $optionalHeaderSize = [BitConverter]::ToUInt16($image, $peOffset + 20)
    $optionalHeaderOffset = $peOffset + 24
    Assert-Range $optionalHeaderOffset $optionalHeaderSize $image.Length "Optional header"
    if ($optionalHeaderSize -lt 112 -or
        [BitConverter]::ToUInt16($image, $optionalHeaderOffset) -ne 0x020B) {
        throw "Image is not a valid PE32+ image: $resolvedImage"
    }

    $directoryCount = [BitConverter]::ToUInt32($image, $optionalHeaderOffset + 108)
    $maximumDirectoryCount = [Math]::Floor(($optionalHeaderSize - 112) / 8)
    if ($directoryCount -gt $maximumDirectoryCount) {
        throw "PE data-directory table is truncated: $resolvedImage"
    }
    if ($directoryCount -gt 9) {
        $tlsDirectoryOffset = $optionalHeaderOffset + 112 + 9 * 8
        $tlsRva = [BitConverter]::ToUInt32($image, $tlsDirectoryOffset)
        $tlsSize = [BitConverter]::ToUInt32($image, $tlsDirectoryOffset + 4)
        if ($tlsRva -ne 0 -or $tlsSize -ne 0) {
            throw ("PE static TLS directory must be empty: {0} (RVA=0x{1:X8}, Size=0x{2:X8})" -f `
                $resolvedImage, $tlsRva, $tlsSize)
        }
    }
}
finally {
    [Array]::Clear($image, 0, $image.Length)
}

Write-Host "Zero-TLS verification passed: $resolvedImage"

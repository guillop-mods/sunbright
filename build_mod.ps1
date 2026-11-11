$ErrorActionPreference = "Stop"

Write-Host "=== DynamicSunAttenuation SFSE - Compilador y Empaquetador ===" -ForegroundColor Cyan

# 1. Verificar VCPKG_ROOT (o VCPKG_INSTALLATION_ROOT)
if (-not $env:VCPKG_ROOT -and $env:VCPKG_INSTALLATION_ROOT) {
    $env:VCPKG_ROOT = $env:VCPKG_INSTALLATION_ROOT
}

if (-not $env:VCPKG_ROOT -or -not (Test-Path "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake")) {
    Write-Host "[ERROR] No se encontro VCPKG_ROOT valido." -ForegroundColor Red
    Write-Host "Instala vcpkg y define VCPKG_ROOT, por ejemplo:"
    Write-Host '  git clone https://github.com/microsoft/vcpkg.git C:\vcpkg'
    Write-Host '  C:\vcpkg\bootstrap-vcpkg.bat'
    Write-Host '  $env:VCPKG_ROOT = "C:\vcpkg"'
    exit 1
}

# 2. Localizar o clonar CommonLibSF si no existe localmente ni via CommonLibSFPath
$localClib = Join-Path $PSScriptRoot "extern\CommonLibSF\CMakeLists.txt"
$hasEnvClib = $env:CommonLibSFPath -and (Test-Path (Join-Path $env:CommonLibSFPath "CMakeLists.txt"))

if (-not (Test-Path $localClib) -and -not $hasEnvClib) {
    Write-Host "[INFO] Clonando libxse/commonlibsf en extern\CommonLibSF..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Force -Path (Join-Path $PSScriptRoot "extern") | Out-Null
    git clone --depth 1 https://github.com/libxse/commonlibsf.git (Join-Path $PSScriptRoot "extern\CommonLibSF")
}

# 3. Cargar entorno MSVC x64 (incluye cl.exe, cmake.exe y ninja.exe) si no esta en PATH
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsPath) {
            $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path $vcvars) {
                Write-Host "[INFO] Cargando entorno MSVC x64 desde $vsPath..." -ForegroundColor Yellow
                $envDump = cmd /c "`"$vcvars`" && set"
                foreach ($line in $envDump) {
                    if ($line -match "^(.*?)=(.*)$") {
                        [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
                    }
                }
            }
        }
    }
}

# 4. Configurar y compilar Release x64
Write-Host "[INFO] Configurando CMake (preset release)..." -ForegroundColor Cyan
cmake --preset release

Write-Host "[INFO] Compilando DynamicSunAttenuation.dll..." -ForegroundColor Cyan
cmake --build --preset release

# 5. Generar ZIP instalable para Mod Organizer 2 / Vortex / instalacion manual
$pkgRoot = Join-Path $PSScriptRoot "build\release\package"
$fomodDst = Join-Path $pkgRoot "fomod"
New-Item -ItemType Directory -Force -Path $fomodDst | Out-Null
Copy-Item -Force (Join-Path $PSScriptRoot "dist\fomod\info.xml") (Join-Path $fomodDst "info.xml")

$outZip = Join-Path $PSScriptRoot "DynamicSunAttenuation-SFSE-v1.1.0.zip"
if (Test-Path $outZip) { Remove-Item $outZip -Force }
Compress-Archive -Path "$pkgRoot\*" -DestinationPath $outZip -CompressionLevel Optimal

Write-Host ""
Write-Host "[LISTO] Mod empaquetado correctamente en:" -ForegroundColor Green
Write-Host "  $outZip" -ForegroundColor White
Write-Host "Contenido listo para Mod Organizer 2 / Vortex:"
Write-Host "  Data\SFSE\Plugins\DynamicSunAttenuation.dll"
Write-Host "  Data\SFSE\Plugins\DynamicSunAttenuation.ini"
Write-Host "  fomod\info.xml"

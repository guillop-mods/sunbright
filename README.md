# DynamicSunAttenuation (Plugin DLL para SFSE / Starfield)

Plugin nativo en C++23 para **Starfield Script Extender (SFSE)** y **CommonLibSF** que atenúa dinámicamente el brillo solar (*Sun Glare Alpha*, tamaño del disco solar y luz direccional `NiDirectionalLight`) según la distancia euclidiana entre `PlayerShip` y la estrella central del sistema.

---

## Requisitos previos para compilar en Windows

1. **Visual Studio 2022** con la carga de trabajo *Desarrollo para el escritorio con C++* (MSVC x64 + Windows 10/11 SDK).
2. **CMake >= 3.26** y **Ninja** (incluidos en Visual Studio 2022).
3. **vcpkg** con la variable de entorno `VCPKG_ROOT` apuntando al directorio de instalación de vcpkg.
4. *(Opcional)* Si ya tienes una copia local de `libxse/commonlibsf`, define la variable `CommonLibSFPath` apuntando a ella o colócala en `extern/CommonLibSF`. Si no existe, `build_mod.ps1` o CMake la descargarán automáticamente.

---

## Compilación y empaquetado en 1 paso (Windows PowerShell)

```powershell
# Opcional: si quieres que se copie automáticamente a tu carpeta de Starfield o MO2
$env:SFPath = "C:\Program Files (x86)\Steam\steamapps\common\Starfield"

# Si ya clonaste CommonLibSF en otra ruta:
# $env:CommonLibSFPath = "C:\dev\commonlibsf"

.\build_mod.ps1
```

Esto compila `DynamicSunAttenuation.dll` en modo **Release x64** y genera el archivo instalable:

```text
DynamicSunAttenuation-SFSE-v1.1.0.zip
├── Data/
│   └── SFSE/
│       └── Plugins/
│           ├── DynamicSunAttenuation.dll
│           └── DynamicSunAttenuation.ini
└── fomod/
    └── info.xml
```

Puedes arrastrar ese `.zip` directamente a **Mod Organizer 2** o **Vortex**, o extraer la carpeta `Data/` en la raíz de `Starfield`.

> **Alternativa sin instalar Visual Studio:** Sube esta carpeta a un repositorio de GitHub; el workflow `.github/workflows/build-sfse-mod.yml` compilará la DLL en `windows-2022` y publicará el paquete listo para instalar como artefacto descargable.

---

## Calibración inicial de `RE::Sky` (primer arranque en el juego)

Debido a que `CommonLibSF` aún no mapea `Sun.h` y `Sky.h` solo expone `pad[0xEB8]` y `windSpeed` (`0xEC4`), el plugin incluye un inspector RTTI MSVC x64 seguro:

1. Instala el mod (con los valores iniciales `0x0` en `[SkyOffsets]`) y entra en una celda espacial con tu nave.
2. El plugin volcará automáticamente (o al pulsar **`F9`**) los primeros **64 punteros** de `RE::Sky::pad[0xEB8]` junto con su nombre RTTI en:
   `Documents\My Games\Starfield\SFSE\Logs\DynamicSunAttenuation.log`
3. Busca las líneas marcadas con `<<< CANDIDATO SUN/LIGHT` (por ejemplo, `Sun`, `NiDirectionalLight`, `NiBillboardNode`) y anota sus offsets hexadecimales en `Data/SFSE/Plugins/DynamicSunAttenuation.ini` bajo `[SkyOffsets]`.
4. Pulsa **`F9`** de nuevo dentro del juego para recargar el `.ini` al instante o reinicia Starfield.

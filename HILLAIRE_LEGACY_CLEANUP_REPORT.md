# HILLAIRE LEGACY CLEANUP REPORT — COMPLETE REMOVAL

Data: 2026-09-15
Branch: `main` (working copy, non committato)
Task: cleanup esclusivo del vecchio sistema atmosferico. Nessun nuovo renderer, nessun tuning, nessuna modifica STARMAP, nessun porting.

---

## 1. Executive Summary

Il progetto Andromeda conteneva ancora un sistema atmosferico Hillaire completo ma inerte
(data model multi-planet + LUT renderer RDG + 4 global shader + 1 shared header + 9 CVars +
1 console command + 1 log category + shader directory mapping), residuo dei cicli
ATMOS / ZEPHYR / UNIFIED / Hillaire-WIP precedenti.

I sistemi ATMOS, ZEPHYR e UNIFIED glue risultavano **già assenti da `Source/`** all'inizio di
questa sessione (rimozione precedente del 2026-09-14, verificata via grep: zero match per
`AtmosphereManager`, `AtmosphereRenderer`, `AtmosphereSystem`, `ViewExtension`,
`PostOpaque`, `ENQUEUE_RENDER`, `Bruneton`, `Irradiance`, `AerialPerspective`).

In questa sessione è stato rimosso **l'intero Hillaire residuo**:

- 5 file C++ (2 header pubblici, 2 cpp privati, 1 header shader privato);
- 5 file shader (1 `.ush` + 4 `.usf`) + directory divenute vuote;
- 9 console variables `r.AndromedaHillaire.*` + 1 console command `r.AndromedaHillaire.Validate`;
- 1 log category `LogAndromedaHillaire`;
- 1 shader directory mapping `/Andromeda/Hillaire`;
- dipendenze renderer `RenderCore` / `RHI` / `Renderer` + include path `Renderer/Public|Internal` da `Andromeda.Build.cs` (uso esclusivo Hillaire, verificato via grep).

Build `AndromedaEditor Win64 Development` = **Succeeded** (14.51 s, unico warning pre-esistente
su `TextureCube.h` dell'engine, non correlato).

---

## 2. Initial Audit

### 2.1 Struttura trovata (pre-cleanup, questa sessione)

```text
Source/Andromeda
├── Public/Atmosphere/AndromedaHillaireCore.h          (data model, 236 righe)
├── Public/Atmosphere/AndromedaHillaireLUTRenderer.h   (LUT cache + RDG API, 131 righe)
├── Private/Atmosphere/AndromedaHillaireCore.cpp       (hash + transform + selection, 287 righe)
├── Private/Atmosphere/AndromedaHillaireLUTRenderer.cpp (renderer RDG + CVars, 757 righe)
├── Private/Atmosphere/AndromedaHillaireShaders.h      (4 FGlobalShader, 130 righe)
├── Andromeda.cpp  (include Hillaire + Initialize/Shutdown)
└── Andromeda.Build.cs (RenderCore/RHI/Renderer + Renderer include paths)
Shaders/Andromeda/Hillaire/
├── HillaireCommon.ush      (shared core, 310 righe)
├── HillaireTransmittance.usf (entry HillaireTransmittanceMainCS)
├── HillaireMultiScatter.usf  (entry HillaireMultiScatterMainCS)
├── HillaireSkyView.usf       (entry HillaireSkyViewMainCS)
└── HillaireSky.usf           (entry HillaireSkyMainPS, 383 righe)
```

### 2.2 Misurazioni grep (pre-cleanup, base `Source/`)

| Pattern | Risultato |
|---|---|
| `Hillaire` (case-sensitive) | ~180 match, 95% in `Atmosphere/` |
| `Atmosphere` (-i) | ~70 match, quasi tutti `Atmosphere/` + 3 commenti CLEAN SLATE |
| `Zephyr` (-i) | 3 match, solo commenti `ATMOS/ZEPHYR removal` |
| `SkyAtmosphere` | 1 match, commento reference standalone |
| `Bruneton`, `Irradiance`, `Aerial(Perspective)`, `SceneViewExtension`, `PostOpaque`, `ENQUEUE_RENDER`, `CurrentPlanet`, `AtmosphereManager`, `AtmosphereRenderer` | **0 match** (legacy già assente) |
| `LUT` (case-sensitive) | 76 match in `LUTRenderer.cpp`, resto in `Atmosphere/` + shader |
| `RDG\|FRDG` | 45 + 17 match, solo `Atmosphere/` |
| `r.AndromedaHillaire.*` | 9 CVars + 1 command, solo `LUTRenderer.cpp` |
| `LogAndromedaHillaire` | declare + define + 3 `UE_LOG`, solo `Atmosphere/` |
| `Atmosphere/` negli `#include` | solo `Andromeda.cpp` + interni `Atmosphere/` |

### 2.3 Classificazione applicata

```text
Path: Source/Andromeda/Public/Atmosphere/AndromedaHillaireCore.h
Type: C++ header (UHT-free structs + namespace API)
System: Legacy Hillaire (WIP multi-planet data model)
Classification: REMOVE
Reason: Intero data model atmosferico (profili, stati per-pianeta, selezione, hash). Nessun consumer fuori da Atmosphere/.
Dependencies: CoreMinimal, UnrealMathUtility. Dependents: AndromedaHillaireLUTRenderer.h/.cpp soltanto.
Action: Delete

Path: Source/Andromeda/Public/Atmosphere/AndromedaHillaireLUTRenderer.h
Type: C++ header (RDG renderer interface)
System: Legacy Hillaire LUT system
Classification: REMOVE
Reason: LUT cache, GPU packing, entry point RenderSky senza chiamanti (inerte). Unico includente: Andromeda.cpp.
Dependencies: AndromedaHillaireCore.h. Dependents: Andromeda.cpp, AndromedaHillaireShaders.h, .cpp.
Action: Delete

Path: Source/Andromeda/Private/Atmosphere/AndromedaHillaireCore.cpp
Type: C++ implementation (hash FNV-1a, transforms, SelectPlanets, PlanetScreenRect)
System: Legacy Hillaire
Classification: REMOVE — Reason/Dependents: come sopra. Action: Delete

Path: Source/Andromeda/Private/Atmosphere/AndromedaHillaireLUTRenderer.cpp
Type: C++ implementation (IMPLEMENT_GLOBAL_SHADER x4, 9 CVars, 1 command, RDG passes)
System: Legacy Hillaire (renderer + console + log usage)
Classification: REMOVE — unico file con CVars r.AndromedaHillaire.* e UE_LOG LogAndromedaHillaire. Action: Delete

Path: Source/Andromeda/Private/Atmosphere/AndromedaHillaireShaders.h
Type: C++ shader interface (4 FGlobalShader)
System: Legacy Hillaire shader binding
Classification: REMOVE — referenziato solo dal .cpp eliminato. Action: Delete

Path: Shaders/Andromeda/Hillaire/ (5 file)
Type: HLSL (1 .ush shared + 3 compute + 1 pixel)
System: Legacy Hillaire shader pipeline
Classification: REMOVE
Reason: Pipeline Transmittance→MultiScatter→SkyView→Sky autosufficiente ma senza chiamanti; registrata solo dagli IMPLEMENT_GLOBAL_SHADER eliminati.
Dependencies: solo Engine.ush + HillaireCommon.ush interno. Dependents: nessuno dopo delete C++.
Action: Delete tutti i 5 file + directory vuote

Path: Source/Andromeda/Andromeda.cpp (include + Initialize/Shutdown)
Type: module startup integration — Classification: MODIFY (rimuovere solo parte atmosferica)

Path: Source/Andromeda/Andromeda.Build.cs (RenderCore/RHI/Renderer + Renderer include paths)
Type: build dependencies — Classification: MODIFY
Reason: grep prova uso esclusivo Hillaire (zero include renderer fuori da Atmosphere/). Rimozione sicura.

Path: Content/Materials/M_UAS_Atmosphere.uasset
Type: Material asset — Classification: REVIEW REQUIRED (lasciato intatto)
Reason: referenziato da BP_Planet.uasset (binary). Regola asset: mai cancellare cieca se referenziato.

Path: STARMAP (StarSystem, Planet, Sun, generatori, Pawn, GameMode-base, gravity, lighting, noise/seed)
Type: gameplay/astronomy — Classification: KEEP (logica intoccata)
```

---

## 3. Removed Files

C++ (5):

- `Source/Andromeda/Public/Atmosphere/AndromedaHillaireCore.h`
- `Source/Andromeda/Public/Atmosphere/AndromedaHillaireLUTRenderer.h`
- `Source/Andromeda/Private/Atmosphere/AndromedaHillaireCore.cpp`
- `Source/Andromeda/Private/Atmosphere/AndromedaHillaireLUTRenderer.cpp`
- `Source/Andromeda/Private/Atmosphere/AndromedaHillaireShaders.h`

Shader (5):

- `Shaders/Andromeda/Hillaire/HillaireCommon.ush`
- `Shaders/Andromeda/Hillaire/HillaireTransmittance.usf`
- `Shaders/Andromeda/Hillaire/HillaireMultiScatter.usf`
- `Shaders/Andromeda/Hillaire/HillaireSkyView.usf`
- `Shaders/Andromeda/Hillaire/HillaireSky.usf`

Directory divenute vuote e rimosse (3):

- `Source/Andromeda/Public/Atmosphere/`
- `Source/Andromeda/Private/Atmosphere/`
- `Shaders/Andromeda/Hillaire/` (+ `Shaders/Andromeda/`, vuota a sua volta)

Nota: i file Hillaire risultavano untracked in git (WIP mai committato), quindi la rimozione
non appare come `D` in `git status`; i file ATMOS/ZEPHYR/UNIFIED della pulizia precedente
(2026-09-14) risultano ancora come deletions uncommitted nella working copy — non toccate
in questa sessione.

---

## 4. Removed Systems

- Hillaire legacy data model (`FHillaireAtmosphereProfile`, `FHillaireAtmosphereInstance`,
  `FPlanetAtmosphereState`, `FPlanetSelection`, hash FNV-1a, transform world↔local, selezione pianeti).
- Hillaire legacy LUT system (atlas Transmittance 256×64, MultiScatter 32×32, SkyView 192×112,
  cache `IPooledRenderTarget`, chiavi di invalidazione, `RenderSky` RDG).
- Hillaire legacy shaders (3 compute + 1 pixel + 1 shared header, entry
  `HillaireTransmittanceMainCS`, `HillaireMultiScatterMainCS`, `HillaireSkyViewMainCS`, `HillaireSkyMainPS`).
- Hillaire legacy console (CVars `r.AndromedaHillaire.Enable/DebugMode/Exposure/
  SunAngularRadiusDeg/SunLuminance/MultiScatterScale/MieScale/AbsorptionScale/FreezeLUTs`
  + command `r.AndromedaHillaire.Validate`).
- Hillaire legacy logging (`LogAndromedaHillaire`: declare + define + usi).
- Hillaire legacy shader mapping (`/Andromeda/Hillaire` → `Shaders/Andromeda/Hillaire`).
- Hillaire legacy build deps (`RenderCore`, `RHI`, `Renderer` + `Renderer/Public|Internal` include paths).
- Confermata assenza (già dalla pulizia precedente, riverificata): ATMOS renderer/manager/mailbox,
  ZEPHYR renderer/manager/LUT/view-extension/profile-library, UNIFIED mailbox/dual-renderer/registry/
  light-reference/commands, view extension e Tonemap hook atmosferici (zero match).

---

## 5. Modified Files

```text
Path: Source/Andromeda/Andromeda.cpp
Reason: unico includente del sistema eliminato; Startup/Shutdown chiamavano solo AndromedaHillaire::Initialize/Shutdown.
What changed: rimosso #include "Atmosphere/AndromedaHillaireLUTRenderer.h" e le due chiamate;
  Startup/Shutdown ora vuoti con commento che documenta il cleanup e il futuro port.
Why safe: nessun altro simbolo del modulo toccato; IMPLEMENT_PRIMARY_GAME_MODULE invariato;
  build succeeded lo conferma.
```

```text
Path: Source/Andromeda/Andromeda.Build.cs
Reason: dipendenze renderer servivano esclusivamente al LUT renderer RDG eliminato.
What changed: rimosso blocco PrivateDependencyModuleNames (RenderCore/RHI/Renderer),
  rimossi i due PrivateIncludePaths Renderer/Public|Internal, rimosso using System.IO
  (serviva solo a Path.Combine), lasciato commento esplicativo.
Why safe: grep prova zero #include renderer fuori da Atmosphere/ (GlobalShader/RenderGraph/
  RHI/ScreenPass/SceneView assenti nel codice restante); build succeeded senza errori né
  nuovi warning.
```

Nessun altro file modificato in questa sessione. (`AndromedaGameMode.cpp`, `Planet.cpp`,
`StarSystem.cpp`, `Sun.cpp`, `AndromedaGameMode.h` risultano `M` in git per la precedente
pulizia ATMOS/ZEPHYR del 2026-09-14, non toccati ora.)

---

## 6. Preserved Systems

Esplicitamente preservati e intoccati nella logica:

- STARMAP: `StarSystem.h/.cpp` (generazione, `UpdatePlanetOrbits`, `UpdatePlanetRotations`,
  query runtime, `GetSunActor`, `GetAllPlanetRuntimeData`), `StarSystemGenerator`,
  `PlanetProfile` (+ `EPlanetArchetype`).
- Planet generation: `PlanetLandformGenerator`, `PlanetContinentalGenerator`,
  `PlanetDepressionGenerator`, `PlanetBiomeGenerator`, `PlanetTerrainGenerator`,
  `PlanetSurfaceData`, `APlanet` (mesh terreno, lighting).
- Solar system: `Sun` (luci Point/SkyLight, mesh decorativa), `PlanetaryLightingComponent`,
  `PlanetaryGravitySystem`.
- Gameplay: `AndromedaPawn`, `AndromedaNoiseLibrary`, `AndromedaSeedLibrary`,
  `AndromedaGameMode` (base, solo `Super::BeginPlay`).
- Rendering non atmosferico: nessuna dipendenza renderer rimossa che servisse ad altro;
  `Config/*.ini` intoccati (non contenevano chiavi atmosferiche).
- Asset: `M_Planet.uasset`, `M_Sun.uasset`, `M_UAS_Atmosphere.uasset`, Blueprint e mappe —
  nessun asset cancellato o modificato.

---

## 7. Residual Search

Ricerca finale su `Source/`, `Config/`, `Shaders/` (rg, case-insensitive dove indicato):

| # | Pattern | Match | Classification | Reason |
|---|---|---|---|---|
| 1 | `Hillaire` (CS) | `Andromeda.cpp:6-7`, `Andromeda.Build.cs:21,23,25` — solo commenti `HILLAIRE LEGACY CLEANUP` | REFERENCE / DOCUMENTATION | Commenti che documentano il cleanup, nessuno simbolo |
| 2 | `AndromedaHillaire`, `LogAndromedaHillaire`, `r.AndromedaHillaire.*` | **0 match** | NONE | CVars, command, log, namespace spariti |
| 3 | `Zephyr`, `ATMOS/ZEPHYR` | `AndromedaGameMode.cpp:21,23` commento storico CLEAN SLATE | REFERENCE / DOCUMENTATION | Pre-esistente, non toccato per regola STARMAP-minimo |
| 4 | `SkyAtmosphere`, `Bruneton`, `Irradiance`, `AerialPerspective`, `MultipleScattering`, `SingleScattering`, `PlanetAtmosphere`, `CurrentPlanet`, `AtmosphereManager`, `AtmosphereRenderer`, `AtmosphereSystem`, `AtmosphereViewExtension`, `AtmosphereRegistry`, `AtmosphereLightReference`, `mailbox` | **0 match** | NONE | Nessun residuo reale |
| 5 | `Atmosphere` generico (-i) | Solo i commenti di riga 1–3 (cleanup) | REFERENCE / DOCUMENTATION | Nessun codice |
| 6 | `LUT` (CS) | Solo commento Build.cs (`deleted Hillaire LUT renderer`) | REFERENCE / DOCUMENTATION | Nessun codice LUT rimasto |
| 7 | `GlobalShader`, `RenderGraph`, `Renderer/Public`, `Renderer/Internal` | **0 match** (solo commento `RDG/FGlobalShader` in Build.cs) | NONE | Nessun include renderer rimasto |
| 8 | `Atmosphere/` negli include | **0 match** | NONE | Nessun dangling include |
| 9 | `Transmittance`, `Scattering` (-i) | **0 match** in Source | NONE | Fisica scattering sparita |
| 10 | Config `Atmosphere\|Hillaire\|Zephyr` | **0 match** in `Config/*.ini` | NONE | Config puliti |
| 11 | `Shaders/` contenuto | directory vuota, 0 file `.usf/.ush` | NONE | Nessun shader orfano |
| 12 | Commenti italiani `atmosfera/atmosferica` (`PlanetaryGravitySystem.h`, `StarSystem.h`, `PlanetBiomeGenerator.cpp:863`) | documentazione di dominio (limite atmosfera, circolazione atmosferica come modello biome) | LEGITIMATE NON-ATMOSPHERIC CODE | Prosa, non simboli né pipeline |

Falsi positivi noti e ignorati: `LUT` case-insensitive matcha `Resolution`, `assoluta`,
`valutata`, `volutamente` — verificati uno a uno, nessuno atmosferico.

---

## 8. Build Validation

```text
Build command:
& "C:\Unreal Engine\UE_5.8\Engine\Build\BatchFiles\Build.bat" AndromedaEditor Win64 Development
  -project="C:\Users\aless\Documents\Unreal Projects\Andromeda\Andromeda.uproject" -waitmutex

Result: Succeeded
Total execution time: 14.51 s (UBA local executor 9.89 s, 22 actions)
UHT: nessun errore. Compilati tutti i 17 moduli C++ restanti
  (Andromeda, GameMode, Pawn, Noise, Seed, Gravity, Lighting, StarSystem,
   StarSystemGenerator, Sun, Planet, 6 generatori Planet, gen files).
  Nessun file Atmosphere compila più (conferma rimozione).
Errors: 0
Warnings: 1 pre-esistente, non correlato:
  Engine TextureCube.h(43,26): warning C4996 'UTexture::GetAssetRegistryTags'
  (stesso warning già riportato nella pulizia del 2026-09-14).
```

---

## 9. Shader Validation

```text
Result: PASS (per costruzione + build)
- 0 file .usf/.ush nel progetto (Shaders/ vuota): nessun include mancante possibile.
- 0 IMPLEMENT_GLOBAL_SHADER nel codice (il .cpp che li conteneva è eliminato).
- 0 AddShaderSourceDirectoryMapping (Initialize eliminato con Andromeda.cpp).
- 0 riferimenti HLSL Hillaire/Transmittance/MultiScatter/SkyView nei sorgenti.
- La build UBT/UHT/shader-map ha linkato senza errori di shader mancanti.
Errors: 0 — Warnings: 0
```

---

## 10. Final Architecture

```text
STARMAP (AStarSystem → FPlanetRuntimeData → APlanet / ASun)
├── planet generation (Landform/Continental/Depression/Biome/Terrain + SurfaceData + Profile)
├── solar system (Sun lights + mesh, PlanetaryLighting, PlanetaryGravity)
└── gameplay (AndromedaPawn, GameMode-base, Noise/Seed libraries)
        ↓  (nessun consumer atmosferico, nessun mailbox, nessun hook)
[ FUTURE HILLAIRE MULTIPLANET PORT ]  (non implementato — slot libero)
        ↓
FAndromedaModule (vuoto: nessuno shader mapping, nessun ViewExtension, nessun dispatch)
        ↓
(no renderer → no SceneColor integration — cielo/atmosfera assenti per scelta)
```

`Source/` finale: 35 file C++ (17 Public + 18 Private incl. Andromeda.cpp/h), zero directory
`Atmosphere/`, zero dipendenze `RenderCore/RHI/Renderer`. `Shaders/`: vuota. `Config/`: pulita.

---

## 11. Remaining Review Items

Elementi lasciati intenzionalmente intatti (ambiguo / fuori portata chirurgica):

1. `Content/Materials/M_UAS_Atmosphere.uasset` — REVIEW REQUIRED.
   Material legacy referenziato da `Content/Blueprints/Planets/BP_Planet.uasset` (match binary).
   Non cancellato per la regola asset (mai rompere reference). Da gestire con l'editor
   durante il futuro port (sostituire o rimuovere il riferimento dal Blueprint).
2. `BP_Planet.uasset` contiene riferimento testuale a `PlanetAtmosphereComponent`
   (classe C++ eliminata nella pulizia ATMOS del 2026-09-14). Riferimento Blueprint
   potenzialmente dangling — richiede verifica/apertura in editor, fuori portata di un
   cleanup C++/shader headless. Segnalato, non toccato (binario).
3. `SM_Athmoshpere` (typo originale) in `Maps/_GENERATED/` — mesh generata referenziata dal
   materiale sopra; stessa gestione editor futura.
4. Report storici in root (`SKY_ATMOSPHERE_*`, `ZEPHYR_PHASE_*`, `ATMOS_ZEPHYR_CLEANUP_REPORT.md`)
   e `Tools/ZephyrSim/` (simulatore Python offline, non compilato) — conservati
   deliberatamente come documentazione/reference, nessun hook runtime.
5. Commenti storici `CLEAN SLATE (ATMOS/ZEPHYR removal)` in `AndromedaGameMode.cpp/.h`,
   `StarSystem.h`, `PlanetaryGravitySystem.h`, doc `atmosfera/circolazione atmosferica` —
   prosa legittima, nessun simbolo; lasciati per non fare cleanup eccessivo.

---

## 12. Explicit Confirmation

```text
NEW HILLAIRE MULTIPLANET PORT:
NOT IMPLEMENTED

STANDALONE HILLAIRE REFERENCE (C:\Users\aless\UnrealEngineSkyAtmosphere):
NOT MODIFIED (mai aperta in scrittura; solo verificata esistenza via Test-Path)

ANDROMEDA LEGACY HILLAIRE:
CLEANED

ATMOS LEGACY:
CLEANED (verificato assente; rimozione fisica dalla pulizia precedente, uncommitted)

ZEPHYR LEGACY:
CLEANED (verificato assente; rimozione fisica dalla pulizia precedente, uncommitted)

STARMAP:
PRESERVED (logica astronomica/gameplay intatta, build succeeded)
```

# ATMOS / ZEPHYR CLEANUP REPORT — CLEAN SLATE

Data: 2026-09-14
Branch: `main` (working copy, non committato — demolizione + edit da revisionare prima del commit)
Task: demolizione/pulizia esclusiva. Nessun nuovo renderer, nessun tuning, nessuna modifica STARMAP.

---

## 1. Legacy systems identified (FASE 0–1, audit pre-cancellazione)

Mappa costruita prima di cancellare:

```text
Source/Andromeda
 ├── ATMOS legacy (renderer + dati + mailbox + hook + console)
 │    ├── Public/Atmosphere/AndromedaAtmosphereTypes.h       (handle, params, FAndromedaAtmosphereGPUData, LogAndromedaAtmos)
 │    ├── Public/Atmosphere/AndromedaAtmosphereManager.h + Private/.../AndromedaAtmosphereManager.cpp  (mailbox ATMOS, deprecated)
 │    ├── Public/Atmosphere/AndromedaAtmosphereRenderer.h + Private/.../AndromedaAtmosphereRenderer.cpp (aerial raymarch, mapping /Andromeda, r.AndromedaAtmos.*)
 │    ├── Private/Atmosphere/AndromedaAtmosphereShader.h     (FAndromedaAtmospherePS)
 │    ├── Private/Atmosphere/AndromedaAtmosphereViewExtension.h/.cpp (Tonemap hook legacy, deprecated no-op)
 │    ├── Public/PlanetAtmosphereComponent.h + Private/PlanetAtmosphereComponent.cpp (component per-pianeta, deprecated)
 │    ├── Public/PlanetAtmosphereRenderer.h + Private/PlanetAtmosphereRenderer.cpp   (single-planet UAS renderer, deprecated)
 │    └── Shaders/Andromeda/AndromedaAtmosphere.usf
 ├── ZEPHYR legacy (renderer + dati + mailbox + hook + console + LUT + cache + comandi)
 │    ├── Public/Planet/Zephyr/ZephyrTypes.h + Private/.../ZephyrTypes.cpp (profili, snapshot, GPU data, LogAndromedaZephyr)
 │    ├── Public/Planet/Zephyr/ZephyrManager.h + Private/.../ZephyrManager.cpp (mailbox ZEPHYR, deprecated)
 │    ├── Public/Planet/Zephyr/ZephyrRenderer.h + Private/.../ZephyrRenderer.cpp (~99 KB: LUT pipeline + sky, r.AndromedaZephyr.*)
 │    ├── Private/Planet/Zephyr/ZephyrShaders.h              (FZephyr* shaders)
 │    ├── Private/Planet/Zephyr/ZephyrViewExtension.h/.cpp   (Tonemap hook legacy, deprecated no-op)
 │    ├── Public/Planet/Zephyr/ZephyrProfileLibrary.h + Private/.../ZephyrProfileLibrary.cpp (seed+archetipo → profilo)
 │    ├── Public/Planet/Zephyr/ZephyrSharedAtmosphere.h      (consistency layer ATMOS/ZEPHYR)
 │    └── Shaders/Andromeda/Zephyr/* (ZephyrCommon.ush + 4 .usf)
 ├── UNIFIED glue (secondo sistema concorrente: mailbox + renderer dual-stage + comandi)
 │    ├── Public/Atmosphere/AndromedaAtmosphereSystem.h + Private/.../AndromedaAtmosphereSystem.cpp (mailbox unificato)
 │    ├── Public/Atmosphere/AndromedaUnifiedAtmosphereRenderer.h + Private/.../AndromedaUnifiedAtmosphereRenderer.cpp (owner Tonemap Aerial→Sky)
 │    ├── Private/Atmosphere/AndromedaUnifiedAtmosphereViewExtension.h (single-hook extension)
 │    ├── Private/Atmosphere/AndromedaAtmosphereUnifiedCommands.cpp (console r.AndromedaAtmosphere.*)
 │    ├── Public/Atmosphere/AndromedaAtmosphereRegistry.h + Private/.../AndromedaAtmosphereRegistry.cpp (actor publisher snapshot)
 │    └── Public/Atmosphere/AtmosphereLightReferenceComponent.h + Private/.../AtmosphereLightReferenceComponent.cpp (sun reference)
 ├── Hillaire new core (DA PRESERVARE — nuovo sistema, mai concorrente via hook)
 │    ├── Public/Atmosphere/AndromedaHillaireCore.h + Private/.../AndromedaHillaireCore.cpp
 │    ├── Public/Atmosphere/AndromedaHillaireLUTRenderer.h + Private/.../AndromedaHillaireLUTRenderer.cpp
 │    ├── Private/Atmosphere/AndromedaHillaireShaders.h
 │    └── Shaders/Andromeda/Hillaire/* (HillaireCommon.ush + 4 .usf, self-contained)
 ├── STARMAP (INTOCCABILE: StarSystem, Planet, Sun, gravità, lighting,Pawn, GameMode-base)
 └── shared / ambiguous (risolti in FASE 3, vedi §3)
```

Dato critico dell'audit: il core Hillaire (file untracked, WIP) dipendeva da header legacy
(`Planet/Zephyr/ZephyrTypes.h`, `Atmosphere/AndromedaAtmosphereSystem.h`,
`Atmosphere/AndromedaAtmosphereTypes.h`) e il suo shader-header C++ referenziava un tipo
inesistente (`FHillairePlanetGPUData` al posto di `AndromedaHillaire::FPlanetGPUData`):
nella working copy pre-pulizia **non compilava**. Sciolto in FASE 3 senza toccare fisica/numerica (§4).

---

## 2. Files deleted (FASE 2 — 41 file, rimozione reale, nessuna copia temporanea)

ATMOS (14): `AndromedaAtmosphereTypes.h`, `AndromedaAtmosphereManager.h/.cpp`,
`AndromedaAtmosphereRenderer.h/.cpp`, `AndromedaAtmosphereShader.h`,
`AndromedaAtmosphereViewExtension.h/.cpp`, `AndromedaAtmosphere.usf`,
`PlanetAtmosphereComponent.h/.cpp`, `PlanetAtmosphereRenderer.h/.cpp`,
più junk tracciato `Private/AndromedaNoiseLibrary.cpp~RF58a5d13.TMP` (temp editor committato per errore).

ZEPHYR (17): `ZephyrTypes.h/.cpp`, `ZephyrManager.h/.cpp`, `ZephyrRenderer.h/.cpp`,
`ZephyrShaders.h`, `ZephyrViewExtension.h/.cpp`, `ZephyrProfileLibrary.h/.cpp`,
`ZephyrSharedAtmosphere.h`, `ZephyrCommon.ush`, `ZephyrTransmittance.usf`,
`ZephyrMultiScatter.usf`, `ZephyrSkyView.usf`, `ZephyrSky.usf`
(rimosse anche le directory divenute vuote `Public/Planet/Zephyr`, `Private/Planet/Zephyr`,
`Shaders/Andromeda/Zephyr`).

UNIFIED glue (10): `AndromedaAtmosphereSystem.h/.cpp`, `AndromedaUnifiedAtmosphereRenderer.h/.cpp`,
`AndromedaUnifiedAtmosphereViewExtension.h`, `AndromedaAtmosphereUnifiedCommands.cpp`,
`AndromedaAtmosphereRegistry.h/.cpp`, `AtmosphereLightReferenceComponent.h/.cpp`.

---

## 3. Integrations removed (FASE 3 — dai file rimanenti)

- `Source/Andromeda/Andromeda.cpp` — riscritto: resta solo `AndromedaHillaire::Initialize/Shutdown`
  (mapping directory shader `/Andromeda/Hillaire`, nessun hook, nessun dispatch).
- `Public/Planet/Planet.h` + `Private/Planet/Planet.cpp` — rimossi membro `Atmosphere`
  (`UPlanetAtmosphereComponent`), `AtmosphereMesh`, `GenerateAtmosphereMesh()` (tutta la funzione),
  chiamata `InitializeAtmosphere()`. `PlanetaryLighting` e mesh terreno intoccati.
- `Public/Sun.h` + `Private/Sun.cpp` — rimossi membro `AtmosphereLightReference`, flag
  `bHideSunMeshForZephyrSky`; `ConfigureSunMesh()` ora lascia la mesh decorativa visibile
  (nessuno sky-renderer la sostituisce più).
- `Private/AndromedaGameMode.cpp` + `Public/AndromedaGameMode.h` — rimosso auto-spawn di
  `AAndromedaAtmosphereRegistry`; `BeginPlay` solo `Super`.
- `Public/StarSystem.h` + `Private/StarSystem.cpp` — `GetSunActor()` **mantenuto** come API
  generica STARMAP; ripuliti i commenti che lo legavano al Registry/AtmosphereLightReference.
- Nessun `.ini` toccato: `Config/` non conteneva chiavi atmosferiche.

---

## 4. Hillaire files preserved (+ decoupling minimo, zero fisica toccata)

Preservati integralmente come base del rebuild, inclusi tutti i 5 shader Hillaire (byte-identici,
self-contained: includono solo Engine + `HillaireCommon.ush`):

- `Public/Atmosphere/AndromedaHillaireCore.h` — ora definisce tipi propri
  `FHillaireAtmosphereProfile` / `FHillaireAtmosphereInstance` (copia campo-per-campo dagli
  ex-tipi Zephyr, senza macro UHT/Blueprint) + `DECLARE_LOG_CATEGORY_EXTERN(LogAndromedaHillaire)`;
  rimossi include legacy.
- `Private/Atmosphere/AndromedaHillaireCore.cpp` — `DEFINE_LOG_CATEGORY(LogAndromedaHillaire)`,
  firma hash sul nuovo tipo. Logica/hash/trasformate/selezione **invariati**.
- `Public/Atmosphere/AndromedaHillaireLUTRenderer.h` — firme su `FHillaireAtmosphereInstance`,
  commento layout aggiornato a `HillaireCommon.ush`.
- `Private/Atmosphere/AndromedaHillaireLUTRenderer.cpp` — rimossi include legacy,
  `FAndromedaAtmosphereInstance` → `FHillaireAtmosphereInstance`,
  `LogAndromedaAtmosphere` → `LogAndromedaHillaire`;
  `LastSnapshotVersion` ora = contatore dispatch (nessun mailbox). LUT/cache/GPU packing **invariati**.
- `Private/Atmosphere/AndromedaHillaireShaders.h` — fix del nome tipo
  `FHillairePlanetGPUData` → `AndromedaHillaire::FPlanetGPUData` (era riferimento a tipo inesistente,
  il modulo non compilava).
- Stato attuale: il core compila ma è **inerte** — `Initialize()` mappa solo la directory shader,
  nessun ViewExtension, nessun hook Tonemap, `RenderSky()` senza chiamanti. Nessun secondo renderer.

---

## 5. STARMAP files preserved

Intoccati nella logica: `StarSystem.h/.cpp` (generazione, orbite `UpdatePlanetOrbits`,
rotazioni `UpdatePlanetRotations`, query runtime, `GetSunActor`, `GetAllPlanetRuntimeData`),
`StarSystemGenerator`, `PlanetProfile` (+ `EPlanetArchetype`), generatori Landform/Continental/
Depression/Biome/Terrain, `PlanetSurfaceData`, `APlanet` (mesh terreno, lighting),
`Sun` (luci Point/SkyLight, mesh), `PlanetaryLightingComponent`, `PlanetaryGravitySystem`,
`AndromedaPawn`, `AndromedaNoiseLibrary`, `AndromedaSeedLibrary`, `AndromedaGameMode` (base).
Solo rimozioni atmosferiche puntuali (§3), mai logica di simulazione.

---

## 6. Remaining atmosphere-related references (FASE 6 — classificazione residui)

Ricerca `ATMOS|Atmos`, `ZEPHYR|Zephyr`, `SkyAtmosphere|AtmosphereSystem|AtmosphereRenderer|
AtmosphereViewExtension|mailbox`, `Tonemap|SubscribeToPostProcessing|ViewExtension`,
`r.Andromeda*` su `Source/`:

- **Accettabili / nuovo core**: `FHillaireAtmosphere*`, `FPlanetAtmosphereState`, campi
  `AtmosphereRadius`/`AtmosphericDensityScale`, include-dir `Atmosphere/`, console
  `r.AndromedaHillaire.*` (inerte, nessun chiamante), `LogAndromedaHillaire`.
- **Solo commenti**: 3× `ATMOS/ZEPHYR removal` (Andromeda.cpp, GameMode, HillaireCore.h),
  1× `ATMOS-04 stabilization` (tecnica camera-relative, nota storica in HillaireLUTRenderer.cpp:617),
  1× rimozione mailbox (commento).
- **NON rimangono**: renderer legacy, ViewExtension, Tonemap hook, shader legacy, LUT renderer
  legacy, mailbox, dispatch legacy, console `r.AndromedaAtmos.*` / `r.AndromedaZephyr.*` /
  `r.AndromedaAtmosphere.*`. Zero writer di SceneColor attivi.
- Fuori `Source/` (volutamente conservati, non-runtime): report storici in root
  (`SKY_ATMOSPHERE_*`, `ZEPHYR_PHASE_*`), tooling offline `Tools/ZephyrSim/` (simulatore Python
  CPU, non compilato, nessuno hook — reference utile al rebuild), asset generati
  (`SM_Athmoshpere.uasset`, typo originale). Nessun riferimento ad `AtmosphereRegistry` nella
  mappa (`Content/` pulito).

---

## 7. Build result (FASE 5)

```text
& "C:\Unreal Engine\UE_5.8\Engine\Build\BatchFiles\Build.bat" AndromedaEditor Win64 Development
  -project="...\Andromeda\Andromeda.uproject" -waitmutex
Result: Succeeded (16.90 s, UHT 6 file, 13 azioni, unico warning pre-esistente su TextureCube.h engine).
```

Compilati al primo colpo tutti i file toccati (Andromeda, GameMode, HillaireCore, HillaireLUTRenderer,
Planet, Sun, StarSystem). Nessun riferimento rotto residuo.

---

## 8. Final architecture

```text
STARMAP (AStarSystem → FPlanetRuntimeData → APlanet / ASun)
   ↓  (nessun consumer atmosferico: nessun Registry, nessun mailbox)
[ FUTURE HILLAIRE ATMOSPHERE ]  (da implementare nel prossimo task)
   ↓
AndromedaHillaire core (inerte: solo mapping shader /Andromeda/Hillaire)
   ↓
(no renderer → no SceneColor integration — cielo/atmosfera assenti per scelta)
```

## Conclusione

> ATMOS legacy e ZEPHYR legacy sono stati completamente rimossi e non esiste più un secondo
> renderer atmosferico concorrente.

41 file legacy cancellati, 8 file STARMAP/integrazione ripuliti, core Hillaire preservato e
scollegato dai tipi legacy (compilante ma inerte), STARMAP intatto, build `AndromedaEditor
Win64 Development` = **Succeeded**. Il progetto è in clean slate: temporaneamente senza
atmosfera, pronto per `REBUILD HILLAIRE ATMOSPHERE FROM CLEAN SLATE`.

---

## Appendice — tavola fisica archetipi (ex-`ZephyrProfileLibrary.cpp`, per il rebuild)

Baseline Terra (condivisa): Rayleigh km⁻¹ (0.0058, 0.0135, 0.0331), H_Rayleigh 8 km,
Mie 0.004 neutro, g 0.76, H_Mie 1.2 km, assorbimento picco (0.00065, 0.001881, 0.000085),
strato 25 km ± 15 km, albedo 0.3, densità 1.0. Jitter deterministico da seed: Rayleigh ±15%,
Mie ±20%, H ±15%, assorbimento ±20%, densità ±15%, g ±5%, albedo ±20% (clamp 0.02–0.9).
Moltiplicatori archetipo (applicati prima del jitter): Oceanic Mie×1.6 albedo 0.15 dens 1.1;
Jungle Mie×2.0 dens 1.15 albedo 0.2; Arid Mie×3.0 H_Mie×1.4 albedo 0.45 dens 1.05;
Desert Mie×4.5 H_Mie×1.6 albedo 0.55 dens 1.1 ass×1.4; Frozen Ray×0.9 Mie×0.5 dens 0.4
H_Ray×0.8 albedo 0.6; Tundra Mie×0.7 dens 0.7 albedo 0.5; Rocky Mie×0.6 dens 0.55 albedo 0.25;
Volcanic Mie×6.0 H_Mie×1.8 ass×3.0 albedo 0.08 dens 1.6;
Exotic Ray (0.0090, 0.0105, 0.0140) Mie×2.5 ass (0.0004, 0.0012, 0.0045) albedo 0.35 dens 1.25 g 0.65.
Compensazione colonna toy-planet: densità molecolare × clamp(8 km / min(H_Ray, 0.35×shell), 1, 60);
MieDensityScale sempre 1.0 (loading aerosol indipendente dalla pressione). Transizione visiva:
Rs = R_pianeta + terreno_max + 2 km; completamento = (R_pianeta + terreno) × 1.1.

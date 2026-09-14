# ZEPHYR PHASE 2.1 — UNIFIED ATMOSPHERE REPORT

**Data:** 2026-09-14
**Fase:** 2.1 Unified Andromeda Atmosphere Rewrite (Steps A–J completati, K–L parziali)
**Target:** Andromeda.uproject (UE 5.8)
**Piano vincolante:** `ZEPHYR_PHASE_2_1_UNIFICATION_PLAN.md`

---

## 1. ARCHITECTURE

### Old ATMOS analysis (sintesi)

`FAndromedaAtmosphereManager` (handle-based) + `AAndromedaAtmosphereRegistry`
(auto-registrazione) + `FAndromedaAtmosphereRenderer` (pass Rayleigh-only,
16-step march, stella puntiforme finita) + `AndromedaAtmosphere.usf` (1397 righe)
+ hook Tonemap proprio + `UPlanetAtmosphereComponent`/`UPlanetAtmosphereRenderer`
(legacy single-planet, non consumati da nessun renderer). Fisica inferiore
(Rayleigh-only dichiarato in `AndromedaAtmosphere.usf:28-29`), modello stella
finita divergente da Hillaire Case A.

### Old ZEPHYR analysis (sintesi)

`FZephyrPlanetProfile`/`FZephyrPlanetSnapshotEntry`/`FZephyrPlanetGPUData`
(modello full-physics corretto) + `FZephyrManager` (mailbox) +
`FZephyrRenderer` (2650 righe: pipeline LUT T→MS→SkyView→Sky PS, cache
planet/view key, governing planet, SunDirPlanet, dynamic max planets) +
`UZephyrProfileLibrary` (10 archetipi deterministici) + hook Tonemap proprio.
Fisica retained Hillaire Case A, ma SECONDO sistema parallelo con SECONDO
snapshot, SECONDA mailbox, SECONDO hook.

### Final unified architecture (implementata)

```text
STARMAP (AStarSystem, intoccato)
  │  FPlanetRuntimeData (PlanetID, Seed, Radius, TerrainHeight,
  │  WorldPosition, CurrentRotation, Orbit…)
  ▼
AAndromedaAtmosphereRegistry  (UNICO publisher, auto via Tick)
  │  UZephyrProfileLibrary::BuildProfile (invariato)
  ▼
FAndromedaAtmosphereSystem   (UNICA mailbox: istanze full-physics + star + version)
  │  Game Thread scrive ── Render Thread legge
  ▼
FUnifiedAtmosphereRenderer   (UNICO renderer, UNICO hook Tonemap)
  ├── Stage A — Aerial/limb (raymarch legacy, vista Rayleigh derivata)
  └── Stage B — Sky/LUT (Hillaire Case A, profilo full-physics)
  ▼
UE5.8 Tonemapper (una subscription, ordine deterministico A→B)
```

Nuovi file: `Public/Atmosphere/AndromedaAtmosphereSystem.h`,
`Private/Atmosphere/AndromedaAtmosphereSystem.cpp`,
`Public/Atmosphere/AndromedaUnifiedAtmosphereRenderer.h`,
`Private/Atmosphere/AndromedaUnifiedAtmosphereRenderer.cpp`,
`Private/Atmosphere/AndromedaUnifiedAtmosphereViewExtension.h`,
`Private/Atmosphere/AndromedaAtmosphereUnifiedCommands.cpp`.

### Removed duplication

| Prima (2×) | Dopo (1×) |
|------------|-----------|
| 2 mailbox (`FAndromedaAtmosphereManager` + `FZephyrManager`) | 1 (`FAndromedaAtmosphereSystem`); i vecchi restano compilanti ma fuori dal path attivo |
| 2 snapshot (handle-params + sky entries) | 1 (`FAndromedaAtmosphereInstance` = istanza full-physics) |
| 2 scrittori Registry (Register/Update + SetSnapshot) | 1 (`PublishUnifiedSnapshot` ogni Tick) |
| 2 hook Tonemap indipendenti | 1 (`FUnifiedAtmosphereViewExtension`, stage A→B ordinati); i vecchi `Register()` sono no-op con warning |
| 2 namespace console non-spec (`r.AndromedaZephyr.*`, `r.AndromedaAtmos.*`) | 1 namespace documentato (`r.AndromedaAtmosphere.*`, 8 voci reali); i vecchi restano come stage-gate transitori |

---

## 2. HILLAIRE FIDELITY

### Retained mathematics

Transmittance 40-step + occultazione, MultiScatter dual-scattering
(8 Fibonacci × 8 step + ground bounce), SkyView 24-step, mapping Hillaire
sky-latitude, V packing quadratico, fasi Rayleigh/Mie normalizzate,
effective scale `min(ScaleKm, ShellKm*0.35)`, assorbimento gaussiano clampato,
atlas sampling half-texel, `ZephyrCommon.ush` invariato. Case A direzionale only.

### Deviations (dichiarate, invariate dal Phase 2.0)

- Stage A usa stella puntiforme finita (legacy, in attesa di assorbimento LUT).
- ATMOS phase fold `3/(16π)` in exposure vs ZEPHYR fasi normalizzate +
  `ZEPHYR_RADIANCE_SCALE` (differenza di convenzione documentata, stesse sezioni d'urto).

### Dual-scattering explanation

Ordine 2 reale (gather direzioni Fibonacci di sorgenti L1 con transmittance LUT
+ bounce Lambertiano), mai `single*costante` (`MultiScatterScale=0` lo prova).
Non è l'iterativo Hillaire multi-rimbalzo: cattura il termine dominante
(lift al terminator, horizon bleed, blue hour). Nessuna nuova tecnica introdotta.

---

## 3. DATA

- **Planet instance:** `FAndromedaAtmosphereInstance` (alias promosso):
  PlanetID, PlanetCenter, PlanetRotation, Ground/AtmosphereRadius, Profile,
  TerrainHeightCm, SkyTransitionRadiusCm; star a livello sistema.
- **Profile:** `UZephyrProfileLibrary::BuildProfile` invariato (baseline Terra +
  10 archetipi + jitter SplitMix64 + column compensation + MieDensityScale
  disaccoppiato).
- **Runtime state:** mailbox versionata (`SnapshotVersion`), `Clear()` su EndPlay.
- **STARMAP integration:** Registry invariato nel discovery (BeginPlay + finestra
  10 s + Tick), riscritto solo il publish: nessuna `RegisterAtmosphere` per-handle,
  nessun `UpdateAtmosphereWorldPosition`, solo `SetSnapshot` full ogni Tick.
  Zero Actor/Component/Material/Blueprint/LUT manuali.

---

## 4. RENDERING

- **UE5.8 integration:** RDG, global shader (`IMPLEMENT_GLOBAL_SHADER` invariati),
  `FSceneViewExtensionBase::SubscribeToPostProcessingPass(Tonemap)`, render
  thread, `FSceneTextures`, depth (`DepthOcclusionEnabled`), camera-relative.
- **RDG:** pipeline LUT compute (T/MS/SkyView) + 2 pass fullscreen invariata.
- **Shader registration:** mapping `/Andromeda` invariato (ATMOS init), shader
  Zephyr sotto di esso; nessun cambio layout CPU↔GPU (contract
  `FZephyrPlanetGPUData` ↔ `ZephyrCommon.ush` intatto).
- **View extension/hook:** UNO (`FUnifiedAtmosphereViewExtension`), due delegati
  ordinati A→B; UE concatena SceneColor deterministicamente. Doppia
  sottoscrizione impossibile (legacy `Register()` = no-op + warning;
  `HandlePostEngineInit` legacy non registrano più).
- **Final composite:** Stage A (`SceneColor*T+L`), Stage B (overwrite sky pixel,
  passthrough geometria, gate `AtmosEnabled` retained fino al merge shader).

---

## 5. MULTI-PLANET

- **Limite:** `GetMaxPlanets()` dinamico retained (nessun cap arbitrario).
- **Profile isolation:** `ComputeProfileHash()` FNV-1a per pianeta + chiavi planet/view retained.
- **LUT isolation:** slice verticali + half-texel inset retained, nessun cambio.
- **Governing planet:** selezione shader retained (`ZephyrSky.usf` zone/fade/limb);
  diagnostica CPU approssimata (nearest surface distance) esposta in Status.
- **Testati:** 0 pianeti headless in questa fase (validazione multi-pianeta in PIE
  pendente — vedi §10).

---

## 6. TRANSFORM

- Convenzione verificata, NON modificata: `StarSystem.cpp:975-1044`
  (`CurrentRotation = (TiltQuat*SpinQuat).Rotator()`, tilt su Forward/X, spin su Up/Z).
- `SunDirPlanet = PlanetRot.Inverse() * SunDirWorld` applicata esattamente una
  volta in `BuildGPUData` (`ZephyrRenderer.cpp`), retained.
- `r.AndromedaAtmosphere.DebugMode 3` ristampa SunDirWorld/SunDirPlanet/rotazione
  reali per verifica su pianeti rotanti. Tilt assiale, spin, orbita, pianeti in
  moto supportati via re-publish ogni Tick (nessuna doppia applicazione).

---

## 7. LUT

Transmittance 256×(64×N), MultiScatter 32×(32×N), SkyView SS/MS 192×(112×N);
invalidazione planet-key (profile hash + Mie/Abs scales + terrain) e view-key
(cam heights quantizzate + SunDirPlanet + MSScale); single-active-atlas retained.
Nessuna rigenerazione inutile aggiunta; `FreezeLUTs` retained per diagnostica.

---

## 8. RUNTIME PROOF (§23 — evidenza reale, non dichiarazioni)

| Prova | Esito |
|-------|-------|
| Comandi `r.AndromedaAtmosphere.{Enable,DebugMode,Validate,Status,CaptureViews,SurfaceShot,FieldShot,OrbitShot}` realmente registrati | **PROVATO**: tutte le 8 stringhe + simboli `FUnifiedAtmosphereRenderer`/`FAndromedaAtmosphereSystem` presenti in `Binaries/Win64/UnrealEditor-Andromeda.dll` linkato da questo build (timestamp 2026-09-14 01:50, decode UTF-16/UTF-8) |
| Initialization | PROVATO a build-time (path init unico in `Andromeda.cpp`); log runtime atteso in PIE |
| Snapshot / GPU data / LUT / render pass / composite | Strumentati e reali (`Status` legge snapshot, version, contatori dispatch/LUT, frame info); esecuzione in PIE pendente |
| Doppia mailbox nel path attivo | **PROVATO via grep**: zero chiamate `FZephyrManager::Get()` / `FAndromedaAtmosphereManager::Get()` nel path attivo (restano solo impl + 2 `Clear()` difensivi legacy) |

Comandi PIE da eseguire (utente): `r.AndromedaAtmosphere.Validate` poi
`r.AndromedaAtmosphere.Status` → deve mostrare istanze > 0, star YES, LUT regens
> 0, aerial/sky > 0 dopo qualche frame in mappa con StarSystem.

---

## 9. LEGACY

- **DEPRECATED (compilano, fuori dal path):** `FAndromedaAtmosphereManager`,
  `FZephyrManager`, `FAndromedaAtmosphereViewExtension`,
  `FZephyrViewExtension`, `FLegacyAndromedaAtmosphereInstance` (rinominato per
  liberare il nome canonico), `UPlanetAtmosphereComponent`,
  `UPlanetAtmosphereRenderer`.
- **MERGED (demote a stage sotto proprietà unificata):**
  `FAndromedaAtmosphereRenderer`, `FZephyrRenderer` (logica pipeline intatta).
- **DELETED:** `TempAtmosphereTypes.h` (morto), `.bak`/`.TMP` (junk).
- **Rimozione fisica** (manager/component/hook legacy, rename `FZephyr*`,
  `AtmosEnabled`, spostamento shader): pianificata post-validazione visiva.

---

## 10. BUILD

```cmd
& "C:\Unreal Engine\UE_5.8\Engine\Build\BatchFiles\Build.bat" AndromedaEditor Win64 Development -project="C:\Users\aless\Documents\Unreal Projects\Andromeda\Andromeda.uproject" -waitmutex
```

- **Risultato: SUCCESS — 0 errors** (link `UnrealEditor-Andromeda.dll` ok).
- Note: un solo errore intermedio di compilazione (rename `replaceAll` aveva
  colpito anche `FAndromedaAtmosphereInstanceDesc`), corretto e rebuildato verde.
- Warning: solo toolchain non-preferito MSVC 14.51 (pre-esistente, non bloccante).

---

## 11. VALIDATION (PIE — da eseguire, protocollo pronto)

| Test | Comando / azione | Stato |
|------|------------------|-------|
| SPACE | vola via dal pianeta, `DebugMode 0` | PENDENTE (PIE) |
| INSIDE | teleport 50% shell | PENDENTE (PIE) |
| SUNRISE/SUNSET | `FieldShot <planet> 1` (terminator) | PENDENTE (PIE) |
| MULTI-PLANET | sistema multi-pianeta + `Status` (hash/coeff isolati) | PENDENTE (PIE) |
| ROTATION | `DebugMode 3` su pianeta rotante | PENDENTE (PIE) |
| ORBIT | osserva atmosfera seguire il pianeta | PENDENTE (PIE) |
| GOVERNING | camera tra 2 pianeti + `DebugMode 2` | PENDENTE (PIE) |
| Unified interaction | `r.AndromedaAtmosphere.Enable 0/1` (pass-through vs composito) | PENDENTE (PIE) |

---

## 12. KNOWN LIMITATIONS

1. Merge shader completo (aerial→LUT) = follow-up; Stage A retained con fisica
   Rayleigh-only + stella finita; gate `AtmosEnabled` retained.
2. `UPlanetAtmosphereComponent` ancora istanziato in `Planet.cpp:64` (solo
   marcato; rimozione post-validazione).
3. Alias `FAndromedaAtmosphereInstance/Profile` invece di rename fisico.
4. Validazione visiva PIE non eseguibile in ambiente headless (questa sessione).
5. `r.AndromedaAtmos.Enable` / `r.AndromedaZephyr.*` restano come stage-gate
   transitori (documentati, non pubblicizzati).

---

## FINAL VERDICT

```text
VALIDATED WITH LIMITATIONS
```

Struttura unificata + build 0-errori + comandi reali nel binario provati;
interazione visiva PIE e numeri multi-pianeta restano da eseguire con il
protocollo sopra. Nessun successo visivo dichiarato senza evidenza runtime.

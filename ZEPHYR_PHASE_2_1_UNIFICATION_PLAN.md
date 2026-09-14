# ZEPHYR / ATMOS — PHASE 2.1 UNIFICATION PLAN

## Unified Andromeda Atmosphere Rewrite

**Data:** 2026-09-14
**Stato:** ANALISI COMPLETATA — piano vincolante per l'implementazione
**Target:** Andromeda.uproject (UE 5.8)
**Reference READ-ONLY:** `C:\Users\aless\UnrealEngineSkyAtmosphere` (Hillaire, Case A only)
**Predecessore:** `ZEPHYR_PHASE_2_PORTING_MAP.md` (Phase 2.0 — coexistence ATMOS+ZEPHYR)

> Decisione architetturale: ATMOS e ZEPHYR vengono unificati in **UN SOLO sistema
> atmosferico nativo di Andromeda** con **UN SOLO renderer**, UN SOLO snapshot,
> UNA SOLA mailbox, UNA SOLA view extension, UN SOLO namespace console.

---

## 1. ESITO DELL'ANALISI — PERCHÉ L'ARCHITETTURA ATTUALE NON È FINALE

### 1.1 Sintomo osservato

> I comandi ZEPHYR non risultano disponibili durante PIE e l'atmosfera
> visivamente non cambia rispetto al vecchio ATMOS.

### 1.2 Cause radice identificate (verificate sui sorgenti)

| # | Causa | Evidenza |
|---|-------|----------|
| R1 | **Namespace console diverso da quello documentato.** La specifica (§21) richiede `r.AndromedaAtmosphere.*`, ma il codice registra `r.AndromedaZephyr.*` (`ZephyrRenderer.cpp:71-161`) e `r.AndromedaAtmos.*` (`AndromedaAtmosphereRenderer.cpp:42-104`). Chi digita i nomi della specifica ottiene `command not recognized`. | `ZephyrRenderer.cpp:71`, `AndromedaAtmosphereRenderer.cpp:42` |
| R2 | **Due renderer, due snapshot, due mailbox.** `FAndromedaAtmosphereManager` (handle-based, `AndromedaAtmosphereManager.h`) e `FZephyrManager` (mailbox, `ZephyrManager.h`) leggono le stesse `FPlanetRuntimeData` e pubblicano due snapshot indipendenti dallo stesso `AAndromedaAtmosphereRegistry`. | `AndromedaAtmosphereRegistry.cpp:103-119, 217, 313` |
| R3 | **Due hook Tonemap indipendenti.** `FAndromedaAtmosphereViewExtension` e `FZephyrViewExtension` si registrano separatamente su `EPostProcessingPass::Tonemap`. L'ordine di composizione dipende dall'ordine di init, non da un orchestratore. | `AndromedaAtmosphereViewExtension.cpp:41-48`, `ZephyrViewExtension.cpp:30-37` |
| R4 | **Fisica divergente.** ATMOS integra una stella puntiforme a distanza finita (`AndromedaAtmosphere.usf`, `ComputeStarTransmittance`, `StarPosition` camera-relative) mentre ZEPHYR usa la convenzione Hillaire Case A (stella direzionale, `SunDirPlanet`). Due modelli di illuminazione diversi sullo stesso sole. | `AndromedaAtmosphere.usf:402-517`, `ZephyrRenderer.cpp:1820-1833` |
| R5 | **ATMOS è Rayleigh-only.** `AndromedaAtmosphere.usf:28-29` dichiara esplicitamente: niente Mie, niente absorption, niente optical-depth LUT. ZEPHYR implementa Rayleigh+Mie+Absorption+MS. Stesso pianeta, due cieli diversi a seconda di quale pass domina il pixel. | `AndromedaAtmosphere.usf:22-29` |
| R6 | **Cooperazione per flag, non per design.** Il "no double limb" dipende da `AtmosEnabled` letto via `FindConsoleVariable` inside `ZephyrRenderer.cpp:1929-1942` + depth test. Funziona ma è un accoppiamento fra due sistemi che dovrebbero essere uno. | `ZephyrRenderer.cpp:1929-1942`, `ZephyrSky.usf:308-312` (cit. porting map) |
| R7 | **Component legacy ancora istanziati.** `APlanet` crea un `UPlanetAtmosphereComponent` per pianeta (`Planet.cpp:64`) con `UPlanetAtmosphereRenderer` statico single-planet (`PlanetAtmosphereRenderer.cpp:8-24`) — viola "senza componenti atmosferici piazzati" e "multi-planet". Non consumato né da ATMOS né da ZEPHYR. | `Planet.cpp:64`, `PlanetAtmosphereComponent.h`, `PlanetAtmosphereRenderer.h` |

### 1.3 Conclusione

Il Phase 2.0 ha prodotto due sistemi che coesistono "by construction". La Phase 2.1
li fonde in uno: **un solo snapshot full-physics, una sola mailbox, un solo
renderer proprietario, un solo hook, un solo namespace console**. La fisica retained
è quella Hillaire Case A (ZEPHYR); il pass ATMOS diventa *legacy aerial stage*
sotto proprietà unificata in attesa di assorbimento nel pass LUT (merge shader
tracciato come follow-up esplicito, non eseguito in questa fase).

---

## 2. INVENTARIO COMPLETO E CLASSIFICAZIONE

Legenda: **KEEP** = resta invariato · **MERGE** = confluisce nel sistema unificato ·
**REWRITE** = riscritto sotto proprietà unificata · **DEPRECATE** = resta compilante ma
disattivato dal path attivo, rimozione pianificata · **DELETE** = rimosso subito.

### 2.1 ATMOS

| File | Cosa fa | Perché utile / non utile | Responsabilità nel nuovo sistema | Sostituito da | Dipendenze | Verdetto |
|------|---------|--------------------------|----------------------------------|---------------|------------|----------|
| `Public/Atmosphere/AndromedaAtmosphereTypes.h` | `FAndromedaAtmosphereParameters` (Rayleigh+Mie campi, ma shader usa solo Rayleigh), `FAndromedaAtmosphereInstance(Desc)`, `FAndromedaAtmosphereGPUData` (16 float, center+star+Raleigh) | Utile come concetto (parametri serializzabili), inutile come snapshot: è un sottoinsieme impoverito del profilo ZEPHYR, senza absorption/density/albedo/rotation | Diventa **vista derivata** del profilo unificato (conversione in `RenderAtmospheres`, nessun path di scrittura) | `FAndromedaAtmosphereProfile` (= full-physics) + `AndromedaAtmosphereSystem` mailbox | Shader `AndromedaAtmosphere.usf` (layout) | **MERGE** (lettura sola) |
| `Public/Atmosphere/AndromedaAtmosphereManager.h/.cpp` | Registry handle-based thread-safe + star snapshot | Duplicato esatto della mailbox ZEPHYR, con modello dati più povero. Due scrittori → rischio divergenza | Nessuna (path di scrittura rimosso; lettura migrata al sistema unificato) | `FAndromedaAtmosphereSystem` | Registry, ATMOS renderer | **DEPRECATE** (compila, non più scritto dal Registry né letto dal renderer) |
| `Public/Atmosphere/AndromedaAtmosphereRegistry.h` + `Private/Atmosphere/AndromedaAtmosphereRegistry.cpp` | Actor auto-spawnato (via `AndromedaGameMode`), scopre `AStarSystem`, registra un'atmosfera per pianeta, aggiorna posizioni ogni Tick, pubblica snapshot ZEPHYR | **È già il punto di auto-registrazione** richiesto da §6/§18. Pecca: scrive in DUE mailbox con DUE convenzioni | Diventa l'**unico publisher** del sistema unificato (`SyncAtmospheres` → `FAndromedaAtmosphereSystem::SetSnapshot`) | — (evolve in place) | `AStarSystem`, `UZephyrProfileLibrary`, unified system | **KEEP + REWRITE parziale** (solo publish path) |
| `Public/Atmosphere/AndromedaAtmosphereRenderer.h` + `Private/...cpp` (793 righe) | Pass fullscreen Rayleigh-only (16-step march + tetrahedral L2), camera-relative, finite point-star | Utile: gestisce aerial perspective su geometria + limb, che lo sky-pass ZEPHYR NON fa (ritorna SceneColor su geometry pixels). Non-utile come sistema: fisica inferiore, stella finita vs Case A | Diventa **aerial stage interno** del renderer unificato, senza hook proprio, senza snapshot proprio | `FUnifiedAtmosphereRenderer::RenderAerialStage` (orchestratore; implementazione riusata) | Manager (→ unified), `AndromedaAtmosphere.usf`, view ext (→ unified) | **MERGE** (demote a stage) |
| `Private/Atmosphere/AndromedaAtmosphereViewExtension.h/.cpp` | Hook Tonemap per ATMOS | Duplicato dell'hook ZEPHYR | Nessuna (registrazione disabilitata) | `FUnifiedAtmosphereViewExtension` | ATMOS renderer | **DEPRECATE** |
| `Private/Atmosphere/AndromedaAtmosphereShader.h` | `FAndromedaAtmospherePS` param struct | Serve allo stage aerial finché vive | Resta finché lo stage esiste | (futuro: assorbito nel pass LUT) | `AndromedaAtmosphere.usf` | **KEEP** (transitorio) |
| `Shaders/Andromeda/AndromedaAtmosphere.usf` (1397 righe) | Integratore unificato sky+geometry+space, Rayleigh L1 + L2 tetraedrale, stella finita | Utile: aerial su geometria. Da superare: Rayleigh-only, stella finita (devia da Case A), phase non normalizzata (folded in exposure) | Stage shader legacy sotto renderer unificato | (futuro: aerial LUT-based in `ZephyrSky.usf`) | `FAndromedaAtmosphereGPUData` layout | **KEEP** (transitorio, documentato) |
| `Public/PlanetAtmosphereComponent.h` + `Private/PlanetAtmosphereComponent.cpp` | Component per-pianeta con `AtmosphereMaterial`, `StarWorldPosition`, `InitializeAtmosphere` | Viola §6/§27 (componenti/materiali manuali). Non letto da nessun renderer attivo. Istanza creata in `Planet.cpp:64` | Nessuna | Auto-registrazione via Registry | `UPlanetAtmosphereRenderer`, `APlanet` | **DEPRECATE** (istanziazione da rimuovere in fase successiva; ora solo marcato) |
| `Public/PlanetAtmosphereRenderer.h` + `Private/PlanetAtmosphereRenderer.cpp` | `UObject` single-planet con statici globali (`ActivePlanetWorldPosition`, …) | Single-planet globale = anti-multi-planet. Morto (nessun lettore) | Nessuna | Sistema unificato | Component sopra | **DEPRECATE** |
| `Public/Atmosphere/TempAtmosphereTypes.h` | Struct GPU obsoleta non riflessa | Morta, non inclusa da nessuno | Nessuna | — | Nessuna | **DELETE** |
| `Private/Atmosphere/AndromedaAtmosphereRenderer.cpp.bak` + `Public/PlanetAtmosphereRenderer.h~RFb1bba24.TMP` | Backup/editorial junk | Mai compilati, rumore | Nessuna | — | — | **DELETE** |

### 2.2 ZEPHYR

| File | Cosa fa | Perché utile / non utile | Responsabilità nel nuovo sistema | Sostituito da | Dipendenze | Verdetto |
|------|---------|--------------------------|----------------------------------|---------------|------------|----------|
| `Public/Planet/Zephyr/ZephyrTypes.h` | `FZephyrPlanetProfile` (full physics, hash FNV-1a), `FZephyrPlanetSnapshotEntry` (profile+center+ID+terrain+transition+rotation), `FZephyrPlanetGPUData` (7×float4, layout contract con `ZephyrCommon.ush`) | **È il modello dati corretto** (§6/§7): soddisfa tutti i requisiti dell'istanza atmosferica | Diventa il **modello dati unificato** via alias `FAndromedaAtmosphereProfile/Instance` (rename fisico in fase successiva) | — (promosso) | `ZephyrCommon.ush` layout, ProfileLibrary | **KEEP** (promosso a unified) |
| `Public/Planet/Zephyr/ZephyrManager.h` + `Private/.../ZephyrManager.cpp` | Mailbox game→render (Set/Get/Clear, versionata) | Corretta, ma è la SECONDA mailbox | Nessuna (logica mossa nel sistema unificato) | `FAndromedaAtmosphereSystem` (forwarder temporaneo) | Renderer, Registry | **DEPRECATE** (forwarder) |
| `Public/Planet/Zephyr/ZephyrRenderer.h` + `Private/.../ZephyrRenderer.cpp` (2650 righe) | Pipeline LUT Hillaire (Transmittance → MultiScatter → SkyView SS+MS → Sky PS), cache planet/view key, governing planet, SunDirPlanet, console `r.AndromedaZephyr.*`, capture helpers | **È il renderer corretto** (§11/12/13/15): Case A, dual-scattering documentato, LUT isolate per slice, dynamic max planets | Diventa **sky stage + LUT owner** del renderer unificato, senza hook proprio, leggendo lo snapshot unificato | `FUnifiedAtmosphereRenderer::RenderSkyStage` (orchestratore) | Manager (→ unified), `ZephyrShaders.h`, 5 shader, Registry | **MERGE** (promosso, demote hook) |
| `Public/Planet/Zephyr/ZephyrProfileLibrary.h` + `Private/...cpp` | `BuildProfile(seed, archetype, radii)`: baseline Terra + preset archetype + jitter SplitMix64 + column compensation + MieDensityScale disaccoppiato | **È il generatore data-driven** (§7): 10 archetype, deterministico, solo parametri fisici | Resta il builder ufficiale del profilo unificato | — | `ZephyrSharedAtmosphere.h`, `PlanetProfile.h` | **KEEP** |
| `Public/Planet/Zephyr/ZephyrSharedAtmosphere.h` | Costanti fisiche condivise (Rayleigh/Mie/absorption Terra, `CmToKm`, `ComputeSkyTransitionRadiusCm`) | Single source fisica, già usata da entrambi | Resta il riferimento fisico unificato (rename futuro in `AndromedaAtmosphereReference`, già namespacato così) | — | Nessuna | **KEEP** |
| `Private/Planet/Zephyr/ZephyrShaders.h` | 4 global shader (`TransmittanceCS`, `MultiScatterCS`, `SkyViewCS`, `SkyPS`) + param struct | Contratto RDG corretto | Resta invariato | — | `ZephyrTypes.h`, LUT | **KEEP** |
| `Private/Planet/Zephyr/ZephyrViewExtension.h/.cpp` | Hook Tonemap per ZEPHYR | Duplicato dell'hook ATMOS | Nessuna (registrazione disabilitata) | `FUnifiedAtmosphereViewExtension` | ZEPHYR renderer | **DEPRECATE** |
| `Shaders/Andromeda/Zephyr/ZephyrCommon.ush` | Math condivisa: densità esponenziali, effective scale, fasi normalizzate, mapping Hillaire, Fibonacci, atlas sampling half-texel | **Core matematico retained** (§11) | Resta (spostamento path in fase successiva) | — | Tutti gli `.usf` | **KEEP** |
| `Shaders/Andromeda/Zephyr/ZephyrTransmittance.usf` | Integrale 40-step + occultazione geometrica | Hillaire Transmittance LUT | Resta | — | Common | **KEEP** |
| `Shaders/Andromeda/Zephyr/ZephyrMultiScatter.usf` | Dual-scattering 8 dir Fibonacci × 8 step + ground bounce | Approssimazione ordine-2 stabile (§12) | Resta, documentata vs Hillaire iterativo | — | Common, Transmittance | **KEEP** |
| `Shaders/Andromeda/Zephyr/ZephyrSkyView.usf` | March 24-step front-to-back, V mapping horizon-concentrated, dual output SS+MS | Hillaire SkyView LUT | Resta | — | Common, T/MS LUT | **KEEP** |
| `Shaders/Andromeda/Zephyr/ZephyrSky.usf` | Governing planet selection + fade, SkyView sampling, sun disk, debug 0-8, gate `AtmosEnabled` | Renderer finale; il gate `AtmosEnabled` diventa assurdo sotto proprietà unificata (va rimosso quando lo stage aerial è assorbito) | Resta; gate mantenuto finché lo stage ATMOS esiste | — | Common, LUT atlas | **KEEP** (gate da rimuovere in follow-up) |

### 2.3 STARMAP / PLANETS (source of truth — mai riscritti, solo consumati)

| File | Ruolo | Verdetto |
|------|-------|----------|
| `Public/StarSystem.h` + `Private/StarSystem.cpp` (`FPlanetRuntimeData`, `SpawnPlanets`, `UpdatePlanetOrbits/Rotations`, `CalculatePlanetRotation`) | Generazione, orbite, rotazioni, snapshot read-only | **KEEP** (intoccabile, §27) |
| `Public/Planet/Planet.h` + `Private/Planet/Planet.cpp` | Actor pianeta, terrain, `PlanetSeed/Archetype` | **KEEP** (solo deprecazione component atmosfera) |
| `Public/Planet/PlanetProfile.h` (`EPlanetArchetype`, 10 archetipi) | Identità macroscopica → alimenta `BuildProfile` | **KEEP** |
| `Public/Sun.h` + `Private/Sun.cpp` (`bHideSunMeshForZephyrSky`) | Stella; mesh legacy nascosta, sole fisico = `StarSystem->GetActorLocation()` | **KEEP** |
| `Private/PlanetaryGravitySystem.cpp`, `PlanetaryLightingComponent`, generatori terreno/biomi | Non atmosferici | **KEEP** (fuori scope) |

### 2.4 Integrazione rendering / modulo

| File | Ruolo | Verdetto |
|------|-------|----------|
| `Source/Andromeda/Andromeda.cpp` (`StartupModule`: ATMOS init poi ZEPHYR init) | Doppia init, doppia registrazione differita | **REWRITE** (init unificata: system + unified renderer + unified commands) |
| `Source/Andromeda/Andromeda.Build.cs` | Dipendenze RenderCore/RHI/Renderer | **KEEP** |
| Console `r.AndromedaZephyr.*` / `r.AndromedaAtmos.*` | Esistono ma con nomi diversi dalla specifica | **KEEP** come stage-gate transitori + **NUOVO** namespace `r.AndromedaAtmosphere.*` (§21) che li governa |
| Hillaire `C:\Users\aless\UnrealEngineSkyAtmosphere` | Math reference | **KEEP** (read-only, mai modificato) |

---

## 3. ARCHITETTURA FINALE (UNIFICATA)

```text
STARMAP (AStarSystem)
   │  FPlanetRuntimeData per pianeta (PlanetID, Seed, Radius,
   │  TerrainHeight, WorldPosition, CurrentRotation, Orbit…)
   ▼
AAndromedaAtmosphereRegistry  (UNICO publisher, auto, Tick)
   │  UZephyrProfileLibrary::BuildProfile(seed, archetype, radii)
   ▼
FAndromedaAtmosphereSystem    (UNICA mailbox: snapshot full-physics + star + version)
   │  Game Thread scrive ── Render Thread legge
   ▼
FUnifiedAtmosphereRenderer    (UNICO renderer, UNICO hook Tonemap)
   ├── Stage A — Aerial/legacy (ATMOS raymarch, geometria+limb)
   │         legge snapshot unificato (vista Rayleigh derivata)
   └── Stage B — Sky/LUT (ZEPHYR: T → MS → SkyView → Sky PS)
             legge snapshot unificato (profilo full-physics)
   ▼
UE5.8 Tonemapper (single subscription, ordine deterministico A→B)
```

Follow-up pianificato (NON in questa fase): assorbire lo Stage A nel pass LUT
(aerial perspective via Transmittance LUT + depth), rimuovere `AtmosEnabled`,
spostare shader `Zephyr/*` → `Atmosphere/*`, rename fisico
`FZephyr*` → `FAndromedaAtmosphere*`, rimuovere file DEPRECATE.

---

## 4. détails per requisito §6–§23 (decisioni vincolanti)

- **§6 Istanza:** `FZephyrPlanetSnapshotEntry` promosso ad alias
  `FAndromedaAtmosphereInstance` (PlanetID, PlanetCenter, PlanetRotation,
  Ground/AtmosphereRadius, Profile, TerrainHeightCm, SkyTransitionRadiusCm;
  star a livello di sistema). Nessun Actor/Component/Material/LUT/Blueprint manuale.
- **§7 Profili:** `UZephyrProfileLibrary::BuildProfile` invariato (10 archetipi,
  jitter deterministico, column compensation, Mie disaccoppiato).
- **§8 Multi-planet:** `GetMaxPlanets()` dinamico (già implementato, retained);
  slice atlas + half-texel inset retained; chiavi planet/view retained.
- **§9 Governing planet:** `ZephyrSky.usf:181-322` (inside/transition/outside +
  limb fallback + fade) retained; diagnostica runtime in `Status` (distanze camera,
  stato, governing index dell'ultimo frame).
- **§10 Rotazione:** convenzione verificata `StarSystem.cpp:975-1044`
  (`CurrentRotation = (TiltQuat * SpinQuat).Rotator()`, tilt su Forward/X,
  spin su Up/Z). Trasformata `SunDirPlanet = Inv(PlanetRot) * SunDirWorld`
  (`ZephyrRenderer.cpp:1820-1833`) applicata **esattamente una volta** in
  `BuildGPUData`. Retained invariata.
- **§11 Hillaire:** Case A direzionale retained; niente B/C/D (§27).
- **§12 MS:** dual-scattering Fibonacci retained e documentato (non sostituito).
- **§14 UE5.8:** RDG + global shader + singola view extension + render thread +
  camera-relative double→float retained.
- **§15 LUT:** T + MS + SkyView SS/MS retained; invalidazione planet/view key
  retained; nessuna cache globale condivisa fra pianeti (slice isolate).
- **§16 Precisione:** `RelCenter = PlanetCenter − ViewOrigin` in double,
  narrow a float solo in `BuildGPUData` (entrambi gli stage) retained.
- **§21 Console (nomi finali, TUTTI realmente registrati):**
  `r.AndromedaAtmosphere.Enable`, `.DebugMode`, `.Validate`, `.Status` (+),
  `.CaptureViews`, `.SurfaceShot`, `.FieldShot`, `.OrbitShot` → forward ai
  comandi ZEPHYR esistenti (stessi args). I vecchi nomi restano come stage-gate.
- **§22 Debug 0-8 (reali):** 0 normal · 1 regions (governing/stato via Status +
  sky normale) · 2 selection (Status: governing + distanze) · 3 SunDirPlanet
  (Status: SunDirPlanet per pianeta) · 4 profile (Status: hash/coeff per pianeta) ·
  5 transmittance (→ Zephyr visual 1) · 6 SkyView (→ Zephyr visual 7) ·
  7 atlas/dataset (→ Zephyr visual 8) · 8 MultiScatter (→ Zephyr visual 3).
  Nessun modo documentato senza implementazione.
- **§23 Runtime proof:** `r.AndromedaAtmosphere.Status` stampa init, istanze,
  versione snapshot, star, contatori dispatch/LUT, governing ultimo frame,
  esito render pass — tutto da contatori reali, log controllato (no spam).

---

## 5. SEQUENZA IMPLEMENTATIVA (STEP A–L)

| Step | Azione | File | Verifica |
|------|--------|------|----------|
| A | Modello dati unificato (alias + enum debug + status struct) | `Public/Atmosphere/AndromedaAtmosphereSystem.h` (nuovo) | compile |
| B | Mailbox unica `FAndromedaAtmosphereSystem` | `Private/Atmosphere/AndromedaAtmosphereSystem.cpp` (nuovo) | compile |
| C | Renderer unificato (2 stage, 1 hook) + view extension unica | `Public/Atmosphere/AndromedaUnifiedAtmosphereRenderer.h`, `Private/Atmosphere/AndromedaUnifiedAtmosphereRenderer.cpp`, `Private/Atmosphere/AndromedaUnifiedAtmosphereViewExtension.h` (nuovi) | compile |
| D | Interfaccia shader: nessun cambio layout (§25 full-files ok); documenta contract | header commenti | compile |
| E | LUT/cache: proprietà documentata allo sky stage; nessun cambio algoritmico | commenti | compile |
| F | Registry → publish unificato | `AndromedaAtmosphereRegistry.cpp` rewrite publish | compile |
| G | Transforms: nessuna modifica (già corretto, §10) | — | review |
| H | UE5.8 path + console unificata | `Private/Atmosphere/AndromedaAtmosphereUnifiedCommands.cpp` (nuovo), `Andromeda.cpp` rewrite | compile |
| I | Hook legacy disabilitati; manager legacy deprecati; junk deletato | edit `HandlePostEngineInit`×2, `Shutdown`×2, `ZephyrRenderer.cpp`×3 call sites, `AndromedaAtmosphereRenderer.cpp` snapshot source | compile |
| J | Build `Build.bat … -waitmutex` | — | 0 errors |
| K | Runtime proof: comandi riconosciuti, Status, dispatch (richiede PIE utente) | `Status` output | log |
| L | Validazione visiva SPACE/INSIDE/SUNSET/MULTI/ROTATION/ORBIT/GOVERNING (richiede PIE utente) | screenshot | report |

---

## 6. RISCHI RESIDUI

| Rischio | Mitigazione |
|---------|-------------|
| Merge shader completo (aerial nel pass LUT) non eseguito | Stage ATMOS resta sotto proprietà unificata; gate `AtmosEnabled` retained; follow-up esplicito nel report |
| `UPlanetAtmosphereComponent` ancora istanziato in `Planet.cpp:64` | Solo marcato DEPRECATE; rimozione istanziazione dopo validazione visiva (cambio comportamentale) |
| Rename fisico `FZephyr*` → `FAndromeda*` | Solo alias in questa fase; rename dopo validazione (tocca 2650 righe + 5 shader) |
| Validazione PIE non eseguibile headless | Verdetto onesto `VALIDATED WITH LIMITATIONS`; protocollo TEST 1-8 nel report |

*Fine piano — ogni componente esistente classificato, nessuna implementazione
precede questo documento.*

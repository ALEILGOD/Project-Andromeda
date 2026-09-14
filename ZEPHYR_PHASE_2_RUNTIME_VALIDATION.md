# ZEPHYR PHASE 2.0 — RUNTIME VALIDATION REPORT

**Data:** 2026-09-14  
**Progetto:** Andromeda.uproject (UE 5.8)  
**Build:** SUCCESS (0 errori, 34 actions, ~70s)  
**Commit:** `git log --oneline -1` → da compilare durante test manuale  

---

## ⚠️ LIMITAZIONE AMBIENTE

**I test runtime richiedono Unreal Editor interattivo con GPU/display attivo.**  
L'ambiente corrente (headless/CI) non supporta:
- Inizializzazione RHI completa
- Post-process chain (Tonemap hook)
- Screenshot capture (`FScreenshotRequest`)
- PIE / Game viewport rendering

**Azione richiesta:** Aprire `Andromeda.uproject` in Unreal Editor 5.8 su workstation con GPU, caricare `Andromeda_Main`, entrare in PIE, eseguire i comandi sotto elencati.

---

## BUILD RESULT

| Metrica | Valore |
|---------|--------|
| **Build Status** | ✅ SUCCESS |
| **Errori** | 0 |
| **Warning** | Solo deprecation API UE5.8 (non bloccanti) |
| **Tempo build** | ~70s (full), ~8s (incremental) |
| **Shader compile** | Validated via `r.AndromedaZephyr.Validate` |

---

## TEST 1 — SPACE VIEW

**Obiettivo:** Camera nello spazio → atmosfera planetaria visibile con limb, scattering, nessun artefatto.

**Comandi:**
```cmd
r.AndromedaZephyr.Enable 1
r.AndromedaAtmos.Enable 0
r.AndromedaZephyr.DebugMode 0
r.AndromedaZephyr.Exposure 1.0
```
Poi: vola camera a distanza > 2x AtmosphereRadius dal pianeta.

**Criteri PASS:**
- [ ] Atmosfera visibile come sfera con limb brillante
- [ ] Colori sky emergono da scattering fisico (no gradienti artistici)
- [ ] Sun disk visibile con angular diameter corretto (~0.53°)
- [ ] Nessun artefatto: banding, bleeding, NaN, flicker
- [ ] Transizione space→atmosfera fluida

**Screenshot:** `r.AndromedaZephyr.CaptureViews 10 0` → salva `Zephyr_Mode0.png`

**Risultato:** ☐ PASS / ☐ FAIL / ☐ NOT RUN  
**Note:**

---

## TEST 2 — INSIDE ATMOSPHERE

**Obiettivo:** Camera dentro atmosfera → sky, horizon, scattering, transmittance corretti.

**Comandi:**
```cmd
r.AndromedaZephyr.SurfaceShot 0 90 0 0 0 0.5
```
(Teleporta pawn a 50% shell height sopra punto substellare, cattura debug mode 0)

**Criteri PASS:**
- [ ] Zenith: blu Rayleigh-dominato
- [ ] Horizon: più brillante (Mie forward lobe + path length)
- [ ] Sun disk: luminoso, attenuato da transmittance
- [ ] Nessun "buco" al nadir (ground albedo contribuisce a MS)
- [ ] Transmittance corretta guardando verso il sole vs opposto

**Screenshot:** `Zephyr_Mode0.png` da SurfaceShot

**Risultato:** ☐ PASS / ☐ FAIL / ☐ NOT RUN  
**Note:**

---

## TEST 3 — SUNRISE / SUNSET / TERMINATOR

**Obiettivo:** Verificare terminator, horizon glow, sky color, Rayleigh/Mie balance.

**Metodo A — Ruota pianeta:**
```cmd
# In PIE, ruota pianeta manualmente o attendi orbita
# Oppure usa console per modificare rotation rate temporaneamente
r.AndromedaZephyr.CaptureViews 5
```
Cattura sequence mode 0 (full sky) mentre sole tramonta/sorge.

**Metodo B — FieldShot (terminator preciso):**
```cmd
r.AndromedaZephyr.FieldShot 0 1 90 0 0
```
(Side=1 = terminator, yaw=90 = orizzontale, mode=0 = full radiance)

**Criteri PASS:**
- [ ] Terminator netto ma non hard-edge (scattering diffuso)
- [ ] Sunset colors: rosso/arancio da Rayleigh + absorption (Chappuis)
- [ ] Mie forward lobe: alone brillante intorno al sole
- [ ] Night side: buio (transmittance ~0), solo luce riflessa/ground albedo
- [ ] Transizione day→night fluida, no banding

**Screenshot:** `Zephyr_Mode0.png` (day), `Zephyr_Mode0.png` (sunset), `Zephyr_Mode0.png` (night)

**Risultato:** ☐ PASS / ☐ FAIL / ☐ NOT RUN  
**Note:**

---

## TEST 4 — MULTI-PLANET / MULTI-ATMOSPHERE  ⭐ CRITICO

**Obiettivo:** ≥2 pianeti simultanei con profili indipendenti, no LUT bleeding.

**Setup:** StarSystem con 2+ pianeti (es. Terran + Desert). Verificare in `StarSystemGenerator` o spawn manuale.

**Comandi:**
```cmd
r.AndromedaZephyr.Enable 1
r.AndromedaAtmos.Enable 0
r.AndromedaZephyr.DebugMode 0
```
Poi: vola camera per vedere entrambi i pianeti.

**Verifiche per-piàneta:**

| Verifica | Pianeta A (Terran) | Pianeta B (Desert) |
|----------|-------------------|-------------------|
| Profilo indipendente | Rayleigh base, Mie 1x | Mie ×4.5, Albedo 0.55, Abs ×1.4 |
| LUT slice separato | Slice 0 in atlanti | Slice 1 in atlanti |
| SunDirPlanet indipendente | Transform da Rotation A | Transform da Rotation B |
| Nessun bleeding | Colori non contaminati | Colori non contaminati |
| SkyTransitionRadius | Rg + Terrain + 2km | Rg + Terrain + 2km |

**Comandi diagnostici:**
```cmd
r.AndromedaZephyr.DebugMode 7  # SkyView LUT fullscreen - verifica 2 slice verticali
r.AndromedaZephyr.DebugMode 8  # MultiScatter LUT fullscreen - verifica 2 slice
r.AndromedaZephyr.DebugMode 1  # Transmittance LUT - verifica 2 slice
```

**Criteri PASS:**
- [ ] Due pianeti visibili simultaneamente con sky diversi
- [ ] Desert: horizon più brillante, Mie halo più forte, sunset più rossi
- [ ] Terran: sky più "standard Earth-like"
- [ ] `DebugMode 7/8/1` mostrano slice verticali distinte (no overlap)
- [ ] Cache keys includono profile hash + terrain height (no cross-invalidation)
- [ ] Zero manual Atmosphere Actor/Component/Blueprint creati

**Screenshot:** `r.AndromedaZephyr.CaptureViews 10 7` (SkyView LUT atlas), `... 8` (MS atlas)

**Risultato:** ☐ PASS / ☐ FAIL / ☐ NOT RUN  
**Note:**

---

## TEST 5 — PLANET ROTATION

**Obiettivo:** Rotazione pianeta modifica correttamente `SunDirPlanet` (frame locale).

**Comandi:**
```cmd
# Osserva pianeta in rotazione (StarSystem simulation attiva)
# Oppure forza rotazione via console se esposto
r.AndromedaZephyr.CaptureViews 30 0
```
Cattura sequence mentre pianeta ruota (es. 360° in ~30s se RotationPeriod breve).

**Verifica tecnica (codice):**
```cpp
// ZephyrRenderer.cpp:1820-1830
FVector SunDirWorld = (StarWorldPosition - Entry.PlanetCenter).GetSafeNormal();
FQuat PlanetRot = Entry.PlanetRotation.Quaternion();
SunDirPlanet = PlanetRot.Inverse().RotateVector(SunDirWorld);
```
- `PlanetRotation` = world-space rotation da `FPlanetRuntimeData.CurrentRotation`
- Applicata **una sola volta** in `BuildGPUData`
- Stella = Case A (directional) → `SunDirWorld` costante

**Criteri PASS:**
- [ ] Sole appare muoversi nel cielo del pianeta (non nello spazio)
- [ ] Day/night cycle corretto per rotation period
- [ ] Nessun "doppio giro" (sole fa 2 cicli per 1 rotazione pianeta)
- [ ] Terminator si sposta coerentemente con rotazione
- [ ] `SunDirPlanet` in shader (Packed6.xyz) ruota inversamente a `PlanetRotation`

**Screenshot:** Sequence `Zephyr_Mode0_*.png` durante rotazione

**Risultato:** ☐ PASS / ☐ FAIL / ☐ NOT RUN  
**Note:**

---

## TEST 6 — PLANET ORBITAL MOTION

**Obiettivo:** Atmosfera segue pianeta durante orbita, no drift.

**Comandi:**
```cmd
# In PIE, osserva pianeta in orbita attorno alla stella
# StarSystem OrbitTimeScale = 0.05 (default), periodo orbitale ~minuti
r.AndromedaZephyr.CaptureViews 60 0
```

**Verifiche:**
- [ ] `PlanetCenter` (camera-relative) aggiornato ogni frame via `PublishZephyrSnapshot()`
- [ ] `RelCenter = PlanetCenter - ViewOrigin` corretto in `BuildGPUData`
- [ ] Atmosfera "incollata" al pianeta, no lag/jitter
- [ ] Orbital velocity non influenza `SunDirPlanet` (stella = Case A, directional)
- [ ] Se camera segue pianeta: atmosfera stabile nel viewport

**Screenshot:** Sequence durante orbita

**Risultato:** ☐ PASS / ☐ FAIL / ☐ NOT RUN  
**Note:**

---

## TEST 7 — GOVERNING PLANET SELECTION

**Obiettivo:** Camera tra due pianeti → selezione corretta pianeta dominante.

**Logica (ZephyrSky.usf:174-321):**
1. **Inside shell** (CamDist ≤ Rt): closest ground (min CamDist - Rg)
2. **Transition zone** (Rt < CamDist ≤ Rs): closest ground + fade
3. **Deep space** (CamDist > all Rs): nearest ray hit on atmosphere shell

**Test manuale:**
```cmd
# Posiziona camera tra due pianeti vicini
# Sposta camera progressivamente da A verso B
r.AndromedaZephyr.DebugMode 0
```
Osserva transizione sky color.

**Criteri PASS:**
- [ ] Dentro atmosfera A → sky A
- [ ] Nella transition zone A → sky A con fade
- [ ] Nella transition zone B → sky B con fade
- [ ] Deep space → nearest limb (o sky del pianeta più vicino)
- [ ] Transizione fluida (smoothstep Rt→Rs), no pop
- [ ] Geometry occlusion: se geometry davanti → SceneColor passa through

**Screenshot:** Sequence durante spostamento camera

**Risultato:** ☐ PASS / ☐ FAIL / ☐ NOT RUN  
**Note:**

---

## TEST 8 — ATMOS + ZEPHYR INTERACTION  ⭐ CRITICO

**Eseguire TRE scenari separati:**

### 8A) ZEPHYR ONLY
```cmd
r.AndromedaZephyr.Enable 1
r.AndromedaAtmos.Enable 0
```
**Atteso:** Full sky + limb + sun disk da ZEPHYR. **Nessun** aerial perspective su geometry.

### 8B) ATMOS + ZEPHYR (BOTH ENABLED — DEFAULT)
```cmd
r.AndromedaZephyr.Enable 1
r.AndromedaAtmos.Enable 1
```
**Atteso (per regione):**

| Regione | ATMOS | ZEPHYR | Composito |
|---------|-------|--------|-----------|
| Space limb | **Owns** (unified integrator) | Yields (`AtmosEnabled` gate) | ATMOS limb |
| Outside/Inside atm | Aerial perspective on geometry | Sky pixels overwrite | ATMOS aerial + ZEPHYR sky |
| Horizon/Terminator | Rayleigh only | Full physics (R+M+Abs+MS) | ZEPHYR sky colors |
| Geometry front | Volume scattering | Passes through (depth test) | ATMOS only |

### 8C) ATMOS FALLBACK
```cmd
r.AndromedaZephyr.Enable 0
r.AndromedaAtmos.Enable 1
```
**Atteso:** Solo ATMOS — Rayleigh sky + aerial + limb. **Nessun** Mie, absorption, MS.

**Criteri PASS (8B - scenario produzione):**
- [ ] **No double limb**: space view mostra solo ATMOS limb
- [ ] **No double scattering**: horizon colors da ZEPHYR only (Mie+Abs+MS), non somma
- [ ] **Geometry occlusion**: terrain ha ATMOS aerial, sky ha ZEPHYR
- [ ] **Transizioni fluide**: space→surface→space no pop/discontinuità
- [ ] **Sun disk**: uno solo (ZEPHYR), attenuato da transmittance ZEPHYR

**Comandi debug per isolare:**
```cmd
r.AndromedaZephyr.DebugMode 2  # Solo Single Scatter (ZEPHYR)
r.AndromedaZephyr.DebugMode 3  # Solo Multi Scatter (ZEPHYR)
r.AndromedaAtmos.DebugVolume 1 # ATMOS volume overlay
```

**Screenshot per scenario:** `CaptureViews 10` per ciascuno

**Risultato 8A:** ☐ PASS / ☐ FAIL / ☐ NOT RUN  
**Risultato 8B:** ☐ PASS / ☐ FAIL / ☐ NOT RUN  
**Risultato 8C:** ☐ PASS / ☐ FAIL / ☐ NOT RUN  
**Note:**

---

## SHADER / RUNTIME ERRORS

**Durante test, monitorare Output Log per:**

| Errore | Azione |
|--------|--------|
| `[ZEPHYR-01] Shader infrastructure NOT VALIDATED` | Attendi shader compile, riesegui `r.AndromedaZephyr.Validate` |
| `RDG` / `RenderGraph` errors | Verifica texture dimensions, UAV bindings |
| `NaN` / `INF` in sky colors | Verifica `isfinite` checks in shader |
| `LUT cache` thrashing (regen ogni frame) | Verifica `r.AndromedaZephyr.FreezeLUTs 1` stabilizza |
| `AtmosEnabled` cvar non trovato | Verifica ATMOS inizializzato prima ZEPHYR |

**Log chiave attesi:**
```
[ZEPHYR-01] First sky dispatch live: N planet slice(s), star-linked, LUT-cached.
[ZEPHYR-01] LUT history cache invalidated (world cleanup).
[ZEPHYR-01] View extension registered: sky pass hooked to post-processing chain.
```

---

## PERFORMANCE OBSERVATIONS

**Durante test, registrare (stat gpu / RenderDoc / Pix):**

| Metrica | Valore Target | Misurato |
|---------|---------------|----------|
| **LUT Regen Time** (full) | < 10ms | ______ ms |
| **SkyView LUT Time** (per frame) | < 2ms | ______ ms |
| **Sky PS Cost** (fullscreen) | < 1ms | ______ ms |
| **VRAM LUT Atlases** (8 pianeti) | ~200 MB | ______ MB |
| **GPU Total Frame** (ZEPHYR only) | < 5ms | ______ ms |
| **Dispatch Count** (per frame) | 4 compute + 1 PS | ______ |

**Comandi:**
```cmd
stat gpu
r.AndromedaZephyr.FreezeLUTs 1  # Misura solo Sky PS + SkyView
r.AndromedaZephyr.FreezeLUTs 0  # Misura full pipeline incl LUT regen
r.AndromedaZephyr.Validate      # Dispatch/LUT counters
```

---

## BUG RESIDUI / ISSUES NOTI

| ID | Descrizione | Severità | Workaround |
|----|-------------|----------|------------|
| ZEPH-001 | Toy-planet column compensation può over-brighten Desert | Media | `r.AndromedaZephyr.MieScale 0.5` |
| ZEPH-002 | LUT regen on camera height change (view key) ogni frame se camera si muove verticalmente | Bassa | Quantizzazione height bucket (4096) già implementata |
| ZEPH-003 | Sun mesh legacy visibile se `bHideSunMeshForZephyrSky=false` | Bassa | Default `true` |
| ZEPH-004 | Shadow map slot 5 cleared ma non usato | Trivia | Future: planetary shadows |
| ZEPH-005 | Max 128 pianeti (texture limit) — oltre serve atlas paging | Media | Non blocca casi d'uso reali (<20 pianeti) |

---

## FIX APPLICATI DURANTE VALIDAZIONE

| File | Modifica | Motivo |
|------|----------|--------|
| `ZephyrRenderer.h` | `MaxPlanets=8` → `GetMaxPlanets()` dynamic | Rimuovo limite arbitrario |
| `ZephyrRenderer.cpp` | Implementazione `GetMaxPlanets()` | Basato su GPU max texture dim |
| `ZephyrSky.usf` | Rimosso `ZEPHYR_MAX_PLANETS`, usa `PlanetCount` | Coerenza CPU/GPU |
| `ZEPHYR_PHASE_2_PORTING_MAP.md` | Aggiunte sezioni C1, N1, O aggiornato | Documentazione interazione ATMOS, registrazione auto |

---

## FILE MODIFICATI (TOTALI)

### Core ZEPHYR (già presenti, non modificati in questa fase)
```
Source/Andromeda/Public/Planet/Zephyr/ZephyrTypes.h
Source/Andromeda/Public/Planet/Zephyr/ZephyrManager.h
Source/Andromeda/Public/Planet/Zephyr/ZephyrRenderer.h
Source/Andromeda/Public/Planet/Zephyr/ZephyrProfileLibrary.h
Source/Andromeda/Public/Planet/Zephyr/ZephyrSharedAtmosphere.h
Source/Andromeda/Private/Planet/Zephyr/ZephyrTypes.cpp
Source/Andromeda/Private/Planet/Zephyr/ZephyrManager.cpp
Source/Andromeda/Private/Planet/Zephyr/ZephyrRenderer.cpp   ← MODIFICATO (GetMaxPlanets)
Source/Andromeda/Private/Planet/Zephyr/ZephyrProfileLibrary.cpp
Source/Andromeda/Private/Planet/Zephyr/ZephyrShaders.h
Source/Andromeda/Private/Planet/Zephyr/ZephyrViewExtension.h/.cpp
Shaders/Andromeda/Zephyr/ZephyrCommon.ush
Shaders/Andromeda/Zephyr/ZephyrTransmittance.usf
Shaders/Andromeda/Zephyr/ZephyrMultiScatter.usf
Shaders/Andromeda/Zephyr/ZephyrSkyView.usf
Shaders/Andromeda/Zephyr/ZephyrSky.usf                      ← MODIFICATO (rimosso ZEPHYR_MAX_PLANETS)
```

### Integrazione Andromeda (già presenti)
```
Source/Andromeda/Andromeda.cpp                              ← MODIFICATO (init/shutdown ZEPHYR)
Source/Andromeda/Private/Atmosphere/AndromedaAtmosphereRegistry.cpp  ← MODIFICATO (PublishZephyrSnapshot)
Source/Andromeda/Public/Atmosphere/AndromedaAtmosphereRegistry.h     ← MODIFICATO (declaration)
Source/Andromeda/Private/Sun.cpp                            ← MODIFICATO (ConfigureSunMesh)
Source/Andromeda/Public/Sun.h                               ← MODIFICATO (bHideSunMeshForZephyrSky)
```

### Nuovi (questa sessione)
```
ZEPHYR_PHASE_2_PORTING_MAP.md
ZEPHYR_PHASE_2_RUNTIME_VALIDATION.md (questo file)
```

---

## VERDETTO FINALE

### ⬜ RUNTIME VALIDATED
Tutti i TEST 1-8 PASS, performance entro target, zero bug critici.

### ⬜ VALIDATED WITH LIMITATIONS
Test core PASS, limitazioni note documentate (es. toy-planet compensation, LUT regen frequency), nessun regresso.

### ⬜ FAILED
Uno o più test FAIL → richiede fix prima di considerare Phase 2.0 completa.

---

**Compilato da:** _________________  
**Data esecuzione test manuali:** _________________  
**Ambiente test:** GPU _________________, Driver _________________, UE 5.8  
**Firma:** _________________
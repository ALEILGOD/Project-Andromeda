# ZEPHYR PHASE 2.1 — NEAR-PLANET TRANSITION DIAGNOSIS

**Data:** 2026-09-14
**Sintomo:** in spazio i pass girano; avvicinandosi a un pianeta l'atmosfera
scompare e il cielo non parte; tutte le planete coinvolte.
**Metodo:** sola analisi statica del path (nessuna modifica al codice, nessuna
fix applicata — nemmeno la radiance-scale, già diagnosticata a parte).
**Geometria di riferimento (log utente, Planet 0):** `Rg ≈ 883510 cm`,
`Rt ≈ 1030102 cm` ⇒ shell ≈ 146592 cm ≈ **1.47 km** (toy planet).

---

## Observed behavior

1. SPACE: sistema inizializzato, aerial YES, sky YES, composite YES.
2. Avvicinamento: il segnale atmosferico visibile svanisce.
3. Vicino/dentro: nessun cielo da nessuno dei 7 pianeti.
4. I contatori restano YES (i pass GIRANO, l'output manca).

## Runtime state in SPACE

- Nessuna selezione inside/transition (CamDist > Rs per tutti) ⇒ pass 3
  (`ZephyrSky.usf:259-319`): raggi che colpiscono lo shell → limb; raggi che
  mancano → deep-space + sole.
- Gate limb (`ZephyrSky.usf:306-310`): `AtmosEnabled != 0` (default
  `r.AndromedaAtmos.Enable = 1`) ⇒ Zephyr restituisce SceneColor sul limb.
  **Il limb visibile in spazio è quello ATMOS** (raymarch Rayleigh con fase
  NON normalizzata × `ATMOS_RADIANCE_SCALE 8.0` ⇒ O(1), visibile).
- Quindi in spazio l'utente vede (correttamente, per costruzione transitoria)
  atmosfera = ATMOS limb; Zephyr fa passthrough.

## Runtime state near Planet

- Appena `CamDist ≤ Rs` (transition radius, FUORI dallo shell fisico), pass 2
  (`ZephyrSky.usf:218-257`) elegge il governante con fade 0→1: da qui lo sky
  stage **sovrascrive** i pixel sky con `SkyRadiance ≈ 10⁻²` (scala mancante,
  diagnosi precedente) ⇒ il cielo visibile ATMOS viene cancellato dal nero
  Zephyr **già fuori dall'atmosfera fisica**.
- Dentro lo shell (`CamDist ≤ Rt`), pass 1 (`ZephyrSky.usf:186-216`) elegge per
  minima altezza sul datum: stesso overwrite nero.
- Su pixel con depth (terreno): Zephyr passthrough (`ZephyrSky.usf:355-359`) ⇒
  l'haze aerial sul terreno sopravvive; i pixel sky restano neri.
- Netto avvicinandosi: sparizione GRADUALE poi totale del segnale visibile,
  mentre tutti i contatori restano YES. Coerente al 100% col sintomo.

## Governing planet analysis

Selezione (`ZephyrSky.usf:181-319`), tre stadi in priorità:

1. **Inside** (`CamDist ≤ Rt`, non sepolto): minima `CamDist − Rg`.
   Soglia d'ingresso atmosfera = **`AtmosphereRadius` (Rt)** — corretto.
2. **Transition** (`Rt < CamDist ≤ Rs`): minima altezza + fade smoothstep
   (`ZephyrCommon.ush:165-182`). `fade ≤ 0 ⇒ -1` (degenerate `Rs ≤ Rt` → hard
   step documentato).
3. **Deep space**: hit più vicino sullo shell + test ground + gate `AtmosEnabled`.

Distanze: `CamDist = length(RelCenter)`, `RelCenter` camera-relative in double
sulla CPU (`BuildGPUData`), narrowing a float solo nel buffer — nessuna
perdita di governo per precisione a queste scale (float ~0.06 cm a 10⁶ cm).

**Il governante NON diventa invalido in avvicinamento:** buried-skip
(`CamDist < Rg·(1−10⁻⁴)`) scarta solo camera dentro il corpo; vicino alla
superficie ma sopra il datum analitico l'elezione riesce (prova: governing 0
riportato dallo Status). Sotto il datum ma sopra la mesh del terreno (rilievi
più alti di `TerrainHeight` analitico) il bake SkyView scrive nero (buried,
`ZephyrSkyView.usf:83-88`) mentre il pixel shader elegge comunque → sky nero
anche con scala corretta: **caso limite reale ma secondario** (riguarda solo
camera sepolta sotto il datum, non l'avvicinamento normale).

## Atmosphere transition analysis

Percorsonormale atteso e verificato nel codice:

```text
SPACE → (pass 3, limb ATMOS / deep-space+sole)
  → cross Rs → (pass 2, fade 0→1, Zephyr sky faded-in)
    → cross Rt → (pass 1, Zephyr sky pieno)
      → SURFACE (haze aerial su depth + Zephyr sky)
```

**Il punto esatto di sparizione è `CamDist = Rs`** (Rs = Rg + terrain + 200000 cm,
fuori dallo shell): lì Zephyr inizia a sovrascrivere. La meccanica di
transizione (soglie, fade, priorità) è CORRETTA; ciò che sparisce è il segnale,
non il sistema. Nessuna condizione disabilita rendering in transizione:
`AtmosphereEnabled`/`SkyEnabled` non esistono come kill-switch nel path;
`AtmosEnabled` agisce solo sul limb deep-space; nessuno switch a legacy path;
nessuna esclusione del governante né degli altri pianeti.

## Sky pass analysis

Esegue sempre (nessun kill in avvicinamento); per i pixel sky produce
`SkySS+SkyMS+SunDisk ≈ 10⁻² × 1.0` ⇒ nero dopo tonemap. Causa già isolata:
**scala 24× assente** (diagnosi SkyView dedicata). Il bake dipende dalla camera
solo via `RelCenter` diretto (nessuna altezza stale); rigenera correttamente
al variare della quota (view-key).

## Aerial pass analysis

Esegue sempre; vicino alla superficie marcia `[EntryT, min(ExitT, TSurf.x,
GeometryT)]` correttamente (clamp geometrici verificati, nessun doppio
conteggio). Il suo cielo Rayleigh O(1) è aritmeticamente capace di output
visibile, ma sui pixel sky viene **sovrascritto dallo stage successivo**.
Sui pixel con depth sopravvive (passthrough Zephyr). L'aerial NON si
disattiva in avvicinamento: nessun gate di quota, `StarValid` stabile,
snapshot a 7 pianeti stabile.

## Final composite analysis

Un hook Tonemap, delegati Aerial→Sky; chaining UE deterministico. Lo sky stage
legge l'output aerial come SceneColor e sovrascrive i pixel sky. Output/restitu-
zione texture standard (`FScreenPassTexture(Output.Texture, Output.ViewRect)`),
provata dal DebugMode 5. Il composite FINALE è nero sui pixel sky **perché
l'ultimo scrittore (sky) scrive nero**, non per failure di plumbing.

## Unit/scale analysis (Planet 0: Rg = 883510, Rt = 1030102 cm)

| Quantità | CPU | Shader | Verdetto |
|----------|-----|--------|----------|
| Raggi/centri | cm (FVector/double) | cm→km UNA volta (`×1e-5`, `ZephyrCommon.ush:88-96`) | OK, nessuna doppia conversione |
| σ Rayleigh/Mie/Abs | km⁻¹ | × step in km (`CurStepLenKm`) | OK |
| `H01 = AltKm/ShellKm` | — | adimensionale | OK |
| `HCam01`, `MuSunG`, fade Rs | cm/cm | adimensionale | OK |
| Soglie `2e-3·Rg`, `1e-4` buried | cm / adim. | coerenti | OK |
| Rs = Rg+terrain+200000 | cm | vs `CamDistCm` | OK |
| Shell effettiva 1.47 km vs scale 8/1.2 km | compensation ×~15 in `DensityScale` | `EffectiveScale = min(scale, shell·0.35)` | OK by-design (colonna Earth-like ripristinata) |
| `ViewHeightKm·4096` (view-key) | per-frame durante l'approach ⇒ **rebake SkyView completo (7 slice) ogni frame** | corretto ma costoso: probabile hitch in avvicinamento, NON sparizione |

## Exact root cause

**NESSUN bug indipendente di transizione.** La meccanica SPACE→LIMB→
ATMOSPHERE→HORIZON→SURFACE (elezione, soglie Rt/Rs/Rg, fade, gate, clamp
aerial, overwrite ordinato) è tutta corretta e verificata riga per riga.
Il punto in cui "rendering funzionante" diventa "assente" è il **handoff a
`CamDist = Rs` dallo sky ATMOS (visibile, caldo) allo sky Zephyr (nero per la
scala 24× mancante)**, amplificato dall'overwrite totale dei pixel sky.

Confronto tipo punto 10 (`Distance < GroundRadius` vs `< AtmosphereRadius`):
**tutti corretti** — inside usa Rt, buried usa Rg, transition usa Rt..Rs, in
tutti e tre i consumatori (pixel shader, bake SkyView, aerial). Nessuna
inversione trovata.

Multi-pianeta (punto 11): entrare nello shell di Planet 0 cambia solo la
view-key ⇒ rebake completo ma corretto di tutte le slice; planet-key stabile;
slice isolate con half-texel inset; **nessuna invalidazione incrociata**.

## Evidence

- `ZephyrSky.usf:181-343` — elezione a 3 stadi, soglie Rt/Rs/Rg corrette.
- `ZephyrSky.usf:306-310, 355-359, 494-497` — gate limb, sky-mask, overwrite.
- `ZephyrCommon.ush:165-182` — fade 1→0 su Rt..Rs, step se degenere.
- `ZephyrSkyView.usf:83-88, 120-145` — buried-zero e march corretti.
- `ZephyrRenderer.cpp:1874-1915` — view-key include quote/sole ⇒ rebake in moto.
- `AndromedaAtmosphere.usf:912, 1391-1396` — aerial O(1) + composite.
- `AndromedaUnifiedAtmosphereRenderer.cpp` — wrapper senza gate di quota;
  contatori = invocazioni (pass EXECUTED ≠ output visibile).
- Sintomo spaziale (limb ATMOS visibile) + sparizione a Rs + contatori YES =
  firma esatta dell'handoff verso output nero, non di un kill.

## Minimal fix proposal (NON applicata)

**Nessuna fix di transizione necessaria.** La sparizione si risolve con la fix
radiance-scale già proposta (bake SkyView × `ZEPHYR_RADIANCE_SCALE`, 2 righe):
a quel punto l'handoff Rs consegna a uno sky luminoso e l'intera catena
SPACE→SURFACE diventa visibile senza toccare elezione, fade, gate o composite.

Controlli runtime a costo zero (conferma senza modifiche):

1. `r.AndromedaZephyr.Exposure 24` in avvicinamento → se l'atmosfera
   ricompare crescendo verso il pianeta, diagnosi chiusa.
2. `r.AndromedaZephyr.Enable 0` in avvicinamento → se resta il cielo/haze
   Rayleigh-only, aerial assolto definitivamente.
3. `r.AndromedaZephyr.FreezeLUTs 1` in avvicinamento → se spariscono gli hitch
   ma resta il nero, separato il tema perf (rebake/frame) dal tema segnale.

Log temporanei (punto 8): NON necessari — la causa è isolata staticamente con
firma sintomatica univoca; i campi richiesti esistono già in
`GetLastFrameInfo` + `Status` (governing, distanze, inside/outside) e nei
contatori. Se dopo la fix-scale restasse un residuo, il passo successivo è un
log throttled nel wrapper (camera, CamDist−Rg/Rt/Rs, fade, AtmosEnabled,
`bHasDepth` %) — snippet pronto su richiesta, da rimuovere dopo l'uso.

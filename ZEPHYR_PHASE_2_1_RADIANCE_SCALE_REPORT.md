# ZEPHYR PHASE 2.1 — RADIANCE SCALE REPORT

**Data:** 2026-09-14
**Task:** SkyView radiance calibration (solo scala, nient'altro)
**Target:** Andromeda.uproject (UE 5.8)

---

## 1. Root cause

`ZEPHYR_RADIANCE_SCALE = 24.0` definita in `ZephyrCommon.ush` ma applicata in
nessun punto: il bake SkyView scriveva radianza fisica ~10⁻² (fasi
normalizzate) e il pixel shader applicava solo `Exposure = 1.0`. Composite
nero a pipeline sana. Firma: Transmittance (0..1, senza bisogno di scala)
visibile in DebugMode 5, cielo nero in DebugMode 0 a parità di path.

## 2. Valore precedente della radiance

`SSAccum/MSAccum` puri (~10⁻² colonna Earth-like) scritti diretti nelle LUT;
nota dell'autore: già a scala 8 ⇒ ~0.1 "near-black"; a scala effettiva 1.0 ⇒
nero dopo tonemap.

## 3. `ZEPHYR_RADIANCE_SCALE`

24.0, `ZephyrCommon.ush:67`. Display gain documentato (rapporti, cromaticità e
zero-notte invariati). Valore invariato, nessuna seconda costante, nessun
duplicato.

## 4. Punti esatti del bake modificati

`Shaders/Andromeda/Zephyr/ZephyrSkyView.usf:225-226` (unico hunk):
`OutSkySingle ← SSAccum × SCALE`, `OutSkyMulti ← MSAccum × SCALE`.
Early `ZeroOut` (83-88/128-129/146-147) intoccati (marker no-sky, mai radianza).

## 5. Formula/operazione applicata

Moltiplicazione una tantum in uscita dal bake. J2 interna resta pre-scala
(coerente: la MS LUT alimenta solo il bake); SS+MS in pixel shader restano
sommati come prima, ora entrambi calibrati.

## 6. Exposure NON usato come fix

`r.AndromedaZephyr.Exposure` default 1.0 verificato e intatto
(`ZephyrRenderer.cpp:87-90`); `r.AndromedaAtmosphere.Exposure` non esiste e non
è stato creato. Usi `× Exposure` nel pixel shader invariati.

## 7. Scattering inalterato

Rayleigh/Mie/absorption/fasi/mu/mu_s/nu/transmittance/SkyView/dual-scatter/
Case A: zero modifiche. ZeroOut, marcia 24-step, guardia isfinite: intatti.

## 8. SunDirectionWorld non modificato

Nessun tocco a reference/snapshot/Packed6/packed comment oltre la calibrazione;
`Up`/`RayDir`/`SunDir` restano world (task precedente intatta).

## 9. Build result

```text
Build.bat AndromedaEditor Win64 Development … -waitmutex
Result: Succeeded — 0 errors (4 azioni: commands cpp + link)
```

Nota: gli `.usf` vivono in `Shaders/` e ricompilano al prossimo PIE, non nel
DLL. Prova binaria: stringa diagnostica Status + `SunReference` PRESENTI nel
DLL linkato (03:23). Prova file: righe 225-226 su disco verificate.

## 10. Runtime validation

PIE non eseguibile headless. Comandi registrati (prova binaria); `Validate` /
`Status` da eseguire in PIE inclusa la nuova riga di contratto scala;
verifica visiva ai punti 11-16, protocollo pronto.

## 11. Visual result — PENDENTE PIE (atteso: cielo visibile day-side)

## 12. Sunrise result — PENDENTE PIE

## 13. Sunset result — PENDENTE PIE

## 14. Terrain consistency — PENDENTE PIE (atteso: lato illuminato coincidente,
convention A condivisa già verificata staticamente)

## 15. Planet rotation result — PENDENTE PIE (view-key ora rotation-stabile:
meno rebake attesi durante lo spin)

## 16. Multi-planet result — PENDENTE PIE (slice/parallasse invariate)

## 17. Problemi residui

1. Validazione visiva PIE da eseguire (punti 11-16).
2. Rebake SkyView completo ogni frame in moto camera (view-key by-design):
   da osservare come hitch post-visibilità, senza nuovo sistema cache
   (fuori scope di questa task).
3. Debug diagnostici Mie/Rayleigh (gain ad-hoc 0.25/2.0) restano deboli —
   diagnostici, non cielo; eventuale allineamento futuro separato.
4. Problemi di direzione/governing/multi-planet, se emergono col cielo
   visibile, vanno riportati come task distinte (questa task: solo scala).

# Medienformate — .viv (BIGF) & .mad (Aufgabe 1.5)

**Status:** Magic + Grobstruktur dokumentiert (nur soweit für HD relevant)
**Datum:** 2026-10-14

> Für das HD-Textur-Ziel (Phase 2) sind diese Formate **nicht** kritisch – sie sind reine
> Medien-Container (Audio/Video), keine Texturen. Daher hier nur Magic + Struktur + Tool-Hinweis.

---

## 1. `.viv` — BIGF-Container (Audio)

Beispiel: `GameData/Sounds/sd1a.viv`, `GameData/Music/zzzymus.viv`.

| Offset | Länge | Endian | Bedeutung |
|---|---|---|---|
| 0 | 4 | – | Magic `BIGF` |
| 4 | 4 | **big** | Gesamt-Dateigröße |
| 8 | 4 | big | Anzahl enthaltene Dateien |
| 12 | 4 | big | Verzeichnis-/Header-Ende |
| 16 | 4 | big | (zweiter Offset / Datenstart) |
| … | | big | Verzeichnis: je Eintrag `name` (8.3, null-terminiert) + `länge` (u32) + `offset` (u32) |

Enthält eingebettete EA-Sounddateien: `.bnk` (Sound-Bank), `.cct`, `.clt`, `.cpt`, `.wav` usw.
Verifiziert an `sd1a.viv`: `BIGF` + Größe 457692 (= reale Dateigröße) + 6 Einträge
(`sd1a.bnk`, `sd1a.cct`, `sd1a.clt`, …).

**Tool-Hinweis:** BIGF wird in OpenNFS referenziert
(`src/Loaders/nfs3_loader.cpp`, `nfs4_loader.cpp`, `src/Util/Utils.cpp`) – dort ist der
Container-Parsing-Code als Referenz vorhanden. Eigenes Extraktions-Tool ist bei Bedarf trivial
(magic + big-endian-Verzeichnis).

## 2. `.mad` — MAD (EA-Video/Audio)

Beispiel: `FEData/Movies/ch1.mad` … `ch6.mad`, `EArts-av.mad`, `er*pc-av.mad`.

| Offset | Länge | Bedeutung |
|---|---|---|
| 0 | 4 | Magic `MADk` (`4D 41 44 6B`) |

Danach binärer MAD-Stream (Electronic-Arts-Videoformat; die `.mad`-Dateien in `Movies/` tragen
die Spiel-Intros/Cutscenes, bis ~18 MB). Für HD-Texturen irrelevant; volle Spezifikation steht aus.

**Tool-Hinweis:** Für MAD-Video-Wiedergabe existieren Community-Tools (ffmpeg-Builds mit
EA-MAD-Demuxer); im Workspace nicht enthalten und aktuell nicht benötigt.

---

## Einordnung

| Format | Inhalt | HD-Relevanz |
|---|---|---|
| `.viv` (BIGF) | Sound/Musik-Bänke | keine |
| `.mad` (MAD) | Introvideos/Cutscenes | keine |
| `.fsh`/`.env` (SHPI/QFS) | **Texturen** (s. `SHPI.md`) | **hoch** |

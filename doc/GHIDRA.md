# GHIDRA.md – Ghidra-Funktionskarten

> Themen-Dokument: Aufbau und Validierung der Ghidra-Exporte. Status und Aufgaben stehen in `PLAN.md`.
> **Validiert:** 2026-10-13.

## 1. Exporte

| Modul | Karte | Funktionen | Bildbereich (PE-Header) |
|---|---|---|---|
| `gimme.dll` | `GhidraNFSP/export/gimme.dll/function_map.{csv,jsonl}` | 5768 (99 % dekompiliert) | `0x10000000–0x105FA000`, alle Funktionen im Bild ✓ |
| `porsche.exe` | `GhidraNFSP/export/porsche.exe/function_map.{csv,jsonl}` | 5841 (99 % dekompiliert) | `0x400000–0x6E3000`, alle Funktionen im Bild ✓ |

- **CSV ist das Hauptformat** (mit grep durchsuchbar) und versioniert. Die JSONL-Dateien haben identischen Inhalt (~25 MB) und bleiben lokal.
- Export-Skript: `GhidraNFSP/ExportNFS5FunctionMap.java`.
- Schema: `entry_address, end_address, size_bytes, function_name, called_functions, calling_functions, strings, decompiled`. `size_bytes` schließt die Endadresse ein (`end − start + 1`).

## 2. Validierung

- **Binary-Version = Mod-Zielversion:** Der PE-Header von `Game CD\Porsche.exe` (`ImageBase 0x400000`, `SizeOfImage 0x2E3000`) stimmt exakt mit `EXPECTED_IMAGE_BASE/SIZE` in `addresses.h` überein.
- **CSV ≡ JSONL:** gleiche Adressmengen, keine Duplikate.
- **Abdeckung:** 99 von 110 bekannten Hook-Stellen aus `addresses.h` liegen in kartierten Funktionen. Der Shape-Parser von `gimme.dll` (RVA `0x1054A0`) ist als `FUN_101054a0` vorhanden.

## 3. Bekannte Schwächen

1. **Analyselücken:** 11 verifizierte Hook-Stellen liegen in keiner erkannten Funktion: `0x42E577`, `0x432BB6/B6/D9/C6B/CB0/CFF/D3A` (Screen-Size-Block), `0x4CBBD9/F4`, `0x53ABA0`, `0x53B450`. Der Export ist nicht gefiltert (vom Nutzer bestätigt). Abhilfe: in Ghidra an diesen Adressen manuell Funktionen anlegen (`F`), neu analysieren und neu exportieren.
2. **Funktionsgrenzen nicht blind vertrauen:** `FUN_101054a0` endet nach 358 Byte, der Mod behandelt den Shape-Parser aber als Funktion von `0x2AA4` Byte (`GIMME_SHAPE_SIZE`). Grenzen immer gegen die Byte-Patterns aus `addresses.h` / `silent_compat.cpp` prüfen.
3. **Aufrufkonvention nicht blind vertrauen:** Ghidra zeigt cdecl-Funktionen als `__thiscall`, wenn ECX zufällig durchgereicht wird (`FUN_004ECF00`, `FUN_004FC0B0` enden mit `ret`). Vor jedem Hook das Funktionsende prüfen: `ret` = cdecl, `ret N` = thiscall/stdcall mit N/4 Stapel-Argumenten (LFSP v7k/v7l stürzten deshalb ab, `VERKEHR.md` §4).
4. **Keine eigenen Namen:** nur Ghidra-Standardnamen (`FUN_…`, Importe, `FID_conflict:`-Dubletten). Die semantische Benennung für B1–B3/B6 steht noch aus.

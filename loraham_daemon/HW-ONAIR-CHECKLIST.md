# Multi-Hardware: On-Air-Checkliste für die Hardware-Session (Maintainer)

Software-seitig grün heißt NICHT on-air-korrekt; diese Liste ist der manuelle
Abnahmetest am Funkgerät. Alle RF-Übungen nur mit legalen Frequenz-/Leistungs-/
Duty-Cycle-Einstellungen (≤ 20 dBm).

## STATUS (Session 2026-07-16/17, Gegenstation MeshCom-T-Deck/SX1262 + Meshtastic-868)

- **M3 A/B: BESTANDEN**, beide Bänder, A==B (Zeile 1 als Ein-Paket-Verfahren;
  Zeile-6-BUSY-in-Airtime auf B nachgewiesen). Befund: A-only kosmetische
  Pin-25-Reclaim-Meldung bei Rekonfiguration; B sauber.
- **M4 Waveshare LF/433: BESTANDEN** mit 2 Befunden: RF-Switch-Polarität
  (gefixt, `dc213b0`, Zeile 10 = Regressionswache hat gegriffen) und
  FSK-RXBW-Raster (gefixt: familienbewusste Validierung). Long-Packets 12×230 B
  ohne Hänger (Zeile 5 TX-Richtung; RX-Richtung mit Textnachrichten nicht
  darstellbar). Zeile 2 LoRa-APRS-Satz + echte SX127x-Gegenstation: OFFEN
  (T-Deck ist SX1262; Cross-Family-RX-Nachweis existiert aus M3 Zeile 1).
  Zeile 11 entfällt (Kombination Waveshare+Uputronics: nicht geplant).
  HF/868-Variante: nicht vorhanden, per Analogie abgedeckt (README).
- **Uputronics-Stack (CE0+CE1): BESTANDEN** (Init/Warmstart, CADSCAN=0-Pfad,
  RX/TX beide Bänder, Parallel-Last 16/16, Duplikat-Exit 3, LED-Slots+Polarität
  aktiv-high verifiziert). Befund: LDRO-Warmstart-Geist (gefixt, `be43e1d`);
  Hardware-Lehre: 433/868-Antennen waren vertauscht (~50 dB, symmetrisch).

## STATUS (Release-Abnahme v112, 2026-07-18, main @ `ad86639`) — ALLE DREI STACKS BESTANDEN

v112 (Merge der Härtungsarbeit) on-air abgenommen auf LoRaHAM, Uputronics und
Waveshare/SX1262. Alle Rows der Abschnitte „Mode-Switch/RX" und „Audit-Abschluss"
grün auf realer Hardware; Gegenstationen MeshCom-T-Deck (433) und
Meshtastic (868). Ergebnisse in `~/loraham-bench/*RESULTS.md`.

- **LoRaHAM (SX1278/RFM95): BESTANDEN** — beide Bänder RX+TX (433 MeshCom, 868
  Meshtastic), CONF-Replies, Band-Politik, Airtime-Gate, GPIO-Locks, SIGTERM.
- **Uputronics CE0/CE1 (SX127x, kein DIO1): BESTANDEN** — disjunkte Pin-Locks
  {6,8,25}/{7,13,16}, **CADSCAN=0 Passiv-CAD-Pfad** (schlüssel), RX+TX beide
  Bänder, alle CONF/Politik/Airtime-Rows. LDRO=0 für MeshCom Pflicht (kein
  RESET); sauber mit gesetztem LDRO=0.
- **Waveshare SX1262: BESTANDEN** — Init mit TCXO 1.8 V (kein CHIP_NOT_FOUND),
  Pin-Locks {6,16,18,20,21}, **CADSCAN=1** (hat DIO1). **RF-Switch-Fix (F1)
  bestätigt: SX1262 STRAHLT AB** — TX auf T-Deck angezeigt (alter
  TX_RESULT-OK-ohne-RF-Bug tot). SX1262-Familienprüfung nur hier möglich:
  `OOK=0` UND `=1` → `ERR INVALID`; `ENCODING=1`/`FREQDEV=0.1` → `ERR INVALID`.
  Langpaket 215 B angezeigt; Live-RSSI GetRssiInst (PACKETRSSI −41 vs
  LIVERSSI −98). 868/HF-Variante nicht vorhanden (per Analogie).
- **Prozess-Hinweis:** `pgrep -f "radio 433"` matcht die aufrufende Shell →
  SIGTERM killt die Test-Shell; immer `pgrep -x loraham_daemon` verwenden.

## M3 — RadioDriver-Extraktion (A/B gegen den Vor-Treiber-Stand `834841a`)

Ziel: `--radio 433` / `--radio 868` (LoRaHAM-Profil, `--hw loraham`) verhalten sich mit dem
RadioDriver byte-identisch zum Stand vor der Extraktion. A = `834841a`,
B = M3-HEAD; jede Zeile auf beiden Ständen ausführen und vergleichen.

| # | Test | Erwartung A == B |
|---|---|---|
| 1 | RX-Decode: bekannte Gegenstation (LoRa-APRS 433.900 SF12), 10 Pakete | identische Dekodierung, RSSI/SNR-Metadaten plausibel, `RX`-Zähler in `GET STATS` |
| 2 | TX raw DATA (beide Bänder), Gegenstation dekodiert | `TX=1/0`-Broadcasts, `TXOK`-Zähler, Inhalt korrekt empfangen |
| 3 | TX framed + `SET TXRESULT=1`: MANAGED (Queue) und DIRECT | `TX_RESULT` OK; MANAGED: flags Bit0 gesetzt; DIRECT: Bit0 leer |
| 4 | CAD-Timeout-Pfad: Kanal mit Dauerträger/Traffic belegen, MANAGED TX | `CHANNEL_BUSY` nach `CADWAIT` (Default 1500 ms); mit `SET CADTXAFTERTIMEOUT=1` Sendung nach Timeout + Flag Bit2 |
| 5 | CAD-Monitor: `SET CADMONITOR=1`, echten Traffic erzeugen | `CAD=1`-Flanke bei Aktivität, `CAD=0` erst nach Hysterese (2 Samples ≥3 dB unter `CADRSSI`) |
| 6 | `GET CHANNEL` in Ruhe und während fremdem Träger | `CADSCAN=1`, `CADSTATE=FREE/BUSY` korrekt; während eigenem TX: `BUSY=1 CADSTATE=UNAVAILABLE` |
| 7 | GETRSSI-Stream: Träger ein-/ausschalten | Pegelsprung sichtbar, Werteplateau plausibel (Offset −164/−157 LF/HF) |
| 8 | MODE-Wechsel LoRa→FSK→LoRa per CONF, danach RX/TX | Wechsel ohne Absturz, RX-Callback wieder aktiv, Parameter-Apply im jeweiligen Modus |
| 9 | `GET STATUS`/`GET STATS` nach Testlauf | Feldreihenfolge und Zählerstände identisch zu A |
| 10 | LED-Verhalten (LoRaHAM, `--hw loraham`): RX-Blitz, TX/CAD-aktiv | identisches Blinkbild |

## M4 — SX1262 (Waveshare LoRaWAN Node HAT, LF und HF)

Vorbereitung: `dtoverlay=spi0-0cs`, HAT aufgesteckt, Start mit
`--radio <band> --hw waveshare-sx1262`. Erwartung ohne HAT: `begin()` −2,
eine Diagnosezeile, Daemon beendet sich fail-closed (softwareseitig getestet).

| # | Test | Erwartung |
|---|---|---|
| 1 | Init mit HAT (beide Bandbindungen: LF-Variante 433, HF-Variante 868) | `Init OK`, `GET STATUS RADIO=READY`; TCXO-Spannung (DIO3) wirksam, kein `CHIP_NOT_FOUND` |
| 2 | Sync-Word-Kompatibilität explizit: SX1262 ↔ SX127x-Gegenstation, `SYNC 0x12` (LoRa-APRS) und `0x2B` (MeshCom-Raster) | beide Richtungen dekodieren; wenn nicht: RadioLib-Mapping (Steuerbits 0x44) als erste Verdächtige dokumentieren |
| 3 | SX1262→SX127x TX, beide Parametersätze (LoRa-APRS 433.900/SF12/BW125; MeshCom 433.175/SF11/BW250/CR6) | Gegenstation dekodiert; Frequenzablage/Drift im Rahmen (TCXO) |
| 4 | SX127x→SX1262 RX, gleiche Sätze | RX_PACKET mit plausiblen RSSI/SNR-Metadaten |
| 5 | Long-Packet-Serie: ≥50 Pakete à 200–255 Byte in beide Richtungen (§1.3-Hinweis: meshtasticd-Longpacket-Probleme auf diesem HAT) | 0 Verluste jenseits der Funkstrecke, keine Hänger; BUSY-Timeouts im Log = Befund |
| 6 | MANAGED TX + CAD: Kanal belegen, `CHANNEL_BUSY`-Pfad; `GET CHANNEL` | SX126x-scanChannel liefert BUSY/FREE korrekt, `CADSCAN=1` |
| 7 | GETRSSI-Stream + CAD-Monitor (`SET CADMONITOR=1`, `CADRSSI` variieren) | Pegel plausibel (GetRssiInst), Flanken wie beim SX127x |
| 8 | TX-Leistungsklemme: `SET POWER=0/10/20` | Übernahme grün; Ausgangsleistung messbar gestaffelt (Chip-Bereich −9…+22) |
| 9 | FSK: `MODE=FSK`, BR/FREQDEV/RXBW (SX126x-Raster!), SHAPING, ENCODING; `SET OOK=1` | Parametersatz wirksam; OOK deutlich abgelehnt; RXBW-Fremdwerte rot |
| 10 | TXEN/ANT_SW (BCM 6): TX-Ausgangsleistung mit/ohne korrekt gesetztem RF-Switch | volle Leistung nur mit `setDio2AsRfSwitch` + TXEN-Pfad (Regressionswache für die RF-Switch-Verdrahtung) |
| 11 | Kombination: Waveshare-Prozess + Uputronics-CE0-Prozess parallel | zweiter Prozess scheitert NICHT auf Funkpins; LED-Konflikt BCM 6 gemäß README-Matrix (CE0-LED-Reassign offen) |

## v112-Härtung — Mode-Switch- & RX-Integritäts-Fixes

Bench-Nachweis für diese Verhaltensänderungen; beide Bänder, Profil wie
verbaut. B8 aus M3 wird hiermit verschärft (Frequenz-Nachweis statt nur
"kein Absturz").

| # | Test | Erwartung |
|---|---|---|
| 1 | `SET MODE=FSK` dann `SET MODE=LORA` (868-Prozess), danach RX von bekannter Gegenstation auf 869.525 | Empfang funktioniert ohne weitere SET-Kommandos — Band-Boot-Defaults aktiv, kein 434-MHz-Chip-Default |
| 2 | Wie 1 auf 433 (LoRa-APRS 433.900 SF12) | dito; `GET STATUS` zeigt Band-Defaults |
| 3 | MODE-Wechsel unter Last: RX-Traffic während des Wechsels | kein Absturz, RX-Callback wieder aktiv, nächstes Paket dekodiert |
| 4 | CAD-Probe bei anliegendem, noch nicht abgeholtem Paket (Traffic + gleichzeitiges MANAGED TX) | Paket wird dekodiert (kein RX-Verlust durch Probe), TX wartet als BUSY/`CHANNEL_BUSY` |
| 5 | `GET STATS` nach Testlauf | `RXREARMFAIL=0` im Normalbetrieb; Feld vorhanden und angehängt (Altparser unbeeinflusst) |

## v112-Härtung — Audit-Abschluss (Deployment & CONF-Protokoll)

**STATUS 2026-07-18: alle Rows 1–8 BESTANDEN** auf LoRaHAM + Uputronics +
Waveshare (Row 1 disjunkte Pin-Locks; Row 2 Exit 3 + gehaltener gpio-Lock →
Exit 4 live; Row 3 CONF-Replies inkl. `set txqueue=1` klein → OK; Row 5/6
Band-Politik + Airtime on-air; Row 7 Dual-Pfad-Client rssi_dualbar; Row 8
SIGTERM sauber). Zusätzlich nur auf Waveshare/SX1262 nachweisbar: `OOK=0`/`=1`
→ `ERR INVALID`, `ENCODING=1`→`ERR INVALID`, `FREQDEV=0.1`→`ERR INVALID`,
`CADSCAN=1`, RF-Switch strahlt ab (TX auf T-Deck).

Voraussetzung Variante A (systemd): Gruppe+Nutzer anlegen (README "systemd
deployment"), tmpfiles ausführen, Units aktivieren. Variante B (direkt/lhpc):
`LORAHAM_SOCKET_DIR=/tmp` beim Daemon-Start; Clients finden beide Welten
automatisch.

| # | Test | Erwartung |
|---|---|---|
| 1 | Boot of both bands (either variant): `[GPIO] pin locks held: ...` vor LED/Init | disjoint pin lists per band, init OK, RADIO=READY |
| 2 | Zweiter Start desselben Bands | Exit 3 (Instanz); gehaltener fremder gpio-Lock → Exit 4, kein Restart-Spin unter systemd |
| 3 | CONF-Replies via socat: gültiges `SET CADWAIT=1500` / ungültiges `SET CADWAIT=1` / `set txqueue=1` (klein) / `BOGUS` | genau eine Antwort je Zeile: `OK` / `ERR INVALID` / `OK` / `ERR UNKNOWN`; `GET STATUS` einzeilig ohne `OK`, mit `RXREADY=1` |
| 4 | TXQUEUE-Drain: Queue mit Jobs füllen (framed TX Serie), sofort `SET TXQUEUE=0` | `ERR BUSY` solange Jobs laufen; nach Drain `OK`; direkter DATA-TX während Rest-Queue → BUSY |
| 5 | Band-Politik on-air: 433-Prozess `SET FREQ=869.525` | `ERR INVALID` (off-band, Log nennt band policy); RF unverändert auf 433 |
| 6 | Airtime-Gate: `SET BW=7.8` (bei SF12) | `ERR INVALID` mit Airtime-Ausgabe im Log; `SET SF=7 BW=7.8` → `OK` |
| 7 | Clients (chat/iGate/rssi_dualbar) gegen beide Varianten (systemd-Sockets UND /tmp-Sockets) ohne Rebuild | verbinden automatisch, RX/TX-Verkehr normal |
| 8 | SIGTERM unter Last (MANAGED TX aktiv) | sauberer Stopp < TimeoutStopSec 30 s, Sockets entfernt, Locks frei |

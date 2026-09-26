![LoRaHAM_Pi](https://github.com/LoRaHAM/LoRaHAM_Pi/blob/main/LoRaHAM_logo.png?raw=true)

# LoRaHAM_Daemon - Gerätetreiber

*English: [README.md](README.md)*

**LoRaHAM_Daemon ist der Hardware-Treiber für LoRa-Funkmodule am Raspberry Pi.** Er übernimmt das
per SPI angebundene Funkmodul und stellt es über UNIX-Sockets bereit, sodass eine Anwendung senden,
empfangen und konfigurieren kann, ohne die Hardware selbst anzufassen — LoRa mit hoher
Sendeleistung und großer Reichweite auf einem Einplatinencomputer, für Funkamateure.

Geschrieben ist er für den **LoRaHAM_Pi HAT** und die **LoRaHAM Cartridge**, unterstützt aber auch
die Uputronics Pi Zero LoRa Boards und den Waveshare SX1262 HAT. Andere Boards sollten ebenfalls
funktionieren: nötig sind ein SX1276, SX1278 oder SX1262 direkt am SPI-Bus des Pi und ein
`--hw` Preset, dessen Verdrahtung passt.

<img src="https://github.com/LoRaHAM/LoRaHAM_Pi/blob/main/LoRaHAM_P1_3.jpg" alt="LoRaHAM_Pi" width="300" height="auto"><img src="https://github.com/LoRaHAM/LoRaHAM_Ressources/blob/main/LoRaHAM_Cartridge_for_pi500.png" alt="LoRaHAM Cartridge" width="300" height="auto">

## Inhalt

* [Hardware](#hardware)
* [Drei Wege, es zu betreiben](#drei-wege-es-zu-betreiben)
* [Eigene Clients schreiben](#eigene-clients-schreiben)
* [Warnungen](#warnungen)
* [Credits und Lizenz](#credits-und-lizenz)

## Hardware

* Raspberry Pi 3/4/5, Raspbian Image
* SPI aktiviert mit dem `spi0-0cs` Overlay — [wie](docs/hardware.md)
* 433 MHz und 868 MHz, ein Daemon-Prozess pro Band

Unterstützte Funkmodule, ausgewählt mit `--hw`:

| `--hw` | Board |
|---|---|
| `loraham` | **[LoRaHAM_Pi HAT / LoRaHAM Cartridge](https://www.loraham.de/produkt/loraham-pi/)** — das Dual-Modul-Board des [LoRaHAM-Projekts](https://loraham.de), SX1278 für 433 MHz + RFM95 für 868 MHz |
| `uputronics-ce0`, `uputronics-ce1` | [Uputronics Raspberry Pi Zero LoRa Expansion Board](https://store.uputronics.com) — RFM95/RFM98; ein Board für ein Band, oder zwei gestapelt für Dualband. Das Preset wählt Chip-Select und Pins, nicht das Band: das bestimmt `--radio`, also kann jedes Board jedes Band bedienen |
| `waveshare-sx1262` | [Waveshare SX1262 LoRaWAN/GNSS HAT](https://www.waveshare.com/wiki/SX1262_XXXM_LoRaWAN/GNSS_HAT) — Varianten 433M und 868M. **Nur SPI**: der Daemon steuert den SX1262 direkt an, der GNSS-Empfänger des HAT bleibt ungenutzt und dies ist kein LoRaWAN-Gateway- oder Konzentrator-Betrieb. Kein FSK-Streaming und keine Board-LED, und keine Kombination mit einem Uputronics-Board — die Pins kollidieren |

Jeder HAT, dessen Funkchip ein **SX1276**, **SX1278** oder **SX1262** ist und direkt am SPI-Bus des
Pi hängt, sollte funktionieren — wählen Sie das Preset, dessen Verdrahtung passt. Verdrahtung,
Fähigkeiten, die Kombinationsmatrix und wie man ein Board ergänzt stehen in
[`docs/hardware.md`](docs/hardware.md).

## Drei Wege, es zu betreiben

### Mit LoRaHAM_Pi Control

**[LoRaHAM_Pi Control (LHPC)](https://github.com/makrohard/loraham-pi-control) installiert und
betreibt das alles für Sie**, über eine Web-Oberfläche, auf einer Box zum Flashen und Vergessen.
Es gibt **[fertige SD-Karten-Images](https://github.com/makrohard/loraham-images)** — Image
schreiben, booten, Web-UI öffnen, Hardware auswählen, Stack starten. LHPC baut den Daemon aus
diesem Repository, pinnt ihn auf einen getesteten Commit und verwaltet Funkmodule, Sockets und
systemd-Units.

Die neun Stacks:

| Stack | Was es ist |
|---|---|
| LoRaHAM daemon | der Daemon allein, ein Prozess pro Band |
| LoRaHAM Chat | die APRS/Chat-TUI aus [`clients/`](clients/), auf 433 |
| LoRaHAM Voice | Codec2-Sprache über LoRa, GTK auf dem Desktop oder ncurses auf einer Lite-Box |
| LoRaHAM KISS TNC | KISS/TCP-TNC auf Port 8001 für APRS-Clients, optional serielles PTY |
| Graywolf APRS | APRS-Station mit Web-UI, Digipeater und iGate, über den KISS-TNC |
| Meshtastic | natives `meshtasticd` auf dem RF95, rootless betrieben |
| MeshCom | MeshCom-Firmware unter QEMU, an den Daemon gebrückt |
| MeshCore | MeshCore auf openHop, 868, Chat-Node und/oder Repeater |
| Reticulum | RNS-Node, der das Funkmodul direkt über SPI ansteuert |

### Den Daemon selbst bauen

[`loraham_daemon/`](loraham_daemon/) ist der aktiv weiterentwickelte Baum: ein Prozess pro Band,
Instanz- und GPIO-Sperren, CAD/LBT, framed DATA-Protokoll, systemd-Units und Testsuite. Bauen und
starten in drei Befehlen — [`loraham_daemon/README.md`](loraham_daemon/README.md).

Die Referenzdokumentation liegt in [`docs/`](docs/) (englisch):

| | |
|---|---|
| [Hardware](docs/hardware.md) | Pakete, RadioLib, SPI, Board-Presets, Verdrahtung, zwei Boards an einem Pi |
| [Deployment](docs/deployment.md) | systemd, Benutzer und Gruppen, Sockets im Dateisystem, Sperren, Exit-Codes |
| [Kommandozeile](docs/cli.md) | Alle Optionen und die RF-Startwerte je Band |
| [CONF-Protokoll](docs/conf-protocol.md) | Funkmodul zur Laufzeit konfigurieren: Kommandos, Parameter, Abfragen, Antworten |
| [DATA-Protokoll](docs/data-protocol.md) | Die rohen und die framed Wire-Formate |
| [Grenzwerte und Timing](docs/limits.md) | Größen, Timeouts, TX-Modi, CAD-Verhalten |
| [Architektur](docs/architecture.md) | Modulkarte und Laufzeit-Design |
| [Beispiele](docs/examples.md) | Kurze Programme, die mit dem Daemon sprechen — Shell, Python, C |

### Die archivierten Daemons

[`archive/`](archive/) enthält die Einzeldatei-Daemons, beide Bänder in einem Prozess. Sie bleiben
verfügbar und baubar; [`archive/README.md`](archive/README.md) enthält alles Nötige:
Voraussetzungen, RadioLib, Kompilierbefehle und Bedienung.

### Clients

In [`clients/`](clients/) liegen die Programme, die mit dem Daemon sprechen — die
[Chat-TUI](clients/chat/README.md), das [APRS-iGate](clients/igate/README.md), Dualband-RSSI-Balken
und ein Meshtastic-Decoder. Zuerst den Daemon starten, dann einen Client.

Mitarbeit am Code: [`CONTRIBUTING.md`](CONTRIBUTING.md) (englisch).

## Eigene Clients schreiben

Der Daemon ist ein Gerätetreiber mit Socket-Schnittstelle: Alles, was einen UNIX-Socket öffnen
kann, kann das Funkmodul ansteuern — ohne SPI, ohne RadioLib, und ohne C, sofern man nicht möchte.

[**Beispiele**](docs/examples.md) (englisch) enthält die kürzesten Programme, die wirklich etwas
tun: den Daemon mit einer `socat`-Zeile nach seinem Zustand fragen, aus einem Shell-Einzeiler
senden, dann Empfangen und Senden in Python, C und JavaScript. Jeder Befehl und jedes Programm
dieser Seite wurde gegen einen echten Daemon auf echter Hardware ausgeführt; die gezeigten
Ausgaben sind das, was zurückkam.

Damit anfangen, die Nutzlast ändern und `GET CHANNEL` beobachten, während man eine Antenne bewegt.

## Warnungen

Dieser Code wird auf eigenes Risiko und eigene Verantwortung zur Verfügung gestellt. Dieser Code ist experimentell.
Er ist nur für Funkamateure oder Labore geeignet.

## Credits und Lizenz

    Copyright (c) 2020-2026 Alexander Walter
    refactored by makrohard Johannes Loose 410733@gmail.com
    Licensed under the GNU GPL v3 (see LICENSE)

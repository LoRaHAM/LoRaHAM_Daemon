![LoRaHAM_Pi](https://github.com/LoRaHAM/LoRaHAM_Pi/blob/main/LoRaHAM_logo.png?raw=true)

# LoRaHAM_Daemon (english)

LoRaHAM_Daemon is a Raspberry Pi software driver for the hardware upgrade project LoRaHAM_Pi and LoRaHAM Cartridge for 
radio amateurs to enable LoRa with high transmission power and thus long range on a single board computer.

First code for the LoRaHAM Pi hardware | https://www.loraham.de/produkt/loraham-pi/


<img src="https://github.com/LoRaHAM/LoRaHAM_Pi/blob/main/LoRaHAM_P1_3.jpg" alt="LoRaHAM_Pi" width="300" height="auto"><img src="https://github.com/LoRaHAM/LoRaHAM_Ressources/blob/main/LoRaHAM_Cartridge_for_pi500.png" alt="LoRaHAM Cartridge" width="300" height="auto">

* Raspberry Pi 3/4/5
* Raspbian Image on RPi

# Need follow parts on the Raspberry Pi image:

    sudo apt update
    sudo apt install g++ make cmake build-essential -y
    sudo apt install liblgpio-dev -y
    sudo apt install libncurses5-dev libncursesw5-dev -y
    sudo apt install socat -y

    git clone https://github.com/jgromes/RadioLib ~/RadioLib

    cd ~/RadioLib
    mkdir build/
    cd build
    cmake ..
    sudo make install

# Configure your Raspberry Pi Hardware Interface:

    sudo raspi-config nonint set_config_var dtparam=spi on /boot/firmware/config.txt # Enable SPI
    
    # Ensure dtoverlay=spi0-0cs is set in /boot/firmware/config.txt without altering dtoverlay=vc4-kms-v3d or dtparam=uart0
    sudo sed -i -e '/^\s*#\?\s*dtoverlay\s*=\s*vc4-kms-v3d/! s/^\s*#\?\s*(dtoverlay|dtparam\s*=\s*uart0)\s*=.*/dtoverlay=spi0-0cs/' /boot/firmware/config.txt
    
    # Insert dtoverlay=spi0-0cs after dtparam=spi=on if not already present
    if ! sudo grep -q '^\s*dtoverlay=spi0-0cs' /boot/firmware/config.txt; then
        sudo sed -i '/^\s*dtparam=spi=on/a dtoverlay=spi0-0cs' /boot/firmware/config.txt
    fi

# Compile instruction
loraham Daemon:

    g++ -o loraham_daemon loradaemon_305d.cpp -I/home/raspberry/RadioLib/src -I/home/raspberry/RadioLib/src/modules \
    -I/home/raspberry/RadioLib/src/protocols/PhysicalLayer /home/raspberry/RadioLib/build/libRadioLib.a -llgpio

lorachat: 

    gcc lorachat_ncurses_113.c -o loraham_chat -lncurses -lpthread

loraham iGate:

    gcc -Wall -o loraham_igate loraham_iGate_105d.c

# Use instructions:
1. first run the LoRaHAM Daemon because this is the interface between hardware (LoRaHAM_Pi HAT or LoRaHAM Cartridge) and users programm
2. then the LoRaHAM iGate (OVERWATCH) or PiGate
3. or you can run the LoRaHAM Chat (but not with iGate)


yes you can run both programms, but the iGate will set Frequency at every TX and RX and your Chat will do the same.
But Chat use the RX-Frequency from iGate to transmitt and the TX-Frequency from the iGate to receive.
That will collide.
You can read on the Chat all incoming RF tranmissions to your iGate.

1. ./loraham_daemon
2. ./loraham_igate
3. ./loraham_chat

Daemon and iGate can also run as real daemon (parameter -d):
1. ./loraham_daemon -d
2. ./loraham_igate -d

if you dont run loraham_daemon as a daemon, you see all traffic on your terminal!

iGate options:

    -c CALL      Rufzeichen
    -t TX_FREQ   TX Frequenz
    -r RX_FREQ   RX Frequenz
    -i SEK       Intervall IS
    -f SEK       Intervall RF
    -L LAT       Beacon Lat
    -O LON       Beacon Lon
    -R km        filter only pass stations arround xy kilometers
    -x LAT       filter from LAT
    -y LON       filter from LON (you can also set a station arround xy kilometers from other location different from yours)
    -S Symbold   Symbol of your map icons device
    -d           Run daemon in background

Example:

     ./loraham_igate -c DB0ABC-10 -t 433.900 -r 433.775 -L 4827.72N -O 00957.94E -f 600 -i 1200 -d
 
# Background information
loraham_daemon opens 4 IPC (inter process communication) UNIX-Sockets:

    - DATA868_SOCKET "/tmp/lora868.sock"
    - DATA433_SOCKET "/tmp/lora433.sock"
    - CONF868_SOCKET "/tmp/loraconf868.sock"
    - CONF433_SOCKET "/tmp/loraconf433.sock"
    
On the config sockets, you can send a simple text string to configurate the LoRa-Module from your programm:

    - "SET FREQ=433.900 SF=12 BW=125 CR=5 CRC=1 PREAMBLE=8 SYNC=0x12 LDRO=1 POWER=17"
    - "SET FREQ=869.525 SF=11 BW=250 CR=5 CRC=1 PREAMBLE=16 SYNC=0x2B LDRO=1 POWER=10"

You can send this also from your terminal via socat:

    - echo "SET FREQ=433.900 SF=12 BW=125 CR=5 CRC=1 PREAMBLE=8 SYNC=0x12 LDRO=1 POWER=17" | socat - UNIX-CONNECT:/tmp/loraconf433.sock
    - echo "SET FREQ=869.525 SF=11 BW=250 CR=5 CRC=1 PREAMBLE=16 SYNC=0x2B LDRO=1 POWER=10" | socat - UNIX-CONNECT:/tmp/loraconf868.sock
 
 thats the config for 433 LoRa-APRS and 868 Meshtastic in Ulm/Dornstadt Germany ;-)


# Warnings
This code is provided at your own risk and responsibility. This code is experimental.
For radio amateur or laboratory use only.

# Credits and license

    Copyright (c) 2020-2026 Alexander Walter
    Licensed under GPL v3 (text)
    Maintained by Alexander Walter 
    
This project is licensed under **GNU GPL v3** with additional commercial restrictions:

    * **Private & Hobby:** Use is free of charge. Modifications must be reported to the author (via Pull Request).
    * **Commercial:** Any use in a business environment or for profit is **prohibited without a paid commercial license**.
    * **Redistribution:** Binaries may only be distributed alongside the full source code.
    * **Liability:** Software is provided "as is". The author is not liable for any damages.
 
         ******************************************************************************
         * Copyright (C) 2026  [LoRaHAM / Alexander Walter]
         * * LICENSE: GNU General Public License v3 (GPLv3) with the following terms:
         * 1. PRIVATE/HOBBY: Free use, modification, and redistribution for non-commercial
         * purposes is permitted.
         * 2. COMMERCIAL: Commercial or business use is STRICTLY PROHIBITED unless a
         * written license is obtained from the author for a fee (Dual-Licensing).
         * [CONTACT: loraham.de Email Contact]
         * 3. CODE MAINTENANCE: Any modifications to this code must be reported to the
         * author (preferably via Pull Request on GitHub).
         * 4. REDISTRIBUTION: Binaries may only be distributed alongside the full
         * source code (Copyleft).
         * * --- DISCLAIMER OF WARRANTY & LIMITATION OF LIABILITY ---
         * THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
         * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
         * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
         * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
         * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
         * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
         * THE SOFTWARE. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE
         * PROGRAM IS WITH THE USER.
     ******************************************************************************


![LoRaHAM_Pi](https://github.com/LoRaHAM/LoRaHAM_Pi/blob/main/LoRaHAM_logo.png?raw=true)

# LoRaHAM_Daemon (deutsch)

LoRaHAM_Daemon ist eine Raspberry Pi Software für das Hardware-Upgrade-Projekt LoRaHAM_Pi und LoRaHAM Cartridge für Funkamateure, um LoRa mit hoher Sendeleistung und somit großer Reichweite auf einem Einplatinencomputer zu ermöglichen.

Erster Code für die LoRaHAM Pi Hardware | https://www.loraham.de/produkt/loraham-pi/

<img src="https://github.com/LoRaHAM/LoRaHAM_Pi/blob/main/LoRaHAM_P1_3.jpg" alt="LoRaHAM_Pi" width="300" height="auto"><img src="https://github.com/LoRaHAM/LoRaHAM_Ressources/blob/main/LoRaHAM_Cartridge_for_pi500.png" alt="LoRaHAM Cartridge" width="300" height="auto">

* Raspberry Pi 3/4/5
* Raspbian Image auf RPi

# Folgende Pakete werden auf dem Raspberry Pi Image benötigt:

    sudo apt update
    sudo apt install g++ make cmake build-essential -y
    sudo apt install liblgpio-dev -y
    sudo apt install libncurses5-dev libncursesw5-dev -y
    sudo apt install socat -y
    
    git clone https://github.com/jgromes/RadioLib ~/RadioLib
    
    cd ~/RadioLib
    mkdir build/
    cd build
    cmake ..
    sudo make install

# Konfiguriere die SPI-Hardware des Raspberry Pi:

    sudo raspi-config nonint set_config_var dtparam=spi on /boot/firmware/config.txt # Enable SPI
    
    # Ensure dtoverlay=spi0-0cs is set in /boot/firmware/config.txt without altering dtoverlay=vc4-kms-v3d or dtparam=uart0
    sudo sed -i -e '/^\s*#\?\s*dtoverlay\s*=\s*vc4-kms-v3d/! s/^\s*#\?\s*(dtoverlay|dtparam\s*=\s*uart0)\s*=.*/dtoverlay=spi0-0cs/' /boot/firmware/config.txt
    
    # Insert dtoverlay=spi0-0cs after dtparam=spi=on if not already present
    if ! sudo grep -q '^\s*dtoverlay=spi0-0cs' /boot/firmware/config.txt; then
        sudo sed -i '/^\s*dtparam=spi=on/a dtoverlay=spi0-0cs' /boot/firmware/config.txt
    fi

# Kompilieranweisung
loraham Daemon:

    g++ -o loraham_daemon loradaemon_305d.cpp -I/home/raspberry/RadioLib/src -I/home/raspberry/RadioLib/src/modules \
    -I/home/raspberry/RadioLib/src/protocols/PhysicalLayer /home/raspberry/RadioLib/build/libRadioLib.a -llgpio

lorachat: 

    gcc lorachat_ncurses_113.c -o loraham_chat -lncurses -lpthread

loraham iGate:

    gcc -Wall -o loraham_igate loraham_iGate_105d.c

# Bedienungsanleitung:
1. Zuerst den LoRaHAM Daemon starten, da dies die Schnittstelle zwischen der Hardware (LoRaHAM_Pi HAT oder LoRaHAM Cartridge) und dem Benutzerprogramm ist.
2. Dann das LoRaHAM iGate (OVERWATCH) oder PiGate.
3. Oder Sie können den LoRaHAM Chat ausführen (aber nicht zusammen mit dem iGate).

Ja, Sie können beide Programme ausführen, aber das iGate wird die Frequenz bei jedem TX und RX setzen und Ihr Chat wird dasselbe tun.
Zudem nutzt der Chat die RX-Frequenz vom iGate zum Senden und die TX-Frequenz vom iGate zum Empfangen.
Das wird kollidieren.
Sie können im Chat alle eingehenden Funkübertragungen an Ihr iGate mitlesen.

1. ./loraham_daemon
2. ./loraham_igate
3. ./loraham_chat

Daemon und iGate können auch als echter Daemon laufen (Parameter -d):

1. ./loraham_daemon -d
2. ./loraham_igate -d

Wenn Sie loraham_daemon nicht als Daemon ausführen, sehen Sie den gesamten Datenverkehr in Ihrem Terminal!

iGate Optionen:

    -c CALL      Rufzeichen
    -t TX_FREQ   TX Frequenz
    -r RX_FREQ   RX Frequenz
    -i SEK       Intervall IS
    -f SEK       Intervall RF
    -L LAT       Beacon Lat
    -O LON       Beacon Lon
    -R km        Filter: Nur Stationen im Umkreis von xy Kilometern durchlassen
    -x LAT       Filter von LAT
    -y LON       Filter von LON (Sie können auch eine Station im Umkreis von xy Kilometern von einem anderen Standort als Ihrem eigenen festlegen)
    -S Symbol    Symbol für Ihr Map-Icon Gerät
    -d           Daemon im Hintergrund ausführen

Beispiel:

     ./loraham_igate -c DB0ABC-10 -t 433.900 -r 433.775 -L 4827.72N -O 00957.94E -f 600 -i 1200 -d
 
# Hintergrundinformationen
loraham_daemon öffnet 4 IPC (Inter-Process Communication) UNIX-Sockets:

    - DATA868_SOCKET "/tmp/lora868.sock"
    - DATA433_SOCKET "/tmp/lora433.sock"
    - CONF868_SOCKET "/tmp/loraconf868.sock"
    - CONF433_SOCKET "/tmp/loraconf433.sock"
    
Über die Konfigurations-Sockets können Sie eine einfache Textzeichenfolge senden, um das LoRa-Modul aus Ihrem Programm heraus zu konfigurieren:

    - "SET FREQ=433.900 SF=12 BW=125 CR=5 CRC=1 PREAMBLE=8 SYNC=0x12 LDRO=1 POWER=17"
    - "SET FREQ=869.525 SF=11 BW=250 CR=5 CRC=1 PREAMBLE=16 SYNC=0x2B LDRO=1 POWER=10"

Sie können dies auch von Ihrem Terminal über socat senden:

    - echo "SET FREQ=433.900 SF=12 BW=125 CR=5 CRC=1 PREAMBLE=8 SYNC=0x12 LDRO=1 POWER=17" | socat - UNIX-CONNECT:/tmp/loraconf433.sock
    - echo "SET FREQ=869.525 SF=11 BW=250 CR=5 CRC=1 PREAMBLE=16 SYNC=0x2B LDRO=1 POWER=10" | socat - UNIX-CONNECT:/tmp/loraconf868.sock
 
Das ist die Konfiguration für 433 LoRa-APRS und 868 Meshtastic in Ulm/Dornstadt, Deutschland ;-)

# Warnungen
Dieser Code wird auf eigenes Risiko und eigene Verantwortung zur Verfügung gestellt. Dieser Code ist experimentell.
Er ist nur für Funkamateure oder Labore geeignet.

# Credits und Lizenz

    Copyright (c) 2020-2025 Alexander Walter
    Licensed under GPL v3 (text)
    Maintained by Alexander Walter 
    
Dieses Projekt ist unter der **GNU GPL v3** lizenziert, jedoch mit spezifischen Bedingungen für die kommerzielle Nutzung:
    
    * **Privat & Hobby:** Die Nutzung ist kostenlos. Änderungen müssen dem Urheber mitgeteilt werden (via Pull Request).
    * **Kommerziell:** Jede Nutzung in einem geschäftlichen Umfeld oder zur Gewinnerzielung ist **genehmigungspflichtig und kostenpflichtig**. 
    * **Weitergabe:** Binärdateien dürfen nur zusammen mit dem Quellcode verbreitet werden.
    * **Haftung:** Die Software wird "wie besehen" bereitgestellt. Der Urheber übernimmt keine Haftung für Schäden.
        
        ******************************************************************************
         * Copyright (C) 2026 [LoRaHAM / Alexander Walter]
         * * LIZENZ: GNU General Public License v3 (GPLv3) mit den folgenden Bedingungen:
         * 1. PRIVAT/HOBBY: Die freie Nutzung, Änderung und Weiterverbreitung für 
         * nicht-kommerzielle Zwecke ist gestattet.
         * 2. KOMMERZIELL: Die kommerzielle oder geschäftliche Nutzung ist STRENGSTENS 
         * UNTERSAGT, sofern keine schriftliche Lizenz vom Autor gegen Gebühr erworben 
         * wurde (Dual-Licensing).
         * [KONTAKT: loraham.de E-Mail Kontakt]
         * 3. CODE-PFLEGE: Jegliche Änderungen an diesem Code müssen dem Autor gemeldet 
         * werden (vorzugsweise via Pull Request auf GitHub).
         * 4. WEITERVERBREITUNG: Binärdateien dürfen nur zusammen mit dem vollständigen 
         * Quellcode verbreitet werden (Copyleft).
         * * --- GEWÄHRLEISTUNGSAUSSCHLUSS & HAFTUNGSBESCHRÄNKUNG ---
         * DIESE SOFTWARE WIRD "WIE BESEHEN" (AS IS) ZUR VERFÜGUNG GESTELLT, OHNE 
         * JEGLICHE AUSDRÜCKLICHE ODER STILLSCHWEIGENDE GEWÄHRLEISTUNG, EINSCHLIESSLICH, 
         * ABER NICHT BESCHRÄNKT AUF DIE GEWÄHRLEISTUNG DER MARKTGÄNGIGKEIT, DER EIGNUNG 
         * FÜR EINEM BESTIMMTEN ZWECK UND DER NICHTVERLETZUNG VON RECHTEN DRITTER. 
         * IN KEINEM FALL SIND DIE AUTOREN ODER URHEBERRECHTSINHABER HAFTBAR FÜR 
         * ANSPRÜCHE, SCHÄDEN ODER ANDERE VERPFLICHTUNGEN, OB AUS VERTRAG, UNERLAUBTER 
         * HANDLUNG ODER ANDERWEITIG, DIE AUS ODER IM ZUSAMMENHANG MIT DER SOFTWARE 
         * ODER DER NUTZUNG ODER ANDEREN GESCHÄFTEN MIT DER SOFTWARE ENTSTEHEN. 
         * DAS GESAMTE RISIKO HINSICHTLICH DER QUALITÄT UND LEISTUNG DES PROGRAMMS 
         * LIEGT BEIM NUTZER.
     ******************************************************************************

# Arduino-Relaissteuerung mit LCD

Firmware fuer einen Arduino Mega 2560 zur Steuerung von bis zu 32 Relaiskanaelen mit ST7920-Grafikdisplay und Drehgeber. Das Standardprofil ist `mega16`; ein zweites 16-Kanal-Board kann ueber das Menue aktiviert werden.

## Funktionen

- 16 oder 32 Kanaele, Auswahl wird im EEPROM gespeichert
- LCD-Raster zur Statusanzeige und Drehgeber zur Bedienung
- Nummerierung ab 0 oder 1, ebenfalls dauerhaft gespeichert
- Menue per 5-Sekunden-Langdruck des Encoder-Tasters
- Relais-Test: alle aktiven Kanaele EIN, danach im Sekundentakt einzeln AUS
- Serielle Befehle mit 115200 Baud

## Hardware und Pinbelegung

| Funktion | Mega-Pin |
| --- | ---: |
| Relais 1-8 | D22-D29 |
| Relais 9-16 | D30-D37 |
| Relais 17-24 | D38-D45 |
| Relais 25-32 | D46-D53 |
| Encoder A / B | D2 / D3 |
| Encoder-Taster | D4 |
| ST7920 Takt / Daten / CS | D13 / D11 / D12 |

Das Display ist ein 128x64 ST7920 im 3-Draht-Modus und wird mit U8g2 betrieben. Details zu Displayanschluss und Verdrahtung stehen in [doc/pinout_display](doc/pinout_display).

## Bedienung

Im normalen Raster bewegt Drehen die Auswahl; kurzes Druecken schaltet das ausgewaehlte Relais. Den Taster 5 Sekunden halten, um das Menue zu oeffnen. Im Menue mit Drehen einen Punkt markieren und kurz druecken, um zwischen 16/32 Kanaelen oder der Nummerierungsbasis 0/1 zu wechseln, den Relais-Test zu starten oder zurueckzukehren.

Der Relais-Test schaltet alle aktiven Kanaele gleichzeitig ein und danach einmal pro Sekunde den naechsten Kanal aus. Ein Tastendruck bricht den Test ab und schaltet die verbleibenden Kanaele aus. Die EEPROM-Auswahl fuer Kanalzahl und Nummerierungsbasis bleibt beim Ausschalten erhalten.

## Serielle Befehle

Seriell mit 115200 Baud, ein Befehl pro Zeile. Bei Nummerierungsbasis 1:

```text
1,2,12:ON
12:OFF
all:OFF
```

Bei Nummerierungsbasis 0 werden die Kanaele `0` bis `15` beziehungsweise `0` bis `31` adressiert. `all:ON` schaltet alle im EEPROM aktiven Kanaele ein; `all:OFF` schaltet sie aus. `all:ON` nur verwenden, wenn die Kontakte sicher beschaltet sind und die Versorgung fuer alle Relaisspulen ausgelegt ist.

## Bauen und Flashen

PlatformIO installiert U8g2 gemaess `platformio.ini`. Das Standardprofil ist `mega16`:

```sh
pio run
pio run -e mega16
pio run -e mega32
pio run -e mega16 -t upload --upload-port /dev/ttyACM0
```

Beide Profile erzeugen Firmware fuer denselben Arduino Mega 2560. `RELAY_COUNT` legt nur den Standard fest, wenn im EEPROM noch keine gueltige Kanalzahl gespeichert ist; andernfalls gilt die gespeicherte Menueauswahl. In der Arduino IDE U8g2 installieren und `RELAY_COUNT` bei Bedarf auf `16` oder `32` setzen.

## 3D-Druckdateien

Die STL-Modelle liegen im Ordner `stl/`:

- `Arduino Relais box arduino.stl`
- `Arduino Relais box card.stl`
- `Arduino Relais box cover.stl`

Die Modelle lassen sich direkt in einen Slicer importieren. Druckmaterial, Ausrichtung und Druckeinstellungen sind abhaengig vom Drucker und hier nicht festgelegt.

## Sicherheit

Die Firmware schaltet aktiv-LOW: LOW bedeutet EIN, HIGH bedeutet AUS. Relais-Spulen nicht aus dem 5-V-Pin des Mega speisen; ein geeignet dimensioniertes separates Netzteil verwenden. Vor dem Anschluss Trigger-Jumper und Eingangsschaltung des konkreten Relaisboards pruefen. Netzspannung nur mit dafuer geeigneter Isolation, Absicherung, Gehaeuse und Zugentlastung schalten. Die Relaiskontakte beim ersten Test nach Moeglichkeit lastfrei lassen.

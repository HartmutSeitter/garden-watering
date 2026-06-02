#!/usr/bin/env python3
"""
CWT-SOIL-THC-S Sensor Auslesung via RS-485 / USB-Adapter
Modbus RTU, Slave 0x01, 4800 8N1

Verwendung:
    python3 read_sensor.py                        # auto-detect Port, einmalig lesen
    python3 read_sensor.py --port /dev/tty.usbserial-XXXX
    python3 read_sensor.py --loop 10              # alle 10 Sekunden wiederholen
    python3 read_sensor.py --list                 # verfügbare Ports anzeigen
"""

import sys
import time
import argparse
import glob


def find_usb_serial_port():
    """Sucht automatisch nach USB-Serial-Adaptern."""
    candidates = glob.glob("/dev/tty.usbserial*") + glob.glob("/dev/tty.SLAB_USBtoUART*") + glob.glob("/dev/tty.usbmodem*")
    if not candidates:
        return None
    if len(candidates) == 1:
        return candidates[0]
    print("Mehrere Ports gefunden:")
    for i, p in enumerate(candidates):
        print(f"  [{i}] {p}")
    idx = int(input("Port auswählen [0]: ") or "0")
    return candidates[idx]


def list_ports():
    """Zeigt alle verfügbaren Serial-Ports."""
    ports = glob.glob("/dev/tty.*") + glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*")
    ports = sorted(set(ports))
    if not ports:
        print("Keine Serial-Ports gefunden.")
        return
    print("Verfügbare Ports:")
    for p in ports:
        print(f"  {p}")


def read_sensor(port: str) -> dict:
    """Liest alle drei Register vom Sensor. Gibt dict mit Messwerten zurück."""
    import minimalmodbus

    instrument = minimalmodbus.Instrument(port, slaveaddress=1)
    instrument.serial.baudrate = 4800
    instrument.serial.bytesize = 8
    instrument.serial.parity = "N"
    instrument.serial.stopbits = 1
    instrument.serial.timeout = 1.0
    instrument.mode = minimalmodbus.MODE_RTU

    # Register 0x0000: Feuchtigkeit (÷10 = %)
    # Register 0x0001: Temperatur   (÷10 = °C, signed)
    # Register 0x0002: EC           (direkt µS/cm)
    raw_hum  = instrument.read_register(0x0000, functioncode=3)
    raw_temp = instrument.read_register(0x0001, functioncode=3, signed=True)
    raw_ec   = instrument.read_register(0x0002, functioncode=3)

    return {
        "humidity_pct": raw_hum  / 10.0,
        "temp_c":       raw_temp / 10.0,
        "ec_us_cm":     raw_ec,
    }


def print_reading(data: dict):
    ts = time.strftime("%Y-%m-%d %H:%M:%S")
    print(f"[{ts}]  Feuchte: {data['humidity_pct']:5.1f} %   "
          f"Temp: {data['temp_c']:5.1f} °C   "
          f"EC: {data['ec_us_cm']:5d} µS/cm")


def main():
    parser = argparse.ArgumentParser(description="CWT-SOIL-THC-S Sensor auslesen")
    parser.add_argument("--port",  help="Serial-Port, z.B. /dev/tty.usbserial-XXXX")
    parser.add_argument("--loop",  type=float, metavar="SEK",
                        help="Dauerhaft lesen, Pause in Sekunden")
    parser.add_argument("--list",  action="store_true", help="Verfügbare Ports anzeigen")
    args = parser.parse_args()

    if args.list:
        list_ports()
        return

    # minimalmodbus prüfen
    try:
        import minimalmodbus  # noqa: F401
    except ImportError:
        print("Fehler: minimalmodbus nicht installiert.")
        print("  pip3 install minimalmodbus")
        sys.exit(1)

    port = args.port or find_usb_serial_port()
    if not port:
        print("Kein USB-Serial-Port gefunden. Bitte --port angeben.")
        print("Verfügbare Ports: python3 read_sensor.py --list")
        sys.exit(1)

    print(f"Port: {port}  |  4800 8N1  |  Slave 0x01")
    print("-" * 60)

    if args.loop:
        while True:
            try:
                data = read_sensor(port)
                print_reading(data)
            except Exception as e:
                print(f"[{time.strftime('%H:%M:%S')}]  Fehler: {e}")
            try:
                time.sleep(args.loop)
            except KeyboardInterrupt:
                print("\nBeendet.")
                break
    else:
        try:
            data = read_sensor(port)
            print_reading(data)
        except Exception as e:
            print(f"Fehler beim Lesen: {e}")
            sys.exit(1)


if __name__ == "__main__":
    main()

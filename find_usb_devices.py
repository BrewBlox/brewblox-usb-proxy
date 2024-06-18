from glob import glob
from pathlib import Path

for tty in glob('/sys/class/tty/ttyACM*'):
    tty = Path(tty)

    if (tty / 'device' / 'subsystem').resolve() != Path('/sys/bus/usb'):
        continue

    dev = (tty / 'device').resolve()
    usb_vid = (dev / '..' / 'idVendor').read_text().strip()
    usb_pid = (dev / '..' / 'idProduct').read_text().strip()
    usb_device_id = (dev / '..' / 'serial').read_text().strip()

    print(tty)
    print('\t', f'{usb_vid}:{usb_pid}')
    print('\t', usb_device_id)
    print('\t', '/dev/' + tty.name)

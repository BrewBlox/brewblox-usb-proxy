from glob import glob
from pathlib import Path

for tty in glob('/sys/class/tty/ttyACM*'):
    tty = Path(tty)

    if (tty / 'device' / 'subsystem').resolve() != Path('/sys/bus/usb'):
        continue

    dev_root = (tty / 'device').resolve() / '..'
    usb_vid = (dev_root / 'idVendor').read_text().strip()
    usb_pid = (dev_root / 'idProduct').read_text().strip()
    usb_device_id = (dev_root / 'serial').read_text().strip()

    print(tty)
    print('\t', f'{usb_vid}:{usb_pid}')
    print('\t', usb_device_id)
    print('\t', '/dev/' + tty.name)

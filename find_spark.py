from glob import glob
from pathlib import Path

usb_ttys = [Path(p) for p in glob('/sys/class/tty/ttyACM*')
            if (Path(p) / 'device' / 'subsystem').resolve() == Path('/sys/bus/usb')]

for tty in usb_ttys:
    dev = (tty / 'device').resolve()
    print(tty)
    print('\t',
          (dev / '..' / 'idVendor').read_text().strip(),
          (dev / '..' / 'idProduct').read_text().strip())
    print('\t', (dev / '..' / 'serial').read_text().strip())
    print('\t', '/dev/' + tty.name)

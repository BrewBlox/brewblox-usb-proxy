from glob import glob
from pathlib import Path
from subprocess import run

for tty in glob('/sys/class/tty/ttyUSB*'):
    tty = Path(tty)

    if (tty / 'device' / 'subsystem').resolve() != Path('/sys/bus/usb-serial'):
        continue

    dev_root = (tty / 'device').resolve() / '..' / '..'
    usb_vid = (dev_root  / 'idVendor').read_text().strip()
    usb_pid = (dev_root  / 'idProduct').read_text().strip()
    usb_device_id = (dev_root / 'serial').read_text().strip()

    print(tty)
    print('\t', f'{usb_vid}:{usb_pid}')
    print('\t', usb_device_id)
    print('\t', '/dev/' + tty.name)

    # To listen to the serial port, use the below code
    # if usb_vid == '10c4' and usb_pid == 'ea60':
    #     run(['sudo', '/usr/bin/socat', '-u', f'file:/dev/{tty.name},raw,echo=0,b115200', '-'])
    #     break

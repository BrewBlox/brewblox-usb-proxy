import asyncio
import logging
import os
import signal
from asyncio.subprocess import Process
from contextlib import asynccontextmanager, suppress
from contextvars import ContextVar
from dataclasses import dataclass

from serial.tools import list_ports

from . import utils

SPARK_HWIDS = [
    r'USB VID\:PID=2B04\:C006.*',  # Photon
    r'USB VID\:PID=2B04\:C008.*',  # P1
]

# Construct a regex OR'ing all allowed hardware ID matches
# Example result: (?:HWID_REGEX_ONE|HWID_REGEX_TWO)
SPARK_DEVICE_REGEX = f'(?:{"|".join([dev for dev in SPARK_HWIDS])})'
USB_BAUD_RATE = 115200


LOGGER = logging.getLogger(__name__)
CV: ContextVar['SparkDiscovery'] = ContextVar('discovery.SparkDiscovery')


@dataclass(frozen=True)
class SparkConnection:
    device_id: str
    port: int
    proc: Process

    def close(self):
        with suppress(Exception):
            os.killpg(os.getpgid(self.proc.pid), signal.SIGINT)


class SparkDiscovery:
    def __init__(self) -> None:
        self.config = utils.get_config()
        self._connections: set[SparkConnection] = set()

    @property
    def connection_index(self) -> dict[str, int]:
        return dict((c.device_id, c.port)
                    for c in self._connections
                    if c.proc.returncode is None)

    def _next_port(self):
        ports = [c.port for c in self._connections]
        return next((p for p
                     in range(self.config.port_start, self.config.port_end)
                     if p not in ports))

    async def connect_usb(self, device_id: str, device_serial: str) -> SparkConnection:
        port = self._next_port()
        proc = await asyncio.create_subprocess_exec('/usr/bin/socat',
                                                    f'tcp-listen:{port},reuseaddr,fork',
                                                    f'file:{device_serial},raw,echo=0,b{USB_BAUD_RATE}',
                                                    preexec_fn=os.setsid,
                                                    shell=False)
        return SparkConnection(device_id=device_id,
                               port=port,
                               proc=proc)

    async def discover_usb(self) -> None:
        connected_ids = set(c.device_id for c in self._connections)
        discovered_ids = set()

        for usb_port in list_ports.grep(SPARK_DEVICE_REGEX):
            device_id = usb_port.serial_number.lower()
            discovered_ids.add(device_id)
            if device_id not in connected_ids:
                conn = await self.connect_usb(device_id, usb_port.device)
                LOGGER.info(f'Added {conn}')
                self._connections.add(conn)

        def test_connected(conn: SparkConnection) -> bool:
            if conn.proc.returncode is not None:
                LOGGER.info(f'Removed {conn} (returncode={conn.proc.returncode})')
                return False

            if conn.device_id not in discovered_ids:
                conn.close()

            return True

        LOGGER.debug(f'{discovered_ids=}')
        self._connections = set(filter(test_connected, self._connections))

    async def repeat(self):
        interval = self.config.discovery_interval
        while True:
            try:
                await self.discover_usb()
                await asyncio.sleep(interval.total_seconds())
            except Exception as ex:
                LOGGER.error(utils.strex(ex))
                await asyncio.sleep(interval.total_seconds())

    def close(self):
        for conn in self._connections:
            conn.close()
        self._connections.clear()


@asynccontextmanager
async def lifespan():
    async with utils.task_context(CV.get().repeat()):
        yield
    CV.get().close()


def setup():
    CV.set(SparkDiscovery())

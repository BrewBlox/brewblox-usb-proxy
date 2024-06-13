import logging

from fastapi import APIRouter

from . import discovery

LOGGER = logging.getLogger(__name__)

router = APIRouter(prefix='/connected', tags=['Connected'])


@router.get('/spark')
async def discovery_connected() -> dict[str, int]:
    """
    Get device ID and port for all connected Spark USB devices
    """
    return discovery.CV.get().connection_index

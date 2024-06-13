"""
Pydantic models are declared here, and then imported wherever needed
"""

from datetime import timedelta

from pydantic_settings import BaseSettings, SettingsConfigDict


class ServiceConfig(BaseSettings):
    """
    Global service configuration.

    Pydantic Settings (https://docs.pydantic.dev/latest/concepts/pydantic_settings/)
    provides the `BaseSettings` model that loads values from environment variables.

    To access the loaded model, we use `utils.get_config()`.
    """
    model_config = SettingsConfigDict(
        env_prefix='brewblox_usb_proxy_',
        case_sensitive=False,
        json_schema_extra='ignore',
    )

    name: str = 'usb-proxy'
    debug: bool = False

    discovery_interval: timedelta = timedelta(seconds=5)
    port_start: int = 9000
    port_end: int = 9500

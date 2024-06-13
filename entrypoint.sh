#!/bin/env bash
set -euo pipefail

exec uvicorn \
    --host 0.0.0.0 \
    --port 5000 \
    --factory \
    brewblox_usb_proxy.app_factory:create_app

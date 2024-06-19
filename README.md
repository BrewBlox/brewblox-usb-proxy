# USB Proxy for Spark devices

This service scans for connected Spark USB devices, and then forwards the connection over TCP.

## Endpoints

### `GET /usb-proxy/discover/{desired_id: string}`

This will scan all connected USB devices.
All previously connected devices that are no longer present will have their proxies stopped.\
If no proxy is active for any device that matches `desired_id`, one will be started.

`desired_id` is either a lower case device ID for a Spark 2/3, or the wildcard value "all".

The return value is a JSON object with all detected devices.
The key is the device ID, and the value is the associated TCP port, or `null` if no proxy is active.

Example output:
```json
{
    "30003d001947383434353030":null,
    "280038000847343337373738":9001
}
```

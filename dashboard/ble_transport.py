"""SolarYou BLE transport. Hardware-independent framing and injectable BLE client.

Existing whole-JSON notifications remain compatible. Small-MTU frames have a
6-byte header: 1e, version=1, sequence little-endian u16, index u8, count u8.
Only complete, ordered, bounded messages reach the dashboard or CSV recorder.
"""
import asyncio
import json
import time
try:
    from bleak.exc import BleakCharacteristicNotFoundError
except ImportError:
    # app.py also supports mock/serial/WiFi without installing Bleak.
    BleakCharacteristicNotFoundError = ()

RATE_UUID = 'f2d2680c-66bc-4e24-8eac-d8013a99320c'
ALLOWED_RATES = (1, 2, 5, 10)
INITIAL_RATE_ATTEMPTS = 3
INITIAL_RATE_RETRY_SECONDS = 0.25


def valid_rate(value):
    return type(value) is int and value in ALLOWED_RATES


class NotificationDecoder:
    def __init__(self, max_bytes=4096, timeout=2.0):
        self.max_bytes = max_bytes
        self.timeout = timeout
        self.reset()

    def reset(self):
        self.sequence = None
        self.next_index = 0
        self.count = 0
        self.started = 0.0
        self.buffer = bytearray()

    def feed(self, packet, now=None):
        now = time.monotonic() if now is None else now
        packet = bytes(packet)
        if not packet:
            return None
        if packet[0] != 0x1e:
            self.reset()
            if len(packet) > self.max_bytes:
                raise ValueError('BLE message exceeds limit')
            return json.loads(packet.decode('utf-8'))
        if len(packet) <= 6 or packet[1] != 1:
            self.reset()
            raise ValueError('Invalid BLE fragment header')
        sequence = int.from_bytes(packet[2:4], 'little')
        index, count = packet[4:6]
        if count < 2 or count > 32 or index >= count:
            self.reset()
            raise ValueError('Invalid BLE fragment index/count')
        if index == 0:
            self.reset()
            self.sequence, self.count, self.started = sequence, count, now
        if (self.sequence != sequence or self.count != count or index != self.next_index
                or now - self.started > self.timeout):
            self.reset()
            raise ValueError('Missing, out-of-order, or expired BLE fragments')
        if len(self.buffer) + len(packet) - 6 > self.max_bytes:
            self.reset()
            raise ValueError('BLE assembly exceeds limit')
        self.buffer.extend(packet[6:])
        self.next_index += 1
        if self.next_index < self.count:
            return None
        complete = bytes(self.buffer)
        self.reset()
        return json.loads(complete.decode('utf-8'))


class BleLink:
    def __init__(self, config, on_reading, on_status, on_rate, retry_seconds=2):
        self.name = config.get('ble_name')
        self.telemetry_uuid = config.get('ble_characteristic')
        self.rate_uuid = config.get('ble_rate_characteristic', RATE_UUID)
        if not self.name or not self.telemetry_uuid:
            raise ValueError('config.json is missing ble_name or ble_characteristic')
        self.on_reading, self.on_status, self.on_rate = on_reading, on_status, on_rate
        self.retry_seconds = retry_seconds
        self.decoder = NotificationDecoder()
        self.client = None
        self.loop = None
        self.change_lock = None
        self.supports_rate = False
        self.desired_rate = None
        self.pending_rate = None

    def handle_notification(self, _characteristic, packet):
        try:
            message = self.decoder.feed(packet)
            if message is None:
                return
            if isinstance(message, dict) and 'rate_hz' in message:
                rate = message['rate_hz']
                if not valid_rate(rate):
                    raise ValueError('Invalid applied-rate response')
                # Notifications are advisory. Only a successful characteristic
                # readback may confirm the setting, including after a failed
                # request or a delayed acknowledgement from an old request.
                return  # Acknowledgements never become sensor/CSV rows.
            # Zack's existing processor accepts JSON text for batches, but not
            # Python lists. Preserve its original validation and batch limit.
            self.on_reading(json.dumps(message, separators=(',', ':'))
                            if isinstance(message, list) else message)
        except (ValueError, UnicodeError, RecursionError) as exc:
            # A bad/missing fragment drops only that sample. Keep the radio link.
            self.on_status('warning', str(exc))

    def _is_current_client(self, client):
        return (client is not None and self.client is client
                and getattr(client, 'is_connected', True))

    async def read_rate(self, client=None):
        client = self.client if client is None else client
        if client is None:
            raise RuntimeError('BLE disconnected')
        raw = await asyncio.wait_for(client.read_gatt_char(self.rate_uuid), timeout=3.0)
        if not self._is_current_client(client):
            raise RuntimeError('BLE disconnected during rate readback')
        message = json.loads(bytes(raw).decode('utf-8'))
        rate = message.get('rate_hz') if isinstance(message, dict) else None
        if not valid_rate(rate):
            raise ValueError('Firmware returned an invalid applied rate')
        return rate

    async def check_initial_rate(self, client):
        """Retry reads on this connection without interrupting notifications."""
        for attempt in range(1, INITIAL_RATE_ATTEMPTS + 1):
            if not self._is_current_client(client):
                raise RuntimeError('BLE disconnected during initial rate check')
            self.on_rate({'available': False, 'state': 'checking',
                          'attempt': attempt, 'max_attempts': INITIAL_RATE_ATTEMPTS})
            try:
                return await self.read_rate(client)
            except BleakCharacteristicNotFoundError:
                if not self._is_current_client(client):
                    raise
                # A missing characteristic proves the firmware has no rate control.
                self.on_rate({'available': False, 'state': 'unsupported'})
                return None
            except Exception:
                if not self._is_current_client(client):
                    raise
                if attempt == INITIAL_RATE_ATTEMPTS:
                    self.on_rate({
                        'available': False, 'state': 'error',
                        'error': f'Could not check ESP32 rate after {INITIAL_RATE_ATTEMPTS} attempts. '
                                 'Readings continue; reconnect Bluetooth to try again.'
                    })
                    return None
                # Yield to telemetry callbacks while waiting for the next read.
                await asyncio.sleep(INITIAL_RATE_RETRY_SECONDS)

    async def change_rate(self, rate, expected_client=None):
        if not valid_rate(rate):
            raise ValueError('Choose 1, 2, 5, or 10 updates per second')
        expected_client = self.client if expected_client is None else expected_client
        async with self.change_lock:
            if not self._is_current_client(expected_client) or not self.supports_rate:
                raise RuntimeError('Connect to firmware supporting the rate setting first')
            self.pending_rate = rate
            client = self.client
            self.on_rate({'available': True, 'requested_hz': rate, 'state': 'pending'})
            try:
                command = json.dumps({'set_rate_hz': rate}, separators=(',', ':')).encode('utf-8')
                # At most 18 bytes: the command itself fits the default MTU.
                await asyncio.wait_for(client.write_gatt_char(self.rate_uuid, command, response=True), timeout=3.0)
                if not self._is_current_client(client):
                    raise RuntimeError('BLE disconnected during rate request')
                self.on_rate({'available': True, 'requested_hz': rate, 'state': 'verifying'})
                # Readback verifies the actual setting even if an ack notification is lost.
                applied = await self.read_rate(client)
                if not self._is_current_client(client):
                    raise RuntimeError('BLE connection changed during rate request')
                if applied != rate:
                    raise RuntimeError(f'Firmware applied {applied} Hz instead of {rate} Hz')
                self.desired_rate = applied
                self.on_rate({'available': True, 'rate_hz': applied, 'state': 'confirmed'})
                return applied
            except Exception as exc:
                # Report before releasing the lock, so this failure cannot
                # overwrite a queued request's newer pending status.
                if self._is_current_client(client):
                    self.on_rate({'available': self.supports_rate, 'state': 'error', 'error': str(exc)})
                raise
            finally:
                self.pending_rate = None

    def request_rate(self, rate):
        """Called from Flask's thread; all BLE operations stay on the BLE loop."""
        if not valid_rate(rate):
            raise ValueError('Choose 1, 2, 5, or 10 updates per second')
        client, loop = self.client, self.loop
        if loop is None or not self._is_current_client(client) or not self.supports_rate:
            raise RuntimeError('BLE rate setting is unavailable until connected')
        future = asyncio.run_coroutine_threadsafe(self.change_rate(rate, client), loop)
        def finished(result):
            try:
                result.result()
            except Exception:
                pass  # change_rate reports current-session failures while holding its lock.
        future.add_done_callback(finished)
        return future

    async def run(self, scanner, client_factory, stop=None):
        self.loop = asyncio.get_running_loop()
        self.change_lock = asyncio.Lock()
        try:
            while stop is None or not stop.is_set():
                self.decoder.reset()
                reason = 'device disconnected'
                try:
                    self.on_status('connecting', f'Scanning for {self.name}')
                    device = await scanner.find_device_by_name(self.name, timeout=10.0)
                    if device is None:
                        raise RuntimeError(f'Could not find BLE device {self.name}')
                    disconnected = asyncio.Event()
                    def lost(lost_client, event=disconnected, loop=self.loop):
                        def mark_lost():
                            if self.client is lost_client:
                                self.client = None
                                self.supports_rate = False
                                self.decoder.reset()
                                self.on_rate({'available': False, 'state': 'disconnected'})
                            event.set()
                        try:
                            loop.call_soon_threadsafe(mark_lost)
                        except RuntimeError:
                            pass  # The dashboard's BLE loop has already closed.
                    async with client_factory(device, disconnected_callback=lost) as client:
                        self.client = client
                        self.supports_rate = False
                        def notify(characteristic, packet, expected=client):
                            if self._is_current_client(expected):
                                self.handle_notification(characteristic, packet)
                        await client.start_notify(self.telemetry_uuid, notify)
                        self.on_status('connected', None)
                        applied = await self.check_initial_rate(client)
                        if applied is not None:
                            self.supports_rate = True
                            if self.desired_rate is not None and applied != self.desired_rate:
                                try:
                                    await self.change_rate(self.desired_rate)
                                except Exception:
                                    # change_rate already reports the error while holding
                                    # its lock. A setting failure must not stop telemetry;
                                    # a genuinely lost connection still needs reconnecting.
                                    if not self._is_current_client(client):
                                        raise
                            else:
                                self.on_rate({'available': True, 'rate_hz': applied, 'state': 'confirmed'})
                        if stop is None:
                            await disconnected.wait()
                        else:
                            while not disconnected.is_set() and not stop.is_set():
                                await asyncio.sleep(0.01)
                except asyncio.CancelledError:
                    raise
                except Exception as exc:
                    reason = str(exc) or type(exc).__name__
                finally:
                    self.client = None
                    self.supports_rate = False
                    self.decoder.reset()
                    self.on_rate({'available': False, 'state': 'disconnected'})
                if stop is None or not stop.is_set():
                    self.on_status('reconnecting', reason)
                    await asyncio.sleep(self.retry_seconds)
        finally:
            self.loop = None

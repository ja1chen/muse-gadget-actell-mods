"""Import the explicitly shared Actell UI connection information on a verified host.
Does not register tools, contact a Muse VM, or disable TLS verification.
"""
import base64
import re
from .client import Client, DirectHTTPTransport, ID, strict_json


def client_from_connection_info(raw, *, ca_file=None):
    if isinstance(raw, str):
        raw = raw.encode('utf-8')
    if not isinstance(raw, bytes) or len(raw) > 8192:
        raise ValueError('connection information must be at most 8 KiB of UTF-8 JSON')
    info = strict_json(raw)
    expected = {'v', 'kind', 'endpoint', 'client_id', 'secret_base64',
                'certificate_sha256', 'tls_verification_required'}
    if not isinstance(info, dict) or set(info) != expected:
        raise ValueError('invalid connection information fields')
    if type(info['v']) is not int or info['v'] != 1 or info['kind'] != 'actell-muse-connection':
        raise ValueError('unsupported connection information version')
    if info['tls_verification_required'] is not True:
        raise ValueError('TLS verification is mandatory')
    if not isinstance(info['client_id'], str) or not ID.fullmatch(info['client_id']):
        raise ValueError('invalid client ID')
    pin = info['certificate_sha256']
    if not isinstance(pin, str) or (pin and not re.fullmatch('[0-9a-f]{64}', pin)):
        raise ValueError('invalid reference certificate fingerprint')
    if not isinstance(info['secret_base64'], str):
        raise ValueError('invalid client secret')
    try:
        secret = base64.b64decode(info['secret_base64'], validate=True)
    except (ValueError, base64.binascii.Error) as error:
        raise ValueError('invalid client secret') from error
    if len(secret) != 32 or base64.b64encode(secret).decode() != info['secret_base64']:
        raise ValueError('client secret must be 32 bytes')
    if not isinstance(info['endpoint'], str):
        raise ValueError('invalid HTTPS endpoint')
    # Fingerprint is a human cross-check; it does not replace chain/name validation.
    return Client(info['client_id'], secret, DirectHTTPTransport(info['endpoint'], ca_file=ca_file))

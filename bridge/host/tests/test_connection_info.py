import base64
import json
import unittest
from muse_client.connection_info import client_from_connection_info

class ConnectionInfoTests(unittest.TestCase):
    def info(self):
        return {'v': 1, 'kind': 'actell-muse-connection', 'endpoint': 'https://actell-test.invalid:8765',
                'client_id': 'test-only-client', 'secret_base64': base64.b64encode(bytes(range(32))).decode(),
                'certificate_sha256': '0' * 64, 'tls_verification_required': True}
    def test_ui_format_creates_client_without_network(self):
        client = client_from_connection_info(json.dumps(self.info()))
        self.assertEqual(client.client_id, 'test-only-client')
    def test_rejects_plaintext_and_verification_bypass(self):
        for field, value in [('endpoint', 'http://127.0.0.1:8765'), ('tls_verification_required', False),
                             ('endpoint', 'https://user:password@host.invalid'), ('endpoint', 'https://host.invalid/path')]:
            info = self.info(); info[field] = value
            with self.assertRaises(ValueError): client_from_connection_info(json.dumps(info))
    def test_rejects_duplicate_keys_unknown_fields_and_invalid_secrets(self):
        with self.assertRaises(ValueError): client_from_connection_info('{"v":1,"v":1}')
        for field, value in [('secret_base64', 'not-base64'), ('secret_base64', base64.b64encode(b'short').decode()),
                             ('client_id', 'bad/client'), ('certificate_sha256', 'x'), ('v', True), ('extra', 1)]:
            info = self.info(); info[field] = value
            with self.assertRaises(ValueError): client_from_connection_info(json.dumps(info))
        with self.assertRaises(ValueError): client_from_connection_info(b' ' * 8193)

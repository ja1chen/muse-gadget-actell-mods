"""Actual portable C guard tests plus structural integration checks; no device access."""
import pathlib, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
EXT=ROOT/'extensions/gadget-actell'
COMP=EXT/'components/actell_bridge'
class GadgetComponentTests(unittest.TestCase):
    def test_real_c_guard(self):
        with tempfile.TemporaryDirectory() as d:
            binary=pathlib.Path(d)/'guard'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(COMP/'include'),str(COMP/'actell_guard.c'),str(COMP/'tests/guard_test.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
    def test_official_hooks_and_no_tunnel(self):
        patch=(ROOT/'patches/muse-gadget-sdk/actell-hook.patch').read_text()
        self.assertIn('actell_bridge_register(commands)',patch)
        self.assertIn('actell_bridge_invoke(command, params, request_id, session_generation)',patch)
        self.assertIn('actell_bridge_session(0)',patch)
        self.assertIn('actell_bridge_usb_setup()',patch)
        self.assertNotIn('link-tunnel',patch)
        self.assertNotIn('noise_tunnel.cpp',patch)
    def test_transport_fail_closed_structure(self):
        source=(COMP/'actell_http.c').read_text()
        self.assertIn('.skip_cert_common_name_check=false',source)
        self.assertIn('.common_name=session->hostname',source)
        self.assertIn('strcpy(session->hostname,hostname)',source)
        self.assertIn('.cert_pem=session->pem',source)
        self.assertIn('strcpy(session->pem,c->pem)',source)
        self.assertIn('.disable_auto_redirect=true',source)
        self.assertIn('esp_http_client_open(http,(int)blen)',source)
        self.assertNotIn('esp_http_client_perform(',source)
        self.assertIn('actell_bridge_current(generation)',source)
        self.assertIn('X-Actell-Signature',source)
        self.assertIn('actell_clock_ready()',source)
        self.assertIn('esp_sntp_init()', (COMP/'actell_clock.c').read_text())
    def test_disabled_defaults(self):
        cfg=(COMP/'Kconfig').read_text()
        self.assertEqual(cfg.count('default n'),4)
        self.assertIn('default -1',cfg)
        self.assertIn('120 seconds',cfg)
    def test_private_provisioning_boundary(self):
        src=(COMP/'actell_usb_setup.c').read_text()
        self.assertIn('gpio_get_level',src)
        self.assertIn('120000000',src)
        self.assertIn('usb_serial_jtag_driver_uninstall()',src)
        self.assertNotIn('ESP_LOG',src)
        self.assertNotIn('httpd_',src)
if __name__=='__main__':unittest.main()

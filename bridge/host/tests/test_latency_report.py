"""Synthetic timing data only; verifies correlation, privacy and measurement bounds."""
import importlib.util
import json
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('latency', Path(__file__).resolve().parents[2]/'tools/analyze-actell-latency.py')
latency = importlib.util.module_from_spec(spec)
spec.loader.exec_module(latency)

class LatencyReportTests(unittest.TestCase):
    def test_only_allowlisted_values_escape_input(self):
        data = {'source':'actell','phase':'fresh_frame','operation_id':'a'*64,'elapsed_ms':50,
                'secret':'synthetic-secret','image':'synthetic-image','request_id':'user supplied text'}
        parsed = latency.event(json.dumps(data))
        self.assertEqual(parsed, {'source':'actell','phase':'fresh_frame','operation_id':'a'*64,'elapsed_ms':50})
        self.assertIsNone(latency.event('phase=http_timing operation_id=not-a-hash'))
        self.assertIsNone(latency.event(json.dumps(dict(data,source='unknown'))))
        self.assertIsNone(latency.event(json.dumps(dict(data,source=[]))))
        self.assertIsNone(latency.event(json.dumps(dict(data,phase={}))))
        self.assertNotIn('op', latency.event(json.dumps(dict(data,op=[]))))

    def test_correlated_routes_and_no_clock_subtraction(self):
        events = []
        for number,elapsed in enumerate((100,200,900)):
            operation = str(number)*64
            events += [latency.event(f'phase=http_timing operation_id={operation} cache_hit=2 connection_reused=0 ok=1'),
                       latency.event(f'phase=worker_done operation_id={operation} op=actell.screenshot ok=1'),
                       latency.event(f'phase=upload_end operation_id={operation} since_invoke_ms={elapsed} jpeg_bytes=1024')]
        report = latency.summarize(events)
        self.assertEqual(report['distributions']['cache_hit.device_until_upload_ms'], {'n':3,'median':200,'p95':900})
        self.assertNotIn('end_to_end_ms', report)
        events.append(latency.event(json.dumps({'source':'actell','phase':'tunnel_tls','operation_id':'0'*64,'elapsed_ms':80})))
        self.assertEqual(latency.summarize(events)['control_path_distributions']['recovery_observed.device_until_upload_ms']['median'],100)
        events.append(latency.event('phase=worker_done operation_id='+('0'*64)+' op=actell.call_status'))
        self.assertEqual(latency.summarize(events)['distributions']['cache_hit.device_until_upload_ms']['n'],2)

    def test_failure_not_hidden_by_successful_fallback(self):
        operation='a'*64
        events=[latency.event(f'phase=http_timing operation_id={operation} cache_hit=2 connection_reused=0 ok=0 attempt=0'),
                latency.event(f'phase=http_timing operation_id={operation} cache_hit=0 connection_reused=0 ok=1 attempt=1')]
        sample=latency.summarize(events)['samples'][0]
        self.assertEqual(sample['route'],'locator_recovery')
        self.assertEqual(sample['transport_failures'],1)
        self.assertTrue(sample['ambiguous_or_incomplete'])

    def test_capture_failures_and_phase_metrics_remain_visible(self):
        operation='b'*64
        events=[latency.event(f'phase=http_timing operation_id={operation} cache_hit=0 connection_reused=0 ok=0 connect_ms=123'),
                latency.event(f'phase=worker_done operation_id={operation} op=actell.screenshot ok=0')]
        report=latency.summarize(events)
        self.assertEqual(report['capture_failures'],1)
        self.assertEqual(report['distributions'],{})
        self.assertEqual(report['phase_distributions']['muse.http_timing.connect_ms']['median'],123)
        events[0]=latency.event(f'phase=http_discovery operation_id={operation} elapsed_ms=2000')
        sample=latency.summarize(events)['samples'][0]
        self.assertEqual(sample['route'],'cold_discovery')
        self.assertEqual(sample['discovery_failures'],1)

    def test_actell_filtered_report_normalizes_only_measured_durations(self):
        state={'operation':'c'*64, 'elapsed_ns':'1250000', 'succeeded':'true',
               'bytes':'128', 'sample_uptime_ns':'99999999', 'secret':'synthetic-secret'}
        report={'events':[{'stage':'performance.fresh_frame_wait', 'state':state},
                          {'stage':'performance.unknown', 'state':state},
                          {'stage':'performance.original_encoding', 'state':dict(state,elapsed_ns='NaN')},
                          {'stage':'performance.cache_write', 'state':dict(state,operation='invalid')}]}
        self.assertEqual(latency.actell_events(report), [{'source':'actell', 'operation_id':'c'*64,
                          'phase':'fresh_frame', 'elapsed_ms':1.25, 'bytes':128, 'ok':1}])

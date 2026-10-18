#!/usr/bin/env python3
"""Summarize correlated, content-free screenshot timing events; never echo input logs.

Input: Muse UART timing lines and/or normalized JSONL from Actell/platform.
Only explicit local durations are aggregated. Clocks on different hosts are not subtracted.
"""
import argparse
import json
import math
import os
from pathlib import Path
import re
import statistics

ROOT = Path(__file__).resolve().parents[2]
OPERATION = re.compile(r'[a-f0-9]{64}\Z')
REQUEST = re.compile(r'[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}\Z')
PHASES = {'operation_begin', 'http_discovery', 'http_timing', 'image_validation', 'rpc_done', 'worker_done',
          'upload_start', 'upload_end', 'upload_aborted', 'control_prepare', 'media_start',
          'fresh_frame', 'original_encode', 'thumbnail_encode', 'cache_write', 'result_ready',
          'release', 'platform_result_received', 'platform_image_ready', 'llm_request_start',
          'llm_first_output', 'discovery', 'pair_socket', 'pair_verify', 'pair_validation',
          'tunnel_listener', 'tunnel_socket', 'tunnel_tls', 'service_socket', 'service_discovery',
          'developer_readiness'}
ACTELL_STAGES = {key:key for key in ('discovery', 'pair_socket', 'pair_verify', 'pair_validation',
                 'tunnel_listener', 'tunnel_socket', 'tunnel_tls', 'service_socket',
                 'service_discovery', 'developer_readiness', 'media_start', 'cache_write',
                 'result_ready', 'release')}
ACTELL_STAGES.update(control_preparation='control_prepare', fresh_frame_wait='fresh_frame',
                     original_encoding='original_encode', thumbnail_encoding='thumbnail_encode')
RECOVERY_STAGES = {'discovery', 'pair_socket', 'pair_verify', 'pair_validation',
                   'tunnel_listener', 'tunnel_socket', 'tunnel_tls', 'service_socket',
                   'service_discovery', 'developer_readiness'}
NUMBERS = {'elapsed_ms', 'queue_ms', 'since_invoke_ms', 'discovery_ms', 'connect_ms', 'read_ms',
           'total_ms', 'cache_hit', 'connection_reused', 'connection_borrowed', 'attempt', 'sent_bytes', 'bytes',
           'jpeg_bytes', 'wire_bytes', 'chunks', 'ok', 'image'}
OPERATIONS = {'capabilities', 'runs.open', 'runs.get', 'runs.cancel', 'tools.call', 'calls.get',
              'images.get', 'actell.screenshot', 'actell.call_status'}

def event(line):
    if len(line) > 16384:
        return None
    try:
        data = json.loads(line)
        if not isinstance(data, dict):
            return None
    except ValueError:
        data = dict(re.findall(r'\b([a-z_]+)=([^\s]+)', line))
        data['source'] = 'muse'
    operation = data.get('operation_id', '')
    phase = data.get('phase')
    if (not isinstance(operation, str) or not OPERATION.fullmatch(operation) or
            not isinstance(phase, str) or phase not in PHASES):
        return None
    source = data.get('source', 'muse')
    if not isinstance(source, str) or source not in {'muse', 'actell', 'platform'}:
        return None
    result = {'source': source, 'operation_id': operation, 'phase': phase}
    request = data.get('request_id', '')
    if isinstance(request, str) and REQUEST.fullmatch(request):
        result['request_id'] = request
    if isinstance(data.get('op'), str) and data['op'] in OPERATIONS:
        result['op'] = data['op']
    for key in NUMBERS:
        try:
            value = float(data[key])
        except (KeyError, ValueError, TypeError, OverflowError):
            continue
        if math.isfinite(value) and 0 <= value <= 10**12:
            result[key] = int(value) if value.is_integer() else value
    return result

def distribution(values):
    values = sorted(values)
    return {'n': len(values), 'median': statistics.median(values) if values else None,
            'p95': values[math.ceil(.95*len(values))-1] if values else None}

def actell_events(report):
    """Accept the peer's filtered collect_diagnostics.py report, not raw console data."""
    rows = report.get('events') if isinstance(report, dict) else None
    if not isinstance(rows, list):
        return []
    events = []
    for row in rows:
        if not isinstance(row, dict):
            continue
        stage, state = row.get('stage'), row.get('state')
        if not isinstance(stage, str) or not stage.startswith('performance.') or not isinstance(state, dict):
            continue
        phase = ACTELL_STAGES.get(stage.removeprefix('performance.'))
        if not phase:
            continue
        try:
            elapsed = float(state['elapsed_ns']) / 1_000_000
        except (KeyError, ValueError, TypeError, OverflowError):
            continue
        data = {'source':'actell', 'phase':phase, 'operation_id':state.get('operation'),
                'elapsed_ms':elapsed, 'bytes':state.get('bytes')}
        if state.get('succeeded') in ('true', 'false'):
            data['ok'] = int(state['succeeded'] == 'true')
        parsed = event(json.dumps(data))
        if parsed and 'elapsed_ms' in parsed:
            events.append(parsed)
    return events

def summarize(events):
    grouped = {}
    for item in events:
        grouped.setdefault(item['operation_id'], []).append(item)
    samples = []
    for operation, items in grouped.items():
        # Status reads can redeliver an image. Do not merge them into capture samples.
        done = [e for e in items if e['source'] == 'muse' and e['phase'] == 'worker_done']
        captures = [e for e in done if e.get('op') == 'actell.screenshot']
        uploads = [e for e in items if e['source'] == 'muse' and e['phase'] == 'upload_end']
        http = [e for e in items if e['source'] == 'muse' and e['phase'] == 'http_timing']
        discovery_failures = sum(e['source'] == 'muse' and e['phase'] == 'http_discovery' for e in items)
        aborted = any(e['source'] == 'muse' and e['phase'] == 'upload_aborted' for e in items)
        actell = [e for e in items if e['source'] == 'actell']
        control_path = ('recovery_observed' if any(e['phase'] in RECOVERY_STAGES for e in actell) else
                        'no_recovery_observed' if actell else 'unobserved')
        ambiguous = len(captures) != 1 or len(done) != 1 or len(uploads) > 1
        route = ('locator_recovery' if any(e.get('attempt',0) > 0 for e in http) else
                 'cold_discovery' if discovery_failures or any(e.get('cache_hit') == 0 for e in http) else
                 'cache_hit' if http else 'unknown')
        sample = {'operation_id': operation, 'route': route, 'control_path': control_path,
                  'ambiguous_or_incomplete': ambiguous,
                  'upload_completed': bool(uploads), 'http_attempts': len(http),
                  'new_connection_attempts': sum(e.get('connection_reused') == 0 for e in http),
                  'borrowed_connection_attempts': sum(e.get('connection_borrowed') == 1 for e in http),
                  'transport_failures': sum(e.get('ok') == 0 for e in http),
                  'discovery_failures': discovery_failures,
                  'capture_failed': not ambiguous and (captures[0].get('ok') == 0 or aborted),
                  'events': items}
        if not ambiguous and not sample['capture_failed'] and uploads:
            sample['device_until_upload_ms'] = uploads[0].get('since_invoke_ms')
            sample['jpeg_bytes'] = uploads[0].get('jpeg_bytes')
        samples.append(sample)
    distributions, control_distributions = {}, {}
    for sample in samples:
        if sample['ambiguous_or_incomplete']:
            continue
        value = sample.get('device_until_upload_ms')
        if value is not None:
            distributions.setdefault(sample['route'] + '.device_until_upload_ms', []).append(value)
            control_distributions.setdefault(sample['control_path'] + '.device_until_upload_ms', []).append(value)
    phases = {}
    for item in events:
        for metric in ('elapsed_ms', 'queue_ms', 'discovery_ms', 'connect_ms', 'read_ms', 'total_ms'):
            if metric in item:
                phases.setdefault(item['source'] + '.' + item['phase'] + '.' + metric, []).append(item[metric])
    return {'schema_version': 1, 'operations': len(samples),
            'capture_failures': sum(s['capture_failed'] for s in samples),
            'excluded_operations': sum(s['ambiguous_or_incomplete'] for s in samples),
            'samples': samples, 'distributions': {k:distribution(v) for k,v in distributions.items()},
            'control_path_distributions': {k:distribution(v) for k,v in control_distributions.items()},
            'phase_distributions': {k:distribution(v) for k,v in phases.items()},
            'limits': ['SDK upload completion is not platform receipt or model visibility.',
                       'No cross-host clock subtraction or addition of overlapping phases.',
                       'Control recovery classification uses observed Actell spans; missing spans do not prove no recovery.',
                       'Repeated/status-only or incomplete invocations are excluded from capture latency.']}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='*', type=Path)
    parser.add_argument('--actell-report', action='append', type=Path, default=[],
                        help='Filtered JSON report from Actell tools/muse-remote/collect_diagnostics.py')
    parser.add_argument('--output', type=Path, default=ROOT/'local/validation/screenshot-performance/timing-report.json')
    args = parser.parse_args()
    if not args.logs and not args.actell_report:
        parser.error('Provide Muse/normalized logs or an Actell report.')
    output = args.output.resolve()
    if not output.is_relative_to(ROOT/'local'):
        parser.error('Reports must remain under local/.')
    events = []
    for path in args.logs:
        with path.open(errors='replace') as stream:
            for line in stream:
                parsed = event(line)
                if parsed:
                    events.append(parsed)
    for path in args.actell_report:
        events.extend(actell_events(json.loads(path.read_text())))
    os.umask(0o077)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(summarize(events), indent=2)+'\n')
    output.chmod(0o600)
    print(f'Saved {len(events)} timing events; raw log content omitted.')

if __name__ == '__main__':
    main()

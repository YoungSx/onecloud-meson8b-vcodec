from fractions import Fraction
from pathlib import Path
import hashlib
import json
import re
import signal
import subprocess
import time
import urllib.request

base = Path('/mnt/sda2/codec-rate-delivery')
candidate = base / 'meson-venc.ko'
original = Path('/opt/meson8b-vcodec/current/modules/meson-venc.ko')
expected = 'e67a83367410c859f44151c774b2290124ca1f5eb3751c1161350556a92d84e8'
state = {'status': 'starting', 'duration_requested_seconds': 1800, 'blocks': [],
         'service_checks': 0, 'encoder_sha256': expected, 'boot_changed': False}
production = json.loads(subprocess.check_output(['docker', 'inspect', 'openlist']))[0]
swapped = False
process = None


def save():
    (base / 'SUSTAINED.json').write_text(json.dumps(state, indent=2) + '\n')


def stop(signum, frame):
    raise RuntimeError('stopped by signal %d' % signum)


signal.signal(signal.SIGTERM, stop)
signal.signal(signal.SIGINT, stop)


def idle():
    assert Path('/sys/module/meson_venc/refcnt').read_text().strip() == '0'
    assert Path('/sys/kernel/debug/meson-vdec/pool_slots').read_text().strip() == '0'


def services():
    result = {}
    for port, label in [(5245, 'openlist'), (8000, 'sublink'), (7575, 'vocat'), (8123, 'homeassistant')]:
        with urllib.request.urlopen('http://127.0.0.1:%d/' % port, timeout=5) as response:
            result[label] = response.status
        assert result[label] == 200, result
    result['arcane_running'] = json.loads(subprocess.check_output(['docker', 'inspect', 'arcane-agent']))[0]['State']['Running']
    assert result['arcane_running']
    state['service_checks'] += 1
    state['services'] = result
    memory = {key: int(value) for key, value in re.findall(r'^(MemAvailable|CmaFree):\s+(\d+)',
                                                          Path('/proc/meminfo').read_text(), re.M)}
    for key, value in memory.items():
        state['minimum_' + key + '_kib'] = min(state.get('minimum_' + key + '_kib', value), value)


def stats():
    return {key: int(value) for key, value in
            (line.split() for line in Path('/sys/kernel/debug/meson-venc/rate_control').read_text().splitlines())}


def verify(path, logfile, frames):
    sizes = [int(value) for value in re.findall(r'^frame \d+ bytes (\d+)', logfile.read_text(), re.M)]
    assert len(sizes) == frames and sum(sizes) == path.stat().st_size
    rate = Fraction(30000, 1001)
    total = minimum = worst = 0
    for index, size in enumerate(sizes):
        minimum = min(minimum, total * rate.numerator - 2000000 * index * rate.denominator)
        total += size * 8
        worst = max(worst, total * rate.numerator - 2000000 * index * rate.denominator - minimum)
    assert worst < (262144 * 8 + 1) * rate.numerator
    probe = subprocess.run([str(base / 'ffprobe'), '-v', 'error', '-threads', '1', '-count_frames',
                            '-show_entries', 'stream=nb_read_frames,width,height', '-of', 'json', str(path)],
                            capture_output=True, timeout=90)
    assert probe.returncode == 0, probe.stderr.decode()[-500:]
    video = json.loads(probe.stdout)['streams'][0]
    assert int(video['nb_read_frames']) == frames and video['width'] == 640 and video['height'] == 360
    decode = subprocess.run(['/usr/local/bin/ffmpeg', '-nostdin', '-v', 'error', '-threads', '1', '-i', str(path),
                             '-threads', '1', '-f', 'null', '-'], capture_output=True, timeout=90)
    assert decode.returncode == 0 and not decode.stderr, decode.stderr.decode()[-500:]
    current = stats()
    assert current['frames'] == frames and current['budget_violations'] == 0 and current['limit_terminal'] == 0
    return {'frames': frames, 'bytes': sum(sizes), 'software_decode': True,
            'all_contiguous_intervals_verified': frames * (frames + 1) // 2,
            'largest_excess_bits': float(Fraction(worst, rate.numerator)), 'stats': current}


try:
    services()
    idle()
    assert hashlib.sha256(candidate.read_bytes()).hexdigest() == expected
    assert hashlib.sha256(original.read_bytes()).hexdigest() == 'fa17e35d50f986d2eaf922590e99a44d385496956acfe64481a211b4a61c19e2'
    subprocess.run(['rmmod', 'meson_venc'], check=True)
    swapped = True
    subprocess.run(['insmod', str(candidate)], check=True)
    started = time.monotonic()
    deadline = started + 1800
    state['status'] = 'running'
    while time.monotonic() < deadline:
        index = len(state['blocks'])
        frames = 3000
        path, logfile = base / 'sustained-current.h264', base / 'sustained-current.log'
        command = [str(base / 'encode-test'), '-p', 'motion', '-r', '30000/1001',
                   '-c', 'bitrate=1600000,peak-bitrate=2000000,cpb-size=256,gop=30',
                   '/dev/video1', str(path), '/dev/null', '640', '360', str(frames), 'NV12', '90', 'H264']
        block_start = time.monotonic()
        with logfile.open('w') as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            while process.poll() is None:
                if time.monotonic() - block_start > 300:
                    raise RuntimeError('encoder block timed out')
                services()
                state['elapsed_seconds'] = round(time.monotonic() - started, 3)
                state['current_block'] = index
                state['current_stats'] = stats()
                save()
                time.sleep(10)
            assert process.returncode == 0, process.returncode
        process = None
        result = verify(path, logfile, frames)
        result['index'] = index
        result['wall_seconds'] = round(time.monotonic() - block_start, 3)
        state['blocks'].append(result)
        state['total_frames'] = sum(b['frames'] for b in state['blocks'])
        state['elapsed_seconds'] = round(time.monotonic() - started, 3)
        save()
        print('block', index, 'PASS', 'elapsed', state['elapsed_seconds'], flush=True)
    state['status'] = 'pass'
except BaseException as error:
    state['status'] = 'fail'
    state['error'] = str(error)
    raise
finally:
    if process is not None and process.poll() is None:
        process.kill()
        process.wait(timeout=15)
    if swapped:
        idle()
        subprocess.run(['rmmod', 'meson_venc'], check=True)
        subprocess.run(['insmod', str(original)], check=True)
        state['restored'] = True
    now = json.loads(subprocess.check_output(['docker', 'inspect', 'openlist']))[0]
    state['production_start_unchanged'] = now['State']['StartedAt'] == production['State']['StartedAt'] and now['RestartCount'] == production['RestartCount']
    services()
    save()
    print(json.dumps({'status': state['status'], 'restored': state.get('restored'),
                      'production_start_unchanged': state['production_start_unchanged']}), flush=True)

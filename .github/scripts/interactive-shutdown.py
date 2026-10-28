#!/usr/bin/env python3
"""Drive the actual GTK application under Xvfb, not the test shutdown seam."""
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time

build = pathlib.Path(sys.argv[1]).resolve()
tests = json.loads(subprocess.check_output(['ctest', '--test-dir', str(build), '--show-only=json-v1']))
test = next(t for t in tests['tests'] if t['name'] == 'test-MainWindow-real-shutdown')
env = os.environ.copy()
for prop in test['properties']:
    if prop['name'] == 'ENVIRONMENT':
        for item in prop['value']:
            key, value = item.split('=', 1)
            env[key] = value
schemas = list(build.rglob('gschemas.compiled'))
if len(schemas) != 1:
    raise RuntimeError(f'expected one compiled schema directory, found {schemas}')
env['GSETTINGS_SCHEMA_DIR'] = str(schemas[0].parent)
with tempfile.TemporaryDirectory(prefix='aiw848-ui-') as home:
    env.update(HOME=home, XDG_CONFIG_HOME=home + '/config', XDG_DATA_HOME=home + '/data',
               XDG_CACHE_HOME=home + '/cache', GSETTINGS_BACKEND='keyfile', AIW848_INTERACTIVE_TEST='1')
    subprocess.run(['gsettings', 'set', 'org.gnucash.GnuCash.dialogs.new-user',
                    'first-startup', 'false'], env=env, check=True)
    log_path = pathlib.Path(home) / 'gnucash.log'
    with log_path.open('w+') as log:
        process = subprocess.Popen([str(build / 'bin/gnucash'), '--nofile'], env=env,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 120
            window = None
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(f'application exited before interaction: {process.returncode}')
                result = subprocess.run(['xdotool', 'search', '--onlyvisible', '--pid', str(process.pid),
                                         '--name', '^AIW848_INTERACTIVE_MAIN_WINDOW$'], text=True, capture_output=True)
                if result.returncode == 0 and result.stdout.strip():
                    window = result.stdout.splitlines()[-1]
                    break
                time.sleep(0.5)
            if not window:
                raise RuntimeError('no visible GnuCash window within 120 seconds')
            # Wait until the startup idle callback has had time to complete. The UI
            # must remain alive; Ctrl+Q invokes the real FileQuitAction accelerator.
            if process.poll() is not None:
                raise RuntimeError(f'application exited before Ctrl+Q: {process.returncode}')
            # The X window can map before the application's startup idle hook.
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(f'application exited before UI ready: {process.returncode}')
                if 'AIW848_UI_READY' in log_path.read_text(errors='replace'):
                    break
                time.sleep(.1)
            else:
                raise RuntimeError('main window mapped but UI readiness marker missing')
            print(f'visible GTK window {window}; UI ready; sending Ctrl+Q', flush=True)
            # X11 mapping/focus can race the GTK event loop. Retry only until the
            # action handler acknowledges the first key, never after admission.
            action_seen = False
            for attempt in range(10):
                subprocess.run(['xdotool', 'windowfocus', '--sync', window], check=True)
                focus = subprocess.check_output(['xdotool', 'getwindowfocus'], text=True).strip()
                print(f'attempt {attempt + 1}: target={window} focus={focus}', flush=True)
                if focus != window:
                    raise RuntimeError(f'input focus {focus} differs from main window {window}')
                subprocess.run(['xdotool', 'key', '--clearmodifiers', 'ctrl+q'], check=True)
                for _ in range(10):
                    time.sleep(0.2)
                    if 'AIW848_QUIT_ACTION ui_running=1' in log_path.read_text():
                        action_seen = True
                        break
                    if process.poll() is not None:
                        break
                if action_seen or process.poll() is not None:
                    break
            if not action_seen:
                raise RuntimeError('quit action not observed with running UI after bounded retries')
            status = process.wait(timeout=30)
            if status != 0:
                raise RuntimeError(f'application exited with status {status}')
            print('interactive GTK shutdown reached process exit 0', flush=True)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            log.seek(0)
            print('--- application output ---\n' + log.read(), flush=True)

"""Manual stock-Android dual-motor panel. Opening it never starts vibration."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import queue
import subprocess
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox

SERIAL = 'HA2A1PT2'
REMOTE = '/data/local/tmp/y700_motor_test_manual_v3'
EXPECTED_BINARY = 'df74a6550e30aaa98cd8a894fb81fe4e7da517497039a3c06aa3d6bb8797ee70'
ADB = Path('D:/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/adb.exe')
ROOT = Path(__file__).resolve().parents[2]
WORKBENCH = Path('D:/Desktop/Git/Patch/Y700-Elden')

def adb(*args, timeout=15):
    result = subprocess.run([str(ADB), '-s', SERIAL, *args], capture_output=True,
                            timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
    text = result.stdout.decode('utf-8', errors='replace').replace('\r', '')
    error = result.stderr.decode('utf-8', errors='replace').replace('\r', '')
    if result.returncode:
        raise RuntimeError((text + error).strip() or f'ADB error {result.returncode}')
    return text.strip()

class Panel:
    def __init__(self, root, log_dir):
        self.root = root; self.queue = queue.Queue(); self.cancel = threading.Event()
        self.busy = False; self.closing = False
        log_dir.mkdir(parents=True, exist_ok=True)
        self.log = log_dir / (datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.jsonl')
        root.title('Y700 双马达独立测试 · Android')
        root.geometry('760x660'); root.minsize(680, 600)
        root.configure(bg='#f4f6fa')
        style = ttk.Style(); style.theme_use('clam')
        style.configure('TButton', font=('Microsoft YaHei UI', 14), padding=12)
        style.configure('TLabel', font=('Microsoft YaHei UI', 11), background='#f4f6fa')
        style.configure('TRadiobutton', font=('Microsoft YaHei UI', 11), background='#f4f6fa')
        main = ttk.Frame(root, padding=22); main.pack(fill='both', expand=True)
        ttk.Label(main, text='Y700 · 两颗马达分开感受', font=('Microsoft YaHei UI', 21, 'bold')).pack(anchor='w')
        ttk.Label(main, text='点击后倒数 3 秒。单测各三下；交替为 L → R → L → R → L → R。', wraplength=700).pack(anchor='w', pady=(10, 6))
        ttk.Label(main, text='L / R 是原厂驱动标签；请按手感确认机身位置。轻握平板，测试时先不要操作屏幕。', wraplength=700).pack(anchor='w', pady=(0, 14))
        self.level = tk.StringVar(value='medium')
        row = ttk.Frame(main); row.pack(fill='x')
        ttk.Radiobutton(row, text='轻柔：250 ms × 3，增益 32', variable=self.level, value='gentle').pack(side='left')
        ttk.Radiobutton(row, text='中等：350 ms × 3，增益 64', variable=self.level, value='medium').pack(side='left', padx=14)
        ttk.Label(main, text='原厂当前增益为 128；本工具只用 32 / 64，不改电压、频率或校准。', wraplength=700).pack(anchor='w', pady=(8, 16))
        row = ttk.Frame(main); row.pack(fill='x')
        self.buttons = []
        for label, action in [('只测 L 马达', 'l'), ('只测 R 马达', 'r'), ('L ↔ R 逐次交替', 'both')]:
            button = ttk.Button(row, text=label, command=lambda value=action: self.start(value))
            button.pack(side='left', expand=True, fill='x', padx=3); self.buttons.append(button)
        row = ttk.Frame(main); row.pack(fill='x', pady=12)
        self.check = ttk.Button(row, text='检查连接（不振动）', command=lambda: self.start('survey'))
        self.check.pack(side='left', expand=True, fill='x', padx=3); self.buttons.append(self.check)
        ttk.Button(row, text='立即停止', command=self.stop).pack(side='left', expand=True, fill='x', padx=3)
        self.status = tk.StringVar(value='待机 · 拿好平板后点击 L 或 R')
        ttk.Label(main, textvariable=self.status, font=('Microsoft YaHei UI', 14, 'bold'), wraplength=700).pack(anchor='w', pady=10)
        self.output = tk.Text(main, height=12, font=('Consolas', 10), wrap='word', state='disabled')
        self.output.pack(fill='both', expand=True)
        ttk.Label(main, text='“TIMER other=0ms”表示另一路没有运行。命令成功不等于已证明机械强度一致。', wraplength=700).pack(anchor='w', pady=(10, 0))
        root.protocol('WM_DELETE_WINDOW', self.close)
        root.after(80, self.poll)
        self.emit('status', '待机 · 拿好平板后点击 L 或 R')

    def emit(self, kind, text):
        self.queue.put((kind, text))

    def record(self, kind, text):
        with self.log.open('a', encoding='utf-8') as stream:
            stream.write(json.dumps({'time': datetime.datetime.now().isoformat(), 'kind': kind, 'text': text}, ensure_ascii=False) + '\n')

    def start(self, action):
        if self.busy: return
        self.busy = True; self.cancel.clear()
        for button in self.buttons: button.configure(state='disabled')
        level = self.level.get()
        threading.Thread(target=self.work, args=(action, level), daemon=True).start()

    def delay(self, seconds, label):
        for remaining in range(seconds, 0, -1):
            self.emit('status', f'{label} · {remaining} 秒后开始')
            if self.cancel.wait(1): return False
        return not self.cancel.is_set()

    def work(self, action, level):
        try:
            self.emit('status', '正在核对 Y700 和测试程序…')
            if adb('shell', 'getprop', 'ro.product.model') != 'TB322FC' or adb('shell', 'getprop', 'sys.boot_completed') != '1':
                raise RuntimeError('需要这台 Y700 的原厂 Android；不会重启或刷机。')
            digest = adb('shell', f"su -c 'sha256sum {REMOTE}'").split()[0]
            if digest != EXPECTED_BINARY: raise RuntimeError('测试程序摘要不符，请重新部署。')
            survey = adb('shell', f"su -c '{REMOTE} survey'")
            self.emit('log', survey)
            if action == 'survey': self.emit('status', '两路接口已识别 · 没有触发振动'); return
            if level not in ('gentle', 'medium'): raise RuntimeError('未知强度')
            sides = ('l', 'r') * 3 if action == 'both' else (action,)
            count = 1 if action == 'both' else 3
            label = 'L → R → L → R → L → R' if action == 'both' else action.upper()
            if not self.delay(3, '拿好平板：即将测试 ' + label):
                self.emit('status', '已取消 · 没有触发振动'); return
            for step, side in enumerate(sides, 1):
                if side not in ('l','r'): raise RuntimeError('未知马达')
                if self.cancel.is_set(): break
                state = adb('shell', 'dumpsys', 'vibrator_manager')
                if 'currentState = IDLE' not in state or 'currentState = VIBRATING' in state:
                    raise RuntimeError('Android 正在使用马达，请停下屏幕操作后重试。')
                if self.cancel.is_set(): break
                self.emit('status', f'交替 {step}/6 · {side.upper()} 振一下' if action == 'both' else f'正在测试 {side.upper()} · 三次短振动')
                process = subprocess.Popen([str(ADB), '-s', SERIAL, 'shell', f"su -c '{REMOTE} {side} {level} {count}'"],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace',
                    creationflags=subprocess.CREATE_NO_WINDOW)
                # The target has its own eight-second limit plus hrtimer stop.
                # A separate desktop timer asks that same target process to clean up.
                watchdog = threading.Timer(12, self.stop); watchdog.daemon = True; watchdog.start()
                try:
                    for line in process.stdout:
                        line = line.strip()
                        if line: self.emit('log', line)
                    code = process.wait(timeout=15)
                finally:
                    watchdog.cancel()
                if code and not self.cancel.is_set(): raise RuntimeError(f'{side.upper()} 返回 {code}，详见上方记录。')
                if self.cancel.is_set(): break
            self.emit('status', '已停止 · 已请求恢复原设置' if self.cancel.is_set() else '本轮结束 · 原设置已恢复，可再次点击')
        except Exception as error:
            self.emit('error', str(error))
        finally:
            self.emit('done', '')

    def stop(self):
        if not self.busy: return
        self.cancel.set(); self.emit('status', '正在停止并恢复设置…')
        def remote_stop():
            try: self.emit('log', adb('shell', f"su -c '{REMOTE} stop'", timeout=10))
            except Exception as error: self.emit('log', '停止请求：' + str(error))
        threading.Thread(target=remote_stop, daemon=True).start()

    def close(self):
        if self.busy:
            self.closing = True; self.stop()
        else: self.root.destroy()

    def poll(self):
        while True:
            try: kind, text = self.queue.get_nowait()
            except queue.Empty: break
            self.record(kind, text)
            if kind in ('status', 'error'): self.status.set(text)
            if kind in ('log', 'error'):
                self.output.configure(state='normal'); self.output.insert('end', text + '\n')
                self.output.see('end'); self.output.configure(state='disabled')
            if kind == 'done':
                self.busy = False
                for button in self.buttons: button.configure(state='normal')
                if self.closing: self.root.destroy(); return
        self.root.after(80, self.poll)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--smoke', action='store_true', help='Build widgets then close without ADB or vibration')
    args = parser.parse_args()
    root = tk.Tk(); panel = Panel(root, WORKBENCH/'lab/records/motor-test-20261114/manual-sessions')
    if args.smoke:
        assert len(panel.buttons) == 4 and panel.level.get() == 'medium' and not panel.busy
        root.after(400, root.destroy)
    root.mainloop()

if __name__ == '__main__': main()

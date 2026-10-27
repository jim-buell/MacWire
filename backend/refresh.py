"""One worker for scheduled and client-requested feed checks."""
import secrets
import threading
import time
from collections import OrderedDict


class FeedRefreshManager:
    def __init__(self, fetch, interval=3600, cooldown=60):
        self.fetch = fetch
        self.interval = interval
        self.cooldown = cooldown
        self.lock = threading.Lock()
        self.wake = threading.Event()
        self.stopped = threading.Event()
        self.jobs = OrderedDict()
        self.current = None
        self.pending = None
        self.thread = threading.Thread(target=self._run, daemon=True)

    def _new_job(self, force):
        token = secrets.token_hex(4)
        while token in self.jobs:
            token = secrets.token_hex(4)
        self.jobs[token] = [True, 0, time.monotonic()]
        self.current = token
        self.pending = (token, force)
        while len(self.jobs) > 8:
            self.jobs.popitem(last=False)
        return token

    def start(self):
        with self.lock:
            self._new_job(False)
        self.wake.set()
        self.thread.start()

    def request(self):
        with self.lock:
            if self.current:
                running, _, started = self.jobs[self.current]
                if running or time.monotonic() - started < self.cooldown:
                    return self.current
            token = self._new_job(True)
            self.wake.set()
            return token

    def status(self, token):
        with self.lock:
            if token not in self.jobs:
                raise KeyError(token)
            return tuple(self.jobs[token][:2])

    def stop(self):
        self.stopped.set()
        self.wake.set()
        self.thread.join(timeout=2)

    def _run(self):
        while not self.stopped.is_set():
            self.wake.wait(self.interval)
            with self.lock:
                self.wake.clear()
                if self.stopped.is_set():
                    return
                if self.pending is None:
                    self._new_job(False)
                token, force = self.pending
                self.pending = None
            try:
                failures = self.fetch(force)
            except Exception as error:
                print(f"[MacWire] Feed update failed: {error}")
                failures = 1
            with self.lock:
                self.jobs[token][0] = False
                self.jobs[token][1] = min(65535, failures)

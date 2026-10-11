
import argparse
import threading
import time
import requests
from concurrent.futures import ThreadPoolExecutor, as_completed

LOCAL_PROXY = "http://127.0.0.1:8888"
_thread_local = threading.local()


def get_session():
    if not hasattr(_thread_local, "session"):
        session = requests.Session()
        session.trust_env = False
        session.proxies = {
            "http": LOCAL_PROXY,
            "https": LOCAL_PROXY,
        }
        _thread_local.session = session
    return _thread_local.session


def make_request(url):
    start = time.perf_counter()
    try:
        response = get_session().get(url, timeout=10)
        status = response.status_code
        response.close()
        latency_ms = (time.perf_counter() - start) * 1000
        return latency_ms, status, None
    except Exception as exc:
        latency_ms = (time.perf_counter() - start) * 1000
        return latency_ms, None, str(exc)


def percentile(values, pct):
    values = sorted(values)
    index = min(int((pct / 100) * len(values)), len(values) - 1)
    return values[index]


parser = argparse.ArgumentParser()
parser.add_argument("--url",
                    default="http://host.docker.internal:9000/")
parser.add_argument("--requests", type=int, default=1000)
parser.add_argument("--workers", type=int, default=25)
args = parser.parse_args()

# Warm up connections; do not include these in the measurements.
for _ in range(10):
    latency, status, error = make_request(args.url)
    if error or status != 200:
        raise SystemExit(
            f"Warm-up failed: status={status}, error={error}"
        )

latencies = []
successes = 0
failures = 0
start = time.perf_counter()

with ThreadPoolExecutor(max_workers=args.workers) as executor:
    futures = [
        executor.submit(make_request, args.url)
        for _ in range(args.requests)
    ]

    for future in as_completed(futures):
        latency, status, error = future.result()
        latencies.append(latency)

        if status == 200:
            successes += 1
        else:
            failures += 1

elapsed = time.perf_counter() - start

print(f"Workers:       {args.workers}")
print(f"Requests:      {args.requests}")
print(f"Successful:    {successes}")
print(f"Failed:        {failures}")
print(f"Duration:      {elapsed:.2f} seconds")
print(f"Throughput:    {successes / elapsed:.2f} successful req/s")

if latencies:
    print(f"p50 latency:   {percentile(latencies, 50):.2f} ms")
    print(f"p95 latency:   {percentile(latencies, 95):.2f} ms")
    print(f"p99 latency:   {percentile(latencies, 99):.2f} ms")

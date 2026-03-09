# Frame Events → Evidence Request Forwarder

Subscribes to `worker_lsr_frame_events` and forwards to `worker_lsr_evidence_request`.

## Run

```bash
docker run --rm --network host \
  -v $(pwd):/app \
  -w /app \
  python:3.11-slim \
  bash -c "pip install redis -q && python3 frame_events_to_evidence_request.py"
```

Custom Redis:

```bash
docker run --rm --network host \
  -v $(pwd):/app \
  -w /app \
  python:3.11-slim \
  bash -c "pip install redis -q && python3 frame_events_to_evidence_request.py --host 192.168.1.100 --port 6380 --db 0"
```

## Args

- `--host` Redis host (default: 192.168.1.99)
- `--port` Redis port (default: 6319)
- `--db` Redis database (default: 0)

## Output

```
2026-03-09 14:25:03 [INFO] Redis: 192.168.1.99:6319 (DB 0)
2026-03-09 14:25:03 [INFO] Source: worker_lsr_frame_events → worker_lsr_evidence_request
2026-03-09 14:25:03 [INFO] ✓ Connected to Redis
2026-03-09 14:25:03 [INFO] Starting consumer
2026-03-09 14:25:05 [INFO] Forwarded 1773040413091-0 | request_id: a1b2c3d4... | objects: 1
2026-03-09 14:25:06 [INFO] Forwarded 1773040414512-0 | request_id: e5f6g7h8... | objects: 2
```

## What it does

- Subscribes to `worker_lsr_frame_events` Redis stream
- For each message:
  - Generates unique `request_id` (UUID)
  - Sets `evidence_types: ["overview", "crop"]`
  - Publishes to `worker_lsr_evidence_request`
- Logs each forward

## Stop

Press `Ctrl+C`

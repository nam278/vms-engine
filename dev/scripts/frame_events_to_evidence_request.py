#!/usr/bin/env python3
"""
Subscribe to worker_lsr_frame_events Redis stream and forward all messages
as evidence_request payloads to worker_lsr_evidence_request.

Usage:
    python frame_events_to_evidence_request.py [--host HOST] [--port PORT] [--db DB]

Example:
    python frame_events_to_evidence_request.py --host 192.168.1.99 --port 6319
"""

import redis
import json
import argparse
import uuid
import logging
import sys
from typing import Dict, Any, Optional

# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
    stream=sys.stdout,
)
logger = logging.getLogger(__name__)

# Configuration
DEFAULT_HOST = "192.168.1.99"
DEFAULT_PORT = 6319
DEFAULT_DB = 0

SOURCE_STREAM = "worker_lsr_frame_events"
TARGET_STREAM = "worker_lsr_evidence_request"


def parse_args():
    parser = argparse.ArgumentParser(
        description="Forward frame_events to evidence_request"
    )
    parser.add_argument(
        "--host", default=DEFAULT_HOST, help=f"Redis host (default: {DEFAULT_HOST})"
    )
    parser.add_argument(
        "--port",
        type=int,
        default=DEFAULT_PORT,
        help=f"Redis port (default: {DEFAULT_PORT})",
    )
    parser.add_argument(
        "--db", type=int, default=DEFAULT_DB, help=f"Redis DB (default: {DEFAULT_DB})"
    )
    return parser.parse_args()


def parse_frame_event(msg_data: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    """Parse a frame_events message and convert to evidence_request format."""
    try:
        # Extract core fields
        schema_version = msg_data.get("schema_version", "1.0")
        pipeline_id = msg_data.get("pipeline_id", "")
        source_name = msg_data.get("source_name", "")
        source_id = msg_data.get("source_id", "0")
        frame_key = msg_data.get("frame_key", "")
        frame_ts_ms = msg_data.get("frame_ts_ms", "0")
        overview_ref = msg_data.get("overview_ref", "")

        # Parse objects array (stored as JSON string in Redis)
        objects_str = msg_data.get("objects", "[]")
        if isinstance(objects_str, str):
            objects = json.loads(objects_str)
        else:
            objects = objects_str

        # Create evidence request
        request_id = str(uuid.uuid4())
        evidence_request = {
            "schema_version": schema_version,
            "request_id": request_id,
            "pipeline_id": pipeline_id,
            "source_name": source_name,
            "source_id": source_id,
            "frame_key": frame_key,
            "frame_ts_ms": frame_ts_ms,
            "overview_ref": overview_ref,
            # "evidence_types": json.dumps(["overview", "crop"]),
            "evidence_types": json.dumps(["overview"]),
            "objects": json.dumps(objects),
            "raw_payload": msg_data.get("raw_payload", ""),
        }

        return evidence_request

    except Exception as e:
        logger.error(f"Error parsing frame_event: {e}", exc_info=True)
        return None


def stream_consumer(
    redis_client: redis.Redis,
    source_stream: str,
    target_stream: str,
    batch_size: int = 100,
):
    """
    Consume messages from source stream and forward to target stream.

    Args:
        redis_client: Redis client instance
        source_stream: Source stream name to read from
        target_stream: Target stream name to write to
        batch_size: Read batch size per iteration
    """
    last_id = "0"
    message_count = 0

    logger.info(f"Starting consumer: {source_stream} → {target_stream}")

    try:
        while True:
            # Read batch from source stream
            messages = redis_client.xread(
                {source_stream: last_id}, count=batch_size, block=1000
            )

            if not messages:
                if message_count > 0:
                    logger.debug(f"Waiting for new data...")
                continue

            # Process each message
            for stream_name, stream_messages in messages:
                for msg_id, msg_data in stream_messages:
                    last_id = msg_id

                    # Parse frame_event
                    evidence_req = parse_frame_event(msg_data)
                    if not evidence_req:
                        continue

                    # Publish to target stream
                    try:
                        result_id = redis_client.xadd(target_stream, evidence_req)
                        message_count += 1
                        logger.info(
                            f"Forwarded {msg_id.decode() if isinstance(msg_id, bytes) else msg_id} | "
                            f"request_id: {evidence_req['request_id'][:8]}... | "
                            f"objects: {len(json.loads(evidence_req['objects']))}"
                        )

                    except Exception as e:
                        logger.error(
                            f"Error publishing to {target_stream}: {e}", exc_info=True
                        )
                        continue

    except KeyboardInterrupt:
        logger.info(f"Stopped. Processed {message_count} messages total.")


def main():
    args = parse_args()

    logger.info(f"Redis: {args.host}:{args.port} (DB {args.db})")
    logger.info(f"Source: {SOURCE_STREAM} → Target: {TARGET_STREAM}")

    try:
        r = redis.Redis(
            host=args.host,
            port=args.port,
            db=args.db,
            decode_responses=True,
        )

        r.ping()
        logger.info("✓ Connected to Redis")
        stream_consumer(r, SOURCE_STREAM, TARGET_STREAM)

    except redis.ConnectionError as e:
        logger.error(f"Failed to connect to Redis: {e}")
        return 1
    except Exception as e:
        logger.error(f"Unexpected error: {e}", exc_info=True)
        return 1

    return 0


if __name__ == "__main__":
    exit(main())

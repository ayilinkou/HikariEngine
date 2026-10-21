"""Shared implementation for Hikari's standalone developer tools."""

import sys

if sys.version_info < (3, 12):
    raise SystemExit("Hikari developer tools require Python 3.12 or newer.")

#!/usr/bin/env python3
"""Compile public and engine-private headers independently, without PCH."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from hikari_tools.common import build_tool, entrypoint


def main(argv=None):
    return build_tool(__doc__, target="HeaderSelfContainment", argv=argv)


if __name__ == "__main__":
    raise SystemExit(entrypoint(main))


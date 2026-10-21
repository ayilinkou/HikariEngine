#!/usr/bin/env python3
"""Check C++ formatting without a configured build tree."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from hikari_tools.common import Parser, entrypoint
from hikari_tools.formatting import format_tree


def main(argv=None):
    Parser(description=__doc__).parse_args(argv)
    return format_tree()


if __name__ == "__main__":
    raise SystemExit(entrypoint(main))

#!/usr/bin/env python3
"""Format the repository's C++ sources using the pinned clang-format policy."""

from hikari_tools.common import Parser, ToolError, entrypoint
from hikari_tools.formatting import find_formatter, format_tree


def main(argv=None):
    parser = Parser(description=__doc__)
    parser.add_argument("--check-version", action="store_true", help="Report formatter availability/version only")
    args = parser.parse_args(argv)
    if args.check_version:
        try:
            find_formatter()
        except ToolError as error:
            print(f"format: {error}")
        return 0
    return format_tree(fix=True)


if __name__ == "__main__":
    raise SystemExit(entrypoint(main))

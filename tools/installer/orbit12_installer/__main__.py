"""Allows `python -m orbit12_installer`."""

import sys

from .cli import main

if __name__ == "__main__":
    sys.exit(main())

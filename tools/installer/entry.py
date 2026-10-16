"""PyInstaller entry script. Kept outside the package so the frozen program starts the CLI directly."""

import sys

from orbit12_installer.cli import main

if __name__ == "__main__":
    sys.exit(main())

"""
SacPin Charger Control - Main Entry Point

Usage:
    python main.py

Requirements:
    pip install -r requirements.txt
"""

import sys
import logging
from PyQt5.QtWidgets import QApplication

from views.main_window import MainWindow


def main():
    """Main entry point"""
    # Setup logging
    logging.basicConfig(
        level=logging.DEBUG,
        format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
    )

    logger = logging.getLogger(__name__)
    logger.info("Starting SacPin Charger Control...")

    # Create application
    app = QApplication(sys.argv)
    app.setStyle("Fusion")

    # Set application info
    app.setApplicationName("SacPin Charger Control")
    app.setApplicationVersion("1.0.0")

    # Create and show window
    window = MainWindow()
    window.show()

    logger.info("Application started")

    # Run event loop
    exit_code = app.exec()

    logger.info(f"Application exited with code {exit_code}")
    sys.exit(exit_code)


if __name__ == "__main__":
    main()

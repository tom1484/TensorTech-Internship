#!/bin/bash
#
# install_deps.sh - Install dependencies for thermo-cli
# For Raspberry Pi OS (Debian-based systems)
#

set -e

echo "================================================"
echo "  Thermo CLI - Dependency Installation Script"
echo "================================================"
echo ""

# Check if running on Raspberry Pi or Debian-based system
if [ ! -f /etc/debian_version ]; then
    echo "Warning: This script is designed for Debian-based systems (Raspberry Pi OS)"
    read -p "Continue anyway? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Update package list
echo "[1/3] Updating package list..."
sudo apt update

# Install libyaml development library
echo ""
echo "[2/3] Installing libyaml-dev..."
sudo apt install -y libyaml-dev

# Check for daqhats library
echo ""
echo "[3/3] Checking for daqhats library..."
if ldconfig -p | grep -q libdaqhats; then
    echo "✓ libdaqhats is already installed"
else
    echo "⚠ libdaqhats not found!"
    echo ""
    echo "The MCC DAQ HAT library is required but not installed."
    echo "Please install it from: https://github.com/mccdaq/daqhats"
    echo ""
    echo "Quick install (if not already done):"
    echo "  git clone https://github.com/mccdaq/daqhats.git"
    echo "  cd daqhats"
    echo "  sudo ./install.sh"
    echo ""
    read -p "Continue anyway? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# cJSON is vendored, no need to install
echo ""
echo "✓ cJSON is vendored (no installation needed)"

echo ""
echo "================================================"
echo "  Dependencies installed successfully!"
echo "================================================"
echo ""
echo "You can now build thermo-cli:"
echo "  make"
echo ""
echo "To install system-wide:"
echo "  sudo make install"
echo ""

#!/usr/bin/env python3
"""
Deploy thermo-cli (C version) to a remote Raspberry Pi.

This script handles:
- Syncing project files via rsync
- Installing build dependencies (libyaml-dev, daqhats)
- Building the C project
- Installing the thermo-cli binary

Usage:
    python scripts/deploy_rpi_c.py pi@192.168.1.100
    python scripts/deploy_rpi_c.py pi@192.168.1.100 --remote-path /opt/thermo-cli
    python scripts/deploy_rpi_c.py pi@192.168.1.100 --password YOUR_PASSWORD
"""

import argparse
import subprocess
import sys
from pathlib import Path

# Project root (parent of scripts/)
PROJECT_ROOT = Path(__file__).resolve().parent.parent
LOCAL_HOME = Path.home()

# Files/directories to sync
SYNC_ITEMS = [
    "src/",
    "include/",
    "vendor/",
    "Makefile",
    "install_deps.sh",
    "README_C.md",
    "thermo_config.yaml",
]

# Files/directories to exclude from sync
EXCLUDE_PATTERNS = [
    "__pycache__",
    "*.pyc",
    ".git",
    ".venv",
    "*.egg-info",
    "*.o",
    "thermo-cli",
    "thermo/",          # Exclude Python source
    "pyproject.toml",   # Exclude Python package config
    "requirements.txt", # Exclude Python requirements
]


def to_remote_path(path: str) -> str:
    """
    Convert a path that may have been expanded locally to a remote-friendly path.
    
    If the shell expanded ~ to the local home directory, convert it back to ~
    so it expands correctly on the remote host.
    """
    local_home_str = str(LOCAL_HOME)
    if path.startswith(local_home_str + "/") or path == local_home_str:
        # Replace local home with ~ for remote expansion
        return "~" + path[len(local_home_str):]
    return path


def run_cmd(cmd: list[str], check: bool = True, capture: bool = False) -> subprocess.CompletedProcess:
    """Run a command and optionally check for errors."""
    print(f"[CMD] {' '.join(cmd)}")
    return subprocess.run(
        cmd,
        check=check,
        capture_output=capture,
        text=True,
    )


def run_ssh(host: str, command: str, check: bool = True) -> subprocess.CompletedProcess:
    """Run a command on the remote host via SSH."""
    return run_cmd(["ssh", host, command], check=check)


def sync_files(host: str, remote_path: str) -> None:
    """Sync project files to the remote host using rsync."""
    print("\n=== Syncing files to remote ===")

    # Build rsync command
    rsync_cmd = [
        "rsync",
        "-avz",
        "--progress",
        "--delete",
    ]

    # Add exclude patterns
    for pattern in EXCLUDE_PATTERNS:
        rsync_cmd.extend(["--exclude", pattern])

    # Add source files
    for item in SYNC_ITEMS:
        src = PROJECT_ROOT / item
        if src.exists():
            rsync_cmd.append(str(src))
        else:
            print(f"[WARN] {item} not found, skipping")

    # Add destination
    rsync_cmd.append(f"{host}:{remote_path}/")

    run_cmd(rsync_cmd)


def install_dependencies(host: str, password: str, remote_path: str) -> None:
    """Install build dependencies on the remote host."""
    print("\n=== Installing dependencies ===")
    
    # Update package list
    print("[INFO] Updating package list...")
    run_ssh(host, f"echo '{password}' | sudo -S apt update", check=False)
    
    # Install libyaml-dev
    print("[INFO] Installing libyaml-dev...")
    run_ssh(host, f"echo '{password}' | sudo -S apt install -y libyaml-dev", check=False)
    
    # Check if daqhats is installed
    print("[INFO] Checking for daqhats library...")
    result = run_ssh(host, "ldconfig -p | grep -q libdaqhats && echo 'installed' || echo 'missing'", check=False)
    
    if result.returncode != 0 or 'missing' in (result.stdout or ''):
        print("[INFO] Installing daqhats library...")
        
        # Clone daqhats if not exists
        run_ssh(host, "test -d daqhats || git clone https://github.com/mccdaq/daqhats.git", check=False)
        
        # Install daqhats
        run_ssh(host, f"cd daqhats && echo '{password}' | sudo -S ./install.sh", check=False)
    else:
        print("[INFO] daqhats already installed")


def build_on_remote(host: str, remote_path: str) -> None:
    """Build the C project on the remote host."""
    print("\n=== Building thermo-cli ===")
    
    # Clean previous build
    print("[INFO] Cleaning previous build...")
    run_ssh(host, f"cd {remote_path} && make clean", check=False)
    
    # Build project
    print("[INFO] Building project...")
    run_ssh(host, f"cd {remote_path} && make")


def install_on_remote(host: str, password: str, remote_path: str) -> None:
    """Install the thermo-cli binary on the remote host."""
    print("\n=== Installing thermo-cli ===")
    
    # Install to /usr/local/bin
    run_ssh(host, f"cd {remote_path} && echo '{password}' | sudo -S make install")
    
    # Verify installation
    print("\n=== Verifying installation ===")
    result = run_ssh(host, "thermo-cli --version", check=False)
    
    if result.returncode == 0:
        print("[SUCCESS] thermo-cli installed successfully!")
    else:
        print("[WARN] Installation verification failed")


def setup_remote_directory(host: str, remote_path: str) -> None:
    """Create the remote directory if it doesn't exist."""
    print(f"\n=== Setting up remote directory: {remote_path} ===")
    run_ssh(host, f"mkdir -p {remote_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Deploy thermo-cli (C version) to a remote Raspberry Pi",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    %(prog)s pi@192.168.1.100 --password raspberry
    %(prog)s pi@192.168.1.100 --remote-path /opt/thermo-cli --password raspberry
    %(prog)s pi@192.168.1.100 --sync-only
    %(prog)s pi@192.168.1.100 --build-only --password raspberry
        """,
    )

    parser.add_argument(
        "host",
        help="Remote host in user@host format (e.g., pi@192.168.1.100)",
    )
    parser.add_argument(
        "--password",
        default=None,
        help="Remote sudo password (required for installation)",
    )
    parser.add_argument(
        "--remote-path",
        default="~/thermo-cli",
        help="Build path on the remote host (default: ~/thermo-cli)",
    )
    parser.add_argument(
        "--sync-only",
        action="store_true",
        help="Only sync files, don't build or install",
    )
    parser.add_argument(
        "--build-only",
        action="store_true",
        help="Only build, don't install (requires --password for dependencies)",
    )
    parser.add_argument(
        "--no-sync",
        action="store_true",
        help="Skip file sync, only build and install",
    )
    parser.add_argument(
        "--no-deps",
        action="store_true",
        help="Skip dependency installation (assumes already installed)",
    )

    args = parser.parse_args()

    # Convert paths that may have been locally expanded back to remote-friendly paths
    args.remote_path = to_remote_path(args.remote_path)

    # Validate host format
    if "@" not in args.host:
        print("[ERROR] Host must be in user@host format (e.g., pi@192.168.1.100)")
        sys.exit(1)

    # Validate password requirement
    if not args.sync_only and not args.password:
        print("[ERROR] --password is required for building/installing")
        print("        (needed for sudo apt install and make install)")
        sys.exit(1)

    print(f"Deploying thermo-cli (C version) to {args.host}:{args.remote_path}")
    print(f"Project root: {PROJECT_ROOT}")

    try:
        # Setup remote directory
        setup_remote_directory(args.host, args.remote_path)

        # Sync files
        if not args.no_sync:
            sync_files(args.host, args.remote_path)

        if args.sync_only:
            print("\n=== Sync complete (--sync-only) ===")
            return

        # Install dependencies
        if not args.no_deps:
            install_dependencies(args.host, args.password, args.remote_path)

        # Build project
        build_on_remote(args.host, args.remote_path)

        if args.build_only:
            print("\n=== Build complete (--build-only) ===")
            print(f"To install, run: ssh {args.host} 'cd {args.remote_path} && sudo make install'")
            return

        # Install binary
        install_on_remote(args.host, args.password, args.remote_path)

        print("\n=== Deployment complete! ===")
        print(f"Run: ssh {args.host} thermo-cli --help")
        print(f"Test: ssh {args.host} thermo-cli list")

    except subprocess.CalledProcessError as e:
        print(f"\n[ERROR] Command failed with exit code {e.returncode}")
        if e.stdout:
            print(f"STDOUT: {e.stdout}")
        if e.stderr:
            print(f"STDERR: {e.stderr}")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\n[INTERRUPTED] Deployment cancelled")
        sys.exit(130)


if __name__ == "__main__":
    main()

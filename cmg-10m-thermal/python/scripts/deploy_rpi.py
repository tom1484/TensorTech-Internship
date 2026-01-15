#!/usr/bin/env python3
"""
Deploy thermo CLI to a remote Raspberry Pi.

This script handles:
- Syncing project files via rsync
- Installing dependencies
- Installing the thermo CLI package

Usage:
    python scripts/deploy_rpi.py pi@192.168.1.100
    python scripts/deploy_rpi.py pi@192.168.1.100 --remote-path /opt/thermo
    python scripts/deploy_rpi.py pi@192.168.1.100 --venv ~/.venv/thermo-cli
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
    "thermo/",
    "pyproject.toml",
    "requirements.txt",
]

# Files/directories to exclude from sync
EXCLUDE_PATTERNS = [
    "__pycache__",
    "*.pyc",
    ".git",
    ".venv",
    "*.egg-info",
    "daqhats/",
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


def install_on_remote(host: str, password: str, remote_path: str, venv_path: str | None = None) -> None:
    """Install the thermo CLI on the remote host."""
    print("\n=== Installing on remote ===")

    # Install daqhats' dependencies
    run_ssh(host, f"git clone https://github.com/mccdaq/daqhats.git", check=False)
    run_ssh(host, f"cd daqhats && echo '{password}' | sudo -S ./install.sh", check=False)

    if venv_path:
        python_bin = f"{venv_path}/bin/python"
        pip_cmd = f"{venv_path}/bin/pip"
        thermo_cmd = f"{venv_path}/bin/thermo"

        # Create virtualenv if it doesn't exist
        print(f"[INFO] Using virtualenv: {venv_path}")
        run_ssh(host, f"python3 -m venv {venv_path} 2>/dev/null || true", check=False)

        # Install the package in editable mode
        install_cmd = f"cd {remote_path} && {pip_cmd} install -e ."
    else:
        python_bin = "python3"
        thermo_cmd = "thermo"

        # Ensure pip is available and up to date
        run_ssh(host, "python3 -m pip install --upgrade pip", check=False)

        # Install the package in editable mode
        install_cmd = f"cd {remote_path} && python3 -m pip install -e ."

    run_ssh(host, install_cmd)

    # Verify installation
    print("\n=== Verifying installation ===")
    run_ssh(host, f"{thermo_cmd} --help", check=False)


def setup_remote_directory(host: str, remote_path: str) -> None:
    """Create the remote directory if it doesn't exist."""
    print(f"\n=== Setting up remote directory: {remote_path} ===")
    run_ssh(host, f"mkdir -p {remote_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Deploy thermo CLI to a remote Raspberry Pi",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    %(prog)s pi@192.168.1.100
    %(prog)s pi@192.168.1.100 --remote-path /opt/thermo
    %(prog)s pi@192.168.1.100 --venv ~/.venv/thermo-cli
    %(prog)s pi@192.168.1.100 --sync-only
        """,
    )

    parser.add_argument(
        "host",
        help="Remote host in user@host format (e.g., pi@192.168.1.100)",
    )
    parser.add_argument(
        "--password",
        default=None,
        help="Remote password (required for installation)",
    )
    parser.add_argument(
        "--remote-path",
        default="~/thermo-cli",
        help="Installation path on the remote host (default: ~/thermo-cli)",
    )
    parser.add_argument(
        "--venv",
        default=None,
        help="Virtualenv path on remote (e.g., ~/.venv/thermo-cli). Creates if not exists.",
    )
    parser.add_argument(
        "--sync-only",
        action="store_true",
        help="Only sync files, don't install",
    )
    parser.add_argument(
        "--no-sync",
        action="store_true",
        help="Skip file sync, only run installation",
    )

    args = parser.parse_args()

    # Convert paths that may have been locally expanded back to remote-friendly paths
    args.remote_path = to_remote_path(args.remote_path)
    if args.venv:
        args.venv = to_remote_path(args.venv)

    # Validate host format
    if "@" not in args.host:
        print("[ERROR] Host must be in user@host format (e.g., pi@192.168.1.100)")
        sys.exit(1)

    print(f"Deploying thermo CLI to {args.host}:{args.remote_path}")
    if args.venv:
        print(f"Using virtualenv: {args.venv}")
    print(f"Project root: {PROJECT_ROOT}")

    try:
        # Setup remote directory
        setup_remote_directory(args.host, args.remote_path)

        # Sync files
        if not args.no_sync:
            sync_files(args.host, args.remote_path)

        if not args.password:
            print("[ERROR] Password is required for installation")
            sys.exit(1)

        if args.sync_only:
            print("\n=== Sync complete (--sync-only) ===")
            return

        # Install thermo CLI
        install_on_remote(args.host, args.password, args.remote_path, args.venv)

        print("\n=== Deployment complete! ===")
        if args.venv:
            print(f"Run 'ssh {args.host} {args.venv}/bin/thermo --help' to verify")
        else:
            print(f"Run 'ssh {args.host} thermo --help' to verify")

    except subprocess.CalledProcessError as e:
        print(f"\n[ERROR] Command failed with exit code {e.returncode}")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\n[INTERRUPTED] Deployment cancelled")
        sys.exit(130)


if __name__ == "__main__":
    main()

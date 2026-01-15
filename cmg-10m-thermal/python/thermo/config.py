"""
Configuration loading and validation for thermo CLI.

Handles YAML configuration files for multi-source thermal setups.
"""

from pathlib import Path
from typing import Any

import yaml


def load_config(config_path: str) -> dict[str, Any]:
    """
    Load and validate a YAML configuration file.

    Args:
        config_path: Path to the YAML config file

    Returns:
        Parsed configuration dictionary

    Raises:
        FileNotFoundError: If config file doesn't exist
        ValueError: If config file is invalid
    """
    path = Path(config_path)
    if not path.exists():
        raise FileNotFoundError(f"Config file not found: {config_path}")

    with open(path, 'r') as f:
        try:
            config = yaml.safe_load(f)
        except yaml.YAMLError as e:
            raise ValueError(f"Invalid YAML in config file: {e}")

    if config is None:
        raise ValueError("Config file is empty")

    # Validate sources if present
    if 'sources' in config:
        validate_sources(config['sources'])

    return config


def validate_sources(sources: list[dict]) -> None:
    """
    Validate the sources configuration.

    Args:
        sources: List of source dictionaries

    Raises:
        ValueError: If sources are invalid
    """
    if not isinstance(sources, list):
        raise ValueError("'sources' must be a list")

    for i, src in enumerate(sources):
        if not isinstance(src, dict):
            raise ValueError(f"Source {i} must be a dictionary")

        if 'address' not in src:
            raise ValueError(f"Source {i} missing required 'address' field")

        if 'channel' not in src:
            raise ValueError(f"Source {i} missing required 'channel' field")

        if 'tc_type' not in src:
            raise ValueError(f"Source {i} missing required 'tc_type' field")

        addr = src['address']
        if not isinstance(addr, int) or addr < 0 or addr > 7:
            raise ValueError(f"Source {i} 'address' must be an integer 0-7")

        ch = src['channel']
        if not isinstance(ch, int) or ch < 0 or ch > 3:
            raise ValueError(f"Source {i} 'channel' must be an integer 0-3")

        tc_type = src['tc_type']
        if tc_type not in ['K', 'J', 'T', 'E', 'R', 'S', 'B', 'N']:
            raise ValueError(f"Source {i} 'tc_type' must be one of: K, J, T, E, R, S, B, N")


def create_example_config(output_path: str) -> None:
    """
    Create an example configuration file.

    Args:
        output_path: Path where to write the example config
    """
    example = {
        'sources': [
            {
                'key': 'BATTERY_TEMP',
                'address': 0,
                'channel': 0,
                'tc_type': 'K',
            },
            {
                'key': 'MOTOR_TEMP',
                'address': 0,
                'channel': 1,
                'tc_type': 'K',
            },
            {
                'key': 'AMBIENT_TEMP',
                'address': 1,
                'channel': 0,
                'tc_type': 'K',
            },
        ]
    }

    with open(output_path, 'w') as f:
        yaml.dump(example, f, default_flow_style=False, sort_keys=False)

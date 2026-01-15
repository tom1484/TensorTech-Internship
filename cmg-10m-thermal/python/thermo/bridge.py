"""
Data fusion bridge for thermo CLI.

Handles spawning subprocesses (like cmg-cli) and injecting
thermal data into their output streams.
"""

import json
import subprocess
import sys
from typing import Any

from .hardware import ThermoBoard


class FuseBridge:
    """
    Bridge for fusing thermal data into 'cmg-cli get' command output.

    Runs 'cmg-cli get' command, captures its output, and injects
    thermal readings from configured MCC 134 sources.
    """

    def __init__(self, sources: list[dict], get_args: list[str]):
        """
        Initialize the fuse bridge.

        Args:
            sources: List of source configs with 'address', 'channel', and optional 'key'
            get_args: Arguments to pass to 'cmg-cli get' command
        """
        self.sources = sources
        self.get_args = get_args
        self._boards: dict[int, ThermoBoard] = {}

    def _get_board(self, address: int) -> ThermoBoard:
        """Get or create a ThermoBoard instance for the given address."""
        if address not in self._boards:
            self._boards[address] = ThermoBoard(address)
        return self._boards[address]

    def _get_thermal_data(self) -> dict[str, float]:
        """
        Read all configured thermal sources.

        Returns:
            Dictionary mapping keys to temperature values
        """
        data = {}
        for src in self.sources:
            addr = src['address']
            ch = src['channel']
            tc_type = src['tc_type']
            key = src.get('key', f'TEMP_{addr}_{ch}')

            try:
                board = self._get_board(addr)
                board.set_tc_type(ch, tc_type)
                val = board.get_reading(ch, 'temp')
                data[key] = val
            except Exception as e:
                # On read error, inject NaN or skip
                data[key] = float('nan')

        return data
    
    def _inject_json(self, json_obj: dict, thermal_data: dict) -> dict:
        """Inject thermal data into a JSON object."""
        json_obj.update(thermal_data)
        return json_obj

    def _inject_plain_text(self, lines: list[str], thermal_data: dict) -> list[str]:
        """Inject thermal data into a plain text object."""
        # Calculate formatting of received lines:
        # KEY:     XX.XXXXXX UNIT
        max_key_length = 0
        max_int_length = 0
        max_frac_length = 0

        data: list[tuple[str, float, str]] = []
        for line in lines:
            semicolon_index = line.find(':')
            point_index = line.find('.')

            unit_space_index = line.find(' ', point_index)
            if unit_space_index == -1:
                unit_space_index = len(line)
                unit = ''
            else:
                unit = line[unit_space_index + 1:].strip()

            key = line[:semicolon_index]
            value_str = line[semicolon_index + 1:unit_space_index].strip()
            int_length = value_str.find('.')
            frac_length = len(value_str) - int_length - 1

            max_key_length = max(max_key_length, len(key))
            max_int_length = max(max_int_length, int_length)
            max_frac_length = max(max_frac_length, frac_length)

            value = float(value_str)
            data.append((key, value, unit))
        
        for key, value in thermal_data.items():
            value_str = str(value)
            max_key_length = max(max_key_length, len(key))
            max_int_length = max(max_int_length, len(value_str.split('.')[0]))
            data.append((key, value, 'degC'))
        
        format_string = f"{{:<{max_key_length + 1}}}  {{:{max_int_length + max_frac_length + 1}.{max_frac_length}f}} {{}}"
        injected_lines = []
        for key, value, unit in data:
            new_line = format_string.format(key + ':', value, unit)
            injected_lines.append(new_line)
        return injected_lines

    def run(self) -> int:
        """
        Run the subprocess and fuse thermal data into its output.

        Returns:
            Exit code from the subprocess
        """
        process = subprocess.Popen(
            ["stdbuf", "-oL", "-eL", "cmg-cli", "get"] + self.get_args,
            stdout=subprocess.PIPE,
            stderr=sys.stderr,  # Pass stderr through directly
            text=True,
            bufsize=1,  # Line buffered
        )

        is_json = True
        received_data = []
        received_keys = set[str]()

        try:
            for line in process.stdout:
                line = line.strip()
                if not line:
                    continue

                if ":" not in line:
                    print(line, flush=True)
                    continue

                # Fetch latest thermal data
                thermal_data = self._get_thermal_data()

                # Attempt to parse line as JSON (cmg-cli --json mode)
                if is_json:
                    try:
                        json_obj = json.loads(line)
                        # Inject thermal data at root level
                        self._inject_json(json_obj, thermal_data)
                        print(json.dumps(json_obj), flush=True)
                    except json.JSONDecodeError:
                        is_json = False

                if not is_json:
                    # Plain text mode - print original line then thermal data
                    key = line.split(':')[0]
                    if key in received_keys:
                        injected_lines = self._inject_plain_text(received_data, thermal_data)
                        for line in injected_lines:
                            print(line, flush=True)
                        received_data = []
                        received_keys = set[str]()

                    received_data.append(line)
                    received_keys.add(key)
                
            if received_data:
                injected_lines = self._inject_plain_text(received_data, thermal_data)
                for line in injected_lines:
                    print(line, flush=True)

        except KeyboardInterrupt:
            process.terminate()
            process.wait()
            return 130  # Standard exit code for SIGINT

        process.wait()
        return process.returncode

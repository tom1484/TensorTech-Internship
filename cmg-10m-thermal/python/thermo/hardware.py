"""
Hardware abstraction for MCC 134 thermocouple DAQ boards.

This module abstracts the daqhats library, handling board discovery
and reading/writing values. This ensures CLI logic remains clean and
doesn't get cluttered with low-level I2C/SPI exception handling.
"""

from collections import namedtuple
from daqhats import hat_list, HatIDs, mcc134, TcTypes


MCC134CalInfo = namedtuple('MCC134CalInfo', ['slope', 'offset'])


class ThermoBoard:
    """Interface for a single MCC 134 thermocouple board."""

    def __init__(self, address: int = 0):
        """
        Initialize connection to an MCC 134 board.

        Args:
            address: Board address (0-7, set by address jumpers on the HAT)
        """
        self.address = address
        self._board = mcc134(address)

    @staticmethod
    def list_boards() -> list[dict]:
        """
        Returns a list of detected MCC 134 boards.

        Returns:
            List of dicts with 'address', 'id', and 'name' keys
        """
        boards = hat_list(filter_by_id=HatIDs.MCC_134)
        return [
            {
                "address": b.address,
                "id": "MCC 134",
                "name": b.product_name if hasattr(b, 'product_name') else "MCC 134"
            }
            for b in boards
        ]

    def get_serial(self) -> str:
        """
        Get the serial number of the board.
        """
        return self._board.serial()
    
    def get_calibration_date(self) -> str:
        """
        Get the calibration date of the board.

        Returns:
            The calibration date in the format YYYY-MM-DD
        """
        return self._board.calibration_date()

    def get_calibration_coefficients(self, channel: int) -> MCC134CalInfo:
        """
        Get calibration coefficients for a channel.

        Args:
            channel: Channel index (0-3)

        Returns:
            MCC134CalInfo with slope and offset
        """
        return self._board.calibration_coefficient_read(channel)
    
    def set_calibration_coefficients(self, channel: int, slope: float, offset: float) -> None:
        """
        Set calibration coefficients for a channel.

        Args:
            channel: Channel index (0-3)
            slope: Slope value
            offset: Offset value (mV)
        """
        self._board.calibration_coefficient_write(channel, slope, offset)
    

    def get_update_interval(self) -> int:
        """
        Get the update interval of the board.

        Returns:
            The update interval in seconds
        """
        return self._board.update_interval_read()

    def set_update_interval(self, interval: int) -> None:
        """
        Set the update interval of the board.

        Args:
            interval: Update interval in seconds
        """
        self._board.update_interval_write(interval)

    def get_reading(self, channel: int, reading_type: str) -> float:
        """
        Read a value from the specified channel.

        Args:
            channel: Channel index (0-3)
            reading_type: One of 'temp', 'adc', 'cjc'

        Returns:
            The reading value (temperature in °C or voltage in V)
        """
        if reading_type == 'temp':
            return self._board.t_in_read(channel)
        elif reading_type == 'adc':
            return self._board.a_in_read(channel)
        elif reading_type == 'cjc':
            return self._board.cjc_read(channel)
        else:
            raise ValueError(f"Unknown reading type: {reading_type}")

    def set_tc_type(self, channel: int, tc_type: str) -> None:
        """
        Set the thermocouple type for a channel.

        Args:
            channel: Channel index (0-3)
            tc_type: Thermocouple type string (K, J, T, E, R, S, B, N)
        """
        type_map = {
            'K': TcTypes.TYPE_K,
            'J': TcTypes.TYPE_J,
            'T': TcTypes.TYPE_T,
            'E': TcTypes.TYPE_E,
            'R': TcTypes.TYPE_R,
            'S': TcTypes.TYPE_S,
            'B': TcTypes.TYPE_B,
            'N': TcTypes.TYPE_N,
        }
        if tc_type.upper() not in type_map:
            raise ValueError(f"Unknown thermocouple type: {tc_type}")
        self._board.tc_type_write(channel, type_map[tc_type.upper()])

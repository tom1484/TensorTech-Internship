"""
Thermo CLI - MCC 134 Interface and Data Fuser

Entry point and Typer application definition.
"""

import json
import sys
from typing import Optional

import typer
from rich.console import Console
from rich.table import Table

from .bridge import FuseBridge
from .config import create_example_config, load_config
from .hardware import ThermoBoard

app = typer.Typer(
    name="thermo",
    help="Thermo: MCC 134 Interface and Data Fuser for TensorCMG-10m",
    add_completion=False,
    pretty_exceptions_show_locals=False,
)
console = Console()


@app.command("list")
def list_boards(
    json_output: bool = typer.Option(False, "--json", "-j", help="Output as JSON"),
):
    """List all connected MCC 134 boards."""
    boards = ThermoBoard.list_boards()

    if json_output:
        print(json.dumps({"boards": boards}))
        return

    if not boards:
        console.print("[yellow]No MCC 134 boards detected.[/]")
        return

    table = Table(title="Connected MCC 134 Boards")
    table.add_column("Address", style="cyan", justify="right")
    table.add_column("ID", style="green")
    table.add_column("Name", style="white")

    for b in boards:
        table.add_row(str(b['address']), b['id'], b['name'])

    console.print(table)


@app.command("get")
def get_reading(
    address: int = typer.Option(0, "--address", "-a", help="Board address (0-7)"),
    channel: int = typer.Option(0, "--channel", "-c", help="Channel index (0-3)"),
    serial: bool = typer.Option(False, "--serial", "-s", help="Get serial number"),
    calibration_date: bool = typer.Option(False, "--cali-date", "-D", help="Get calibration date"),
    calibration_coeffs: bool = typer.Option(False, "--cali-coeffs", "-C", help="Get calibration coefficients"),
    tc_type: str = typer.Option("K", "--tc-type", "-t", help="Thermocouple type (K, J, T, E, R, S, B, N)"),
    temp: bool = typer.Option(False, "--temp", help="Get temperature"),
    adc: bool = typer.Option(False, "--adc", help="Get raw ADC voltage"),
    cjc: bool = typer.Option(False, "--cjc", help="Get CJC temperature"),
    update_interval: bool = typer.Option(False, "--update-interval", "-i", help="Get update interval"),
    json_output: bool = typer.Option(False, "--json", "-j", help="Output as JSON"),
):
    """Read data from a specific channel. Default is temperature."""
    board = ThermoBoard(address)

    output_json = {"ADDRESS": address}

    if serial:
        serial = board.get_serial()
        if json_output:
            output_json["SERIAL"] = serial
        else:
            console.print(f"[bold green]Serial Number (Addr {address}):[/] {serial}")
    
    if calibration_date:
        date = board.get_calibration_date()
        if json_output:
            output_json.setdefault("CALIBRATION", {}).update({"DATE": date})
        else:
            console.print(f"[bold magenta]Calibration Date (Addr {address}):[/] {date}")

    if calibration_coeffs:
        coeffs = board.get_calibration_coefficients(channel)
        if json_output:
            output_json["CHANNEL"] = channel
            output_json.setdefault("CALIBRATION", {}).update({"SLOPE": coeffs.slope, "OFFSET": coeffs.offset})
        else:
            console.print(f"[bold magenta]Calibration Coefficients (Addr {address} Ch {channel}):[/]")
            console.print(f"  Slope:  {coeffs.slope}")
            console.print(f"  Offset: {coeffs.offset}")

    if cjc:
        val = board.get_reading(channel, 'cjc')
        if json_output:
            output_json["CHANNEL"] = channel
            output_json["CJC"] = val
        else:
            console.print(f"[bold blue]CJC (Addr {address}):[/] {val:.2f} °C")

    if update_interval:
        interval = board.get_update_interval()
        if json_output:
            output_json["UPDATE_INTERVAL"] = interval
        else:
            console.print(f"[bold yellow]Update Interval (Addr {address}):[/] {interval}")

    # Enable the channel by setting the thermocouple type
    board.set_tc_type(channel, tc_type)

    if temp:
        val = board.get_reading(channel, 'temp')
        if json_output:
            output_json["CHANNEL"] = channel
            output_json["TEMPERATURE"] = val
        else:
            console.print(f"[bold red]Temperature (Addr {address} Ch {channel}):[/] {val:.2f} °C")
    
    if adc:
        val = board.get_reading(channel, 'adc')
        if json_output:
            output_json["CHANNEL"] = channel
            output_json["ADC"] = val
        else:
            console.print(f"[bold green]ADC (Addr {address} Ch {channel}):[/] {val:.6f} V")
    
    if json_output:
        print(json.dumps(output_json))


@app.command("set")
def set_config(
    address: int = typer.Option(0, "--address", "-a", help="Board address (0-7)"),
    channel: int = typer.Option(0, "--channel", "-c", help="Channel index (0-3)"),
    cali_slope: Optional[float] = typer.Option(None, "--cali-slope", "-Cs", help="Set calibration slope"),
    cali_offset: Optional[float] = typer.Option(None, "--cali-offset", "-Co", help="Set calibration offset"),
    update_interval: Optional[int] = typer.Option(None, "--update-interval", "-i", help="Update interval in seconds"),
):
    """Configure channel parameters."""
    board = ThermoBoard(address)
    if cali_slope is not None or cali_offset is not None:
        if not (cali_slope is not None and cali_offset is not None):
            console.print("[red]Error: Both --cali-slope and --cali-offset must be provided.[/]")
            raise typer.Exit(code=1)
        board.set_calibration_coefficients(channel, cali_slope, cali_offset)
        console.print(f"[bold green]Calibration Coefficients (Addr {address} Ch {channel}) set to:[/]")
        console.print(f"  Slope:  {cali_slope:.6f}")
        console.print(f"  Offset: {cali_offset:.6f}")
    if update_interval is not None:
        board.set_update_interval(update_interval)
        console.print(f"[bold yellow]Update Interval (Addr {address}) set to:[/] {update_interval} seconds")


@app.command(
    "fuse",
    context_settings={"allow_extra_args": True, "ignore_unknown_options": True},
)
def fuse(
    ctx: typer.Context,
    config: Optional[str] = typer.Option(None, "--config", "-C", help="Path to YAML config file"),
    address: Optional[int] = typer.Option(None, "--address", "-a", help="Single mode: Board address"),
    channel: Optional[int] = typer.Option(None, "--channel", "-c", help="Single mode: Channel index"),
    key: str = typer.Option("TEMP_FUSED", "--key", "-k", help="Single mode: JSON key to inject"),
):
    """
    Fuse thermal data into 'cmg-cli get' command.
    Use '--' to separate thermo arguments from the arguments passed to 'cmg-cli get'.

    Examples:
        thermo fuse --address 0 --channel 1 --key MY_TEMP -- --power --json
        thermo fuse --config my_setup.yaml -- --actuator --stream 5 --json
    """
    # Capture the downstream arguments (everything after --)
    cmd_args = ctx.args
    if not cmd_args:
        console.print("[red]Error: No arguments provided to wrap.[/]")
        console.print("[dim]Usage: thermo fuse [OPTIONS] -- [arguments...][/]")
        raise typer.Exit(code=1)

    # Determine sources (config file vs single CLI flags)
    sources = []
    if config:
        try:
            cfg_data = load_config(config)
            sources = cfg_data.get('sources', [])
        except (FileNotFoundError, ValueError) as e:
            console.print(f"[red]Config error: {e}[/]")
            raise typer.Exit(code=1)
    elif address is not None and channel is not None:
        sources = [{"address": address, "channel": channel, "key": key}]

    if not sources:
        console.print("[red]Error: Must specify --config or (--address and --channel)[/]")
        raise typer.Exit(code=1)

    # Start the bridge
    bridge = FuseBridge(sources, cmd_args)

    exit_code = bridge.run()
    raise typer.Exit(code=exit_code)


@app.command("init-config")
def init_config(
    output: str = typer.Option("thermo_config.yaml", "--output", "-o", help="Output file path"),
):
    """Generate an example configuration file."""
    create_example_config(output)
    console.print(f"[green]Created example config: {output}[/]")


def main():
    """Entry point for the thermo CLI."""
    app()


if __name__ == "__main__":
    main()

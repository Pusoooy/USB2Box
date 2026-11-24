"""Command-line entry point for USB2Box and its simulator."""
from argparse import ArgumentParser
import json

from . import Deadline, DeviceError, I2CMessage, USB2XXX, list_devices


def connection_args(parser: ArgumentParser) -> None:
    parser.add_argument("--mock", action="store_true", help="use the shared simulated device")
    parser.add_argument("--vid", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--pid", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--serial")
    parser.add_argument("--executable", help="path to the compiled C mock device")


def settings(args) -> dict:
    return {key: getattr(args, key) for key in ("mock", "vid", "pid", "serial", "executable")}


def main() -> None:
    parser = ArgumentParser(prog="usb2xxx")
    connection_args(parser)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("list", "status", "stop", "log", "vref"):
        commands.add_parser(name)
    uart = commands.add_parser("uart")
    uart.add_argument("hex")
    uart.add_argument("--port", type=int, default=0)
    uart.add_argument("--baud", type=int, default=115200)
    i2c = commands.add_parser("i2c")
    i2c.add_argument("address", type=lambda value: int(value, 0))
    i2c.add_argument("--bus", type=int, default=0)
    i2c.add_argument("--write", default="")
    i2c.add_argument("--read", type=int, default=0)
    recovery = commands.add_parser("i2c-recover")
    recovery.add_argument("--bus", choices=(0, 1), type=int, default=0)
    spi = commands.add_parser("spi")
    spi.add_argument("segments", nargs="+")
    spi.add_argument("--mode", type=int, default=0)
    spi.add_argument("--bits", type=int, default=8)
    adc = commands.add_parser("adc")
    adc.add_argument("channel", type=int)
    pwm = commands.add_parser("pwm")
    pwm.add_argument("frequency", type=int)
    pwm.add_argument("duty", type=int)
    pwm.add_argument("--group", type=int, default=0)
    pwm.add_argument("--duration", type=int, default=100)
    args = parser.parse_args()
    try:
        if args.command == "list":
            result = list_devices(mock=args.mock, vid=args.vid, pid=args.pid)
        else:
            with USB2XXX.open(**settings(args)) as dev:
                result = _run(args, dev)
        print(json.dumps({"simulated": args.mock, "result": result}))
    except (DeviceError, ValueError, OSError) as error:
        parser.exit(2, f"{error}\n")


def _run(args, dev: USB2XXX):
    if args.command == "status":
        return {"capabilities": dev.capabilities(), "resources": dev.resources()}
    if args.command == "log":
        return dev.diagnostic_log()
    if args.command == "vref":
        return {"reference_volts": dev.adc_estimate_reference()}
    if args.command == "i2c-recover":
        dev.i2c_configure(args.bus)
        return dev.i2c_recover(args.bus)
    if args.command == "stop":
        dev.stop_all()
        return {"safe_stop": True}
    if args.command == "adc":
        return {"sample": dev.adc_read_once(args.channel)}
    if args.command == "spi":
        return [item.hex() for item in dev.spi_transfer([bytes.fromhex(x) for x in args.segments],
                                                       mode=args.mode, bits=args.bits)]
    if args.command == "uart":
        dev.uart_configure(args.port, args.baud)
        from time import monotonic, sleep
        queued = dev.uart_write(args.port, bytes.fromhex(args.hex))
        end = monotonic() + 1.5
        while dev.uart_stats(args.port)[2]:
            if monotonic() >= end:
                raise Deadline("UART transmit did not drain")
            sleep(0.005)
        return {"sent": queued}
    if args.command == "i2c":
        messages = []
        if args.write:
            messages.append(I2CMessage(data=bytes.fromhex(args.write)))
        if args.read:
            messages.append(I2CMessage(read=args.read))
        dev.i2c_configure(args.bus)
        if dev.simulated:
            slave = 1 - args.bus
            dev.i2c_configure(slave)
            dev.i2c_role(slave, True, args.address)
            dev.i2c_slave_buffer(slave, bytes(range(64)))
        return dev.i2c_transfer(args.bus, args.address, messages).hex()
    if args.command == "pwm":
        from time import sleep
        dev.pwm_configure(args.group, args.frequency, args.duty)
        result = dev.pwm_start(args.group, args.duration)
        sleep(args.duration / 1000)
        dev.pwm_stop(args.group)
        result["running"] = False
        return result
    raise ValueError("unknown command")


if __name__ == "__main__":
    main()

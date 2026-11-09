#!/usr/bin/env python3
"""home-switch's devices over USB (the round screen plugged in, home-switch open on it): the backup to the config page
(hold the side key on the round screen and scan the code with your phone).

  python3 tools/devices.py add            asks for the name, IP, model and token (the token is typed without echo)
  python3 tools/devices.py list           the devices on the round screen (never their tokens)
  python3 tools/devices.py remove 台灯
  python3 tools/devices.py rename 台灯 卧室的台灯
  python3 tools/devices.py on|off|toggle [台灯]

Needs pyserial (pip install pyserial). With more than one round screen plugged in: --port /dev/cu.usbmodem….

The token goes to the device only on `yk cmd homeswitch.add -`'s argument line, which the device reads itself (never
echoed, never in its console history); it is not on this command line (shell history) and nothing here prints or
stores it.
"""
import getpass
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'devkit', 'firmware', 'tools'))
try:
    import yay_dev  # noqa: E402  (the development kit's USB console)
except ImportError as e:
    sys.exit(f'{e}: pip install pyserial (and run `git submodule update --init devkit` once)')


def reply_of(con, seconds=8):
    for _, line in yay_dev.lines(con, seconds, ('YK_OK', 'YK_ERR')):
        return line
    return None


def command(con, name, args=None, secret=False):
    con.s.reset_input_buffer()
    if secret:
        yay_dev.send(con, f'yk cmd {name} -')
        if not yay_dev.wait_for(con, 'YK_ARGS', 5):
            sys.exit('the round screen did not ask for the device: is home-switch open on it?')
        con.s.write((json.dumps(args, ensure_ascii=False, separators=(',', ':')) + '\n').encode())
    else:
        text = json.dumps(args or {}, ensure_ascii=False, separators=(',', ':'))
        yay_dev.send(con, f'yk cmd {name} {text}')
    line = reply_of(con)
    if not line:
        sys.exit('no answer: is home-switch open on the round screen?')
    if line.startswith('YK_ERR'):
        sys.exit(line)
    return json.loads(line.split(' ', 1)[1]) if ' ' in line else {}


def show(reply):
    for i, d in enumerate(reply.get('devices', [])):
        mark = '*' if i == reply.get('current') else ' '
        print(f"{mark} {d['name']}  {d['ip']}  {d['model'] or '-'}  siid {d['siid']} piid {d['piid']}  {d['power']}")
    if not reply.get('devices'):
        print('no devices yet: python3 tools/devices.py add')


def ask(prompt, default=''):
    value = input(f'{prompt}{f" [{default}]" if default else ""}: ').strip()
    return value or default


def main():
    args = sys.argv[1:]
    port = yay_dev.default_port()
    if args[:1] == ['--port']:
        port, args = args[1], args[2:]
    if not args or args[0] not in ('add', 'list', 'remove', 'rename', 'on', 'off', 'toggle'):
        sys.exit(__doc__)
    con = yay_dev.Console(port)
    cmd, rest = args[0], args[1:]
    if cmd == 'list':
        show(command(con, 'homeswitch.list'))
    elif cmd == 'remove':
        if not rest:
            sys.exit('which one? python3 tools/devices.py remove <name>')
        show(command(con, 'homeswitch.remove', {'name': rest[0]}))
    elif cmd == 'rename':
        if len(rest) < 2:
            sys.exit('python3 tools/devices.py rename <name> <new name>')
        show(command(con, 'homeswitch.rename', {'name': rest[0], 'to': rest[1]}))
    elif cmd in ('on', 'off', 'toggle'):
        a = {'name': rest[0]} if rest else {}
        if cmd != 'toggle':
            a['on'] = cmd == 'on'
        print(command(con, 'homeswitch.set', a))
    else:
        print('From the token extractor: the device\'s IP, MODEL and TOKEN. Name it the way you will say it ("台灯").')
        device = {
            'name': ask('Name'),
            'ip': ask('IP'),
            'model': ask('Model (cuco.plug.v3)'),
        }
        print('siid/piid: the power property. 2 / 1 for most plugs, lamps and fans; home.miot-spec.com has each model.')
        device['siid'] = int(ask('siid', '2'))
        device['piid'] = int(ask('piid', '1'))
        device['token'] = getpass.getpass('Token (32 hex, not shown): ').strip()
        reply = command(con, 'homeswitch.add', device, secret=True)
        del device
        print('replaced' if reply.get('replaced') else 'added')
        show(reply)


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
# Asks mutter (a headless GNOME compositor) through its own ScreenCast interface for a stream of its first monitor and
# prints "NODE <PipeWire node id>". Used by RunMutterCaptureCheck.sh. Needs python3-dbus and python3-gi.
# Usage: mutter_cast.py [cursor mode: 0 hidden, 1 painted in, 2 separate] [seconds to keep the stream open]
import sys, dbus, dbus.mainloop.glib
from gi.repository import GLib

mode = int(sys.argv[1]) if len(sys.argv) > 1 else 2  # cursor mode: 0 hidden, 1 embedded, 2 metadata
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
sc = bus.get_object('org.gnome.Mutter.ScreenCast', '/org/gnome/Mutter/ScreenCast')
sess_path = sc.CreateSession({}, dbus_interface='org.gnome.Mutter.ScreenCast')
session = bus.get_object('org.gnome.Mutter.ScreenCast', sess_path)
props = dbus.Interface(bus.get_object('org.gnome.Mutter.DisplayConfig', '/org/gnome/Mutter/DisplayConfig'),
                       'org.gnome.Mutter.DisplayConfig')
serial, monitors, logical, _ = props.GetCurrentState()
connector = str(monitors[0][0][0])
print('monitor', connector, file=sys.stderr, flush=True)
stream_path = session.RecordMonitor(connector, {'cursor-mode': dbus.UInt32(mode)}, dbus_interface='org.gnome.Mutter.ScreenCast.Session')
stream = bus.get_object('org.gnome.Mutter.ScreenCast', stream_path)
loop = GLib.MainLoop()

def added(node):
    print('NODE', int(node), flush=True)

stream.connect_to_signal('PipeWireStreamAdded', added, dbus_interface='org.gnome.Mutter.ScreenCast.Stream')
session.Start(dbus_interface='org.gnome.Mutter.ScreenCast.Session')
GLib.timeout_add_seconds(int(sys.argv[2]) if len(sys.argv) > 2 else 40, loop.quit)
loop.run()
session.Stop(dbus_interface='org.gnome.Mutter.ScreenCast.Session')

#define ROOT_INTROSPECTION							     \
"<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n" \
"\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"		     \
"<node>\n"									     \
"  <node name=\"Player\"/>\n"							     \
"  <node name=\"TrackList\"/>\n"						     \
"  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"			     \
"    <method name=\"Introspect\">\n"						     \
"      <arg name=\"data\" direction=\"out\" type=\"s\"/>\n"			     \
"    </method>\n"								     \
"  </interface>\n"								     \
"  <interface name=\"org.freedesktop.MediaPlayer\">\n"				     \
"    <method name=\"Identity\">\n"						     \
"      <arg type=\"s\" direction=\"out\" />\n"					     \
"    </method>\n"								     \
"    <method name=\"MprisVersion\">\n"						     \
"      <arg type=\"(qq)\" direction=\"out\" />\n"				     \
"    </method>\n"								     \
"    <method name=\"Quit\">\n"							     \
"    </method>\n"								     \
"  </interface>\n"								     \
"</node>\n"


#define PLAYER_INTROSPECTION							     \
"<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n" \
"\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"		     \
"<node>"									     \
"  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"			     \
"    <method name=\"Introspect\">\n"						     \
"      <arg name=\"data\" direction=\"out\" type=\"s\"/>\n"			     \
"    </method>\n"								     \
"  </interface>\n"								     \
"  <interface name=\"org.freedesktop.MediaPlayer\">\n"				     \
"    <method name=\"GetStatus\">\n"						     \
"      <arg type=\"(iiii)\" direction=\"out\" />\n"				     \
"    </method>\n"								     \
"    <method name=\"Prev\">\n"							     \
"    </method>\n"								     \
"    <method name=\"Next\">\n"							     \
"    </method>\n"								     \
"    <method name=\"Stop\">\n"							     \
"    </method>\n"								     \
"    <method name=\"Play\">\n"							     \
"    </method>\n"								     \
"    <method name=\"Pause\">\n"							     \
"    </method>\n"								     \
"    <method name=\"Repeat\">\n"						     \
"      <arg type=\"b\" direction=\"in\" />\n"					     \
"    </method>\n"								     \
"    <method name=\"VolumeSet\">\n"						     \
"      <arg type=\"i\" direction=\"in\" />\n"					     \
"    </method>\n"								     \
"    <method name=\"VolumeGet\">\n"						     \
"      <arg type=\"i\" direction=\"out\" />\n"					     \
"    </method>\n"								     \
"    <method name=\"PositionSet\">\n"						     \
"      <arg type=\"i\" direction=\"in\" />\n"					     \
"    </method>\n"								     \
"    <method name=\"PositionGet\">\n"						     \
"      <arg type=\"i\" direction=\"out\" />\n"					     \
"    </method>\n"								     \
"    <method name=\"GetMetadata\">\n"						     \
"      <arg type=\"a{sv}\" direction=\"out\" />\n"				     \
"    </method>\n"								     \
"    <method name=\"GetCaps\">\n"						     \
"      <arg type=\"i\" direction=\"out\" />\n"					     \
"    </method>\n"								     \
"    <signal name=\"TrackChange\">\n"						     \
"      <arg type=\"a{sv}\"/>\n"							     \
"    </signal>\n"								     \
"    <signal name=\"StatusChange\">\n"						     \
"      <arg type=\"(iiii)\"/>\n"						     \
"    </signal>\n"								     \
"    <signal name=\"CapsChange\">\n"						     \
"      <arg type=\"i\"/>\n"							     \
"    </signal>\n"								     \
"  </interface>\n"								     \
"</node>\n"

#define TRACKLIST_INTROSPECTION							     \
"<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n" \
"\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"		     \
"<node>"									     \
"  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"			     \
"    <method name=\"Introspect\">\n"						     \
"      <arg name=\"data\" direction=\"out\" type=\"s\"/>\n"			     \
"    </method>\n"								     \
"  </interface>\n"								     \
"  <interface name=\"org.freedesktop.MediaPlayer\">\n"				     \
"    <method name=\"AddTrack\">\n"						     \
"      <arg type=\"s\" direction=\"in\" />\n"					     \
"      <arg type=\"b\" direction=\"in\" />\n"					     \
"      <arg type=\"i\" direction=\"out\" />\n"					     \
"    </method>\n"								     \
"    <method name=\"DelTrack\">\n"						     \
"      <arg type=\"i\" direction=\"in\" />\n"					     \
"    </method>\n"								     \
"    <method name=\"GetMetadata\">\n"						     \
"      <arg type=\"i\" direction=\"in\" />\n"					     \
"      <arg type=\"a{sv}\" direction=\"out\" />\n"				     \
"    </method>\n"								     \
"    <method name=\"GetCurrentTrack\">\n"					     \
"      <arg type=\"i\" direction=\"out\" />\n"					     \
"    </method>\n"								     \
"    <method name=\"GetLength\">\n"						     \
"      <arg type=\"i\" direction=\"out\" />\n"					     \
"    </method>\n"								     \
"    <method name=\"SetLoop\">\n"						     \
"      <arg type=\"b\" direction=\"in\" />\n"					     \
"    </method>\n"								     \
"    <method name=\"SetRandom\">\n"						     \
"      <arg type=\"b\" direction=\"in\" />\n"					     \
"    </method>\n"								     \
"    <signal name=\"TrackListChange\">\n"					     \
"      <arg type=\"i\" />\n"							     \
"    </signal>\n"								     \
"  </interface>\n"								     \
"</node>\n"

# Windows setup for the Phoenix4K config app

**No setup needed.** This file is kept only as a record of what changed and why.

## Current state (2026-10-04, firmware v2 HID-interface revision)

The config app's vendor channel (what used to be a raw USB vendor-class interface) is now a
second HID collection, using a vendor-defined usage page (`0xFF00`) — the same technique
SayoDevice's own original firmware used for its config channel. Every OS, including Windows,
has a built-in generic HID class driver that binds automatically to any HID interface, with zero
vendor driver and zero user setup — exactly like how the keyboard half of this same device has
never needed a driver. Confirmed on Linux: both interfaces enumerate as `bInterfaceClass 3 (HID)`
and get separate hidraw nodes with no manual driver step, and the config app talks to it over
WebHID (`navigator.hid`) rather than WebUSB (`navigator.usb`).

This was not tested on actual Windows (neither this session nor the user had access), but it
relies on nothing Windows-specific — it's the same universal HID class driver every keyboard,
mouse, and game controller already uses. If it doesn't work as expected on real Windows, that
would be a genuine surprise worth reporting back.

## Superseded: the original Zadig/WinUSB plan

An earlier revision of this firmware used a raw vendor-class (`bInterfaceClass 0xFF`) interface
for the config channel, which has no built-in driver on Windows and would have needed a manual
WinUSB driver assignment via Zadig for every user, every time. That plan is gone now that the
interface is HID-class instead — keeping this note only so the reasoning isn't lost.

A separate idea, firmware-side Microsoft OS 2.0 descriptors (to make the *old* vendor-class
interface auto-bind WinUSB with zero Zadig step), was considered and deliberately not pursued:
it would have meant modifying the firmware's core USB enumeration path on hardware nobody could
test, which was too large a risk for a benefit the HID-interface approach now delivers for free,
at much lower risk, using an ordinary and well-proven USB class.

# Customer replies

Reusable answers to the questions that actually come back from customers, kept
in sync with the firmware's real behaviour. Written for people who have never
seen the source code — no "score", no "engine", no thresholds. If you change
what the device does, update the matching answer here in the same change.

Everything below describes the **built** firmware. Anything marked *(next
firmware update)* is committed but not yet on shipped units, and those units
need a USB reflash — there is no over-the-air update on this hardware.

---

## "It only vibrates — I never hear anything"

True on some models, and partly our fault.

- **The Atom Lite has no speaker or buzzer at all.** It can only light its LED,
  so waiting for a beep from one will never work. That is hardware, not a fault.
- **On the Core2 For AWS and M5Stack Basic the sound was switched off in the
  build**, even though both have a 1 W speaker, so no sound played however close
  a device was. *(next firmware update: fixed)*

Which models can make a sound:

| Model | Sound | Screen | Light | Vibration |
|---|---|---|---|---|
| Atom Lite | — none | — | LED | — |
| Atom Echo | buzzer | — | — | — |
| Atom Voice | speaker | — | LED | — |
| **Core2 For AWS** | speaker | yes | on-screen | yes |
| M5Stack Basic | speaker | yes | on-screen | — |
| StickC Plus SE | buzzer | yes | on-screen | — |
| T-Dongle C5 | — none | yes | RGB LED | — |

---

## "It sometimes vibrates once and sometimes twice"

Both are correct, and the difference is the point.

- **One buzz** — something worth noticing is nearby (the caution level).
- **Two buzzes** — a high-confidence surveillance or tracking device is nearby
  (the alert level).

Same idea for sound *(next firmware update)*: **two rising beeps** for an alert,
**one lower beep** for a caution. The buzzes and the beeps are deliberately
matched, so severity can be judged by feel even when the device is in a pocket.

A device that stays nearby does not buzz continuously — it buzzes when the
level is first reached, then stays quiet until it either escalates or the level
falls and rises again. Repeated buzzing means repeated or worsening signals,
not a malfunction.

---

## "It counted 530 alerts but nothing happened"

The counter they are looking at is *(on older firmware)* labelled **Total events**,
and it does not mean 530 alerts.

It counted *every* time the device logged a detection while a signal was still
being seen — including repeat sightings of the same weak, ordinary signal. In a
busy area, roughly one a minute is normal, which is how 8 hours reaches the
hundreds.

An **alert** is different: a signal only adds to the reading once every couple
of minutes, the reading fades by itself when nothing is around, and it has to
pass a threshold before the device buzzes, sounds or turns red. A long list of
events with no alert means the device was **working correctly** — it saw a lot
of background radio traffic and correctly ignored it.

*(next firmware update)* That confusing label is gone. The screen now shows two
counts instead, coloured to match what they mean:

- **ALERTS** in red — how many times the device has reached the alert level.
- **CAUTIONS** in yellow — how many times it has reached the caution level.

These count *occasions*, not time: a device sitting there for an hour is counted
once, not hundreds of times. They also **fade by themselves** — about one point
every two minutes — so the numbers always describe how much has been happening
*recently*, and go back to zero after a quiet spell. Nothing accumulates
forever any more.

---

## "How do I find out which device it detected?"

*(next firmware update)* The screen shows the address of the device that
triggered the alert, on the line directly under the detection type, in the same
colour as the alert level.

- It is shown **solid, never blinking**, so it can be written down or
  photographed.
- `MAC --` means that particular detection carries no address — for instance a
  network-name match, where the network name *is* the identifier.
- The address is the thing to quote when reporting a device to someone, or to
  look up yourself.

---

## "Can I see its IP address?"

No — and not because it is hidden, but because Eye Spy never has one.

The device is entirely passive: it listens to what is broadcast around it and
never connects to any network or to the camera. An IP address only exists once a
device has joined a network, so collecting one would require connecting to it —
a different, active, and far less discreet kind of tool. This is a deliberate
design choice, not a limitation to be fixed.

---

## "Can I adjust the sensitivity from the app?"

Not yet. The sensitivity levels live in the firmware, so changing them means
reflashing the device rather than moving a slider. Making them adjustable from
the dashboard is on the list.

---

## "Red is stuck on"

*(next firmware update)* Red now holds the screen for **15 seconds** after an
alert so the detection type and the address can be read before they disappear.
A countdown shows the remaining time, so a held screen is clearly still working
rather than frozen. The screen then returns to normal on its own.

If red appears stuck for **longer than 15 seconds**, or the screen does not
return to normal, that is worth reporting with the details from the serial log.

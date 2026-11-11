# Input

Header: `RPS_UI_Framework/Input.h`. Namespace: `rpsui::api::input`. Version 2.0.

Value-only controller and keyboard input on the game thread. It needs no skeleton provider. Physical hands are left = 0 and right = 1.

## Subscribing

`subscribe` registers a `Callback` and returns a token. The callback receives a borrowed `Frame` holding each hand's position, forward direction, pressed buttons, stick and `valid` flag. Callbacks must not block or keep the frame.

`unsubscribe` returning false means a callback is in flight: keep the callback and context alive and retry.

## Capturing buttons

`capture` takes a `Capture` record and works only on the game callback thread. Renew it every callback; it expires after three callbacks. Supported buttons are B/Menu (1), Grip (2), A (7), thumbstick click (32) and Trigger (33).

An idle chord reserves its members only while the complete chord is down. Once captured, publish `buttons` until all chord members have been released.

`stickAxes` withholds single thumbstick axes from the game, per hand, as `StickAxis` bits (`Horizontal` 1, `Vertical` 2). Withholding only `Vertical` lets a panel scroll with the stick while the player still turns with it. `Frame` keeps reporting the whole stick, and `capturedButtons` does not include axes. Capturing the thumbstick button (32) still withholds the whole stick. A `Capture` of the earlier 40-byte size (`kCaptureSizeWithoutAxes`) is still accepted and withholds no axis this way.

## Cooperation with other input hooks

`rawInputReadActive` is true only on the calling thread while the UI samples physical OpenVR state. A hook that sees it should leave that read raw.

`capturedButtons` is a read-only, lock-free query for any input thread. Supply the current physical button levels so pending chords resolve. It returns zero outside gameplay and after capture has been released or has expired.

## Mods Config FO4 VR

While the menu of Mods Config FO4 VR is open, input yields to it: every frame has `ready` false, as while a game menu is open, so subscribers stop, capture is released and the keyboard capture lapses. A button that was captured when the menu opened stays hidden from the game and from that menu until it is released, so a press already consumed by a panel never reaches either. Input returns when that menu closes, or when it is minimized while a mod shows its own UI.

## Physical keyboard

`captureKeyboard` lets a subscriber take the physical keyboard for text entry, on the game callback thread. Call it every callback while a text field is active. Each call renews the capture and fills a `KeyboardRead` with what was typed since the last call, oldest first.

- A `Character` event's `code` is one printable UTF-16 code unit, already through the keyboard layout. Control characters never appear here.
- A `Key` event's `code` is the Windows virtual-key code of a key press, including auto-repeat. Use these for Backspace, Enter, Escape and the arrow keys.
- `focused` is whether the game window has foreground focus. Windows sends it no keyboard input otherwise.

While a subscriber holds the capture the game sees no key presses, so typing cannot move the player or open a menu. Key releases still reach the game, so a key held when capture starts does not stick. Mouse input is untouched.

There is one owner at a time, and a new owner starts with an empty queue. A false return means the keyboard is not held: another subscriber owns it, input is not ready or the service could not start. The capture lapses three callbacks after the last call, whenever input stops being ready, on a game load and on `unsubscribe`, so an owner that stops asking releases it without doing anything else.

## Pointer aim

`setPointerAim` works on the game thread and sets the shared UI aim from a `PointerAim` record. The settings host owns persistence. Angles are degrees relative to native controller forward: positive pitch aims up and positive yaw aims right. Pitch ranges over [-180, 180] and yaw over [-90, 90]. Changes apply to every UI panel on the next input frame.

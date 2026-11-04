# Input

Header: `RPS_UI_Framework/Input.h`. Namespace: `rpsui::api::input`. Version 2.0.

Value-only controller input on the game thread. It needs no skeleton provider. Physical hands are left = 0 and right = 1.

## Subscribing

`subscribe` registers a `Callback` and returns a token. The callback receives a borrowed `Frame` holding each hand's position, forward direction, pressed buttons, stick and `valid` flag. Callbacks must not block or keep the frame.

`unsubscribe` returning false means a callback is in flight: keep the callback and context alive and retry.

## Capturing buttons

`capture` takes a `Capture` record and works only on the game callback thread. Renew it every callback; it expires after three callbacks. Supported buttons are B/Menu (1), Grip (2), A (7), thumbstick click (32) and Trigger (33).

An idle chord reserves its members only while the complete chord is down. Once captured, publish `buttons` until all chord members have been released.

## Cooperation with other input hooks

`rawInputReadActive` is true only on the calling thread while the UI samples physical OpenVR state. A hook that sees it should leave that read raw.

`capturedButtons` is a read-only, lock-free query for any input thread. Supply the current physical button levels so pending chords resolve. It returns zero outside gameplay and after capture has been released or has expired.

## Pointer aim

`setPointerAim` works on the game thread and sets the shared UI aim from a `PointerAim` record. The settings host owns persistence. Angles are degrees relative to native controller forward: positive pitch aims up and positive yaw aims right. Pitch ranges over [-180, 180] and yaw over [-90, 90]. Changes apply to every UI panel on the next input frame.

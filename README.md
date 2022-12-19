# Coyote-ESP32
ESP32 Controller for a DG-Lab Coyote e-stim Powerbox.


## IMPORTANT

Copy the file 'secrets.CHANGEME.h' to 'secrets.h' and update according to your device and home network.
DO NOT add this file to the GIT repo as it may expose your credentials.


## To Install

Open 'Coyote-ESP32.ino' with the Arduino IDE, compile and upload to an ESP-32 board.

After reset, the controller will go though the following sequence, indicated by the on-board LED:

1. Solid LED: Scanning for then connecting to WiFI network if one is found.
2. Flashing LED: Controller is scanning for Coyote device.
3. Solid LED: Controller has found the Coyote device and is connecting.
4. LED off: Controller is connected to the Coyote device and generating waveforms.

By default, generates a high-frequency wave on port A, and the 'GrainTouch' wave on port B at power level of 25.


# WARNING: USE AT YOUR OWN RISK

The code is provided as-is, with no warranties of any kind. Not suitable for any purpose.
Provided as an example and exercise in BLE development only.

Some guardrails have been implemented to limit the maximum power that the Coyote can output, but these can easily be by-passed.
The Coyote e-stim power box can generate dangerous power levels under normal usage.
See https://www.reddit.com/r/estim/comments/uadthp/dg_lab_coyote_review_by_an_electronics_engineer for details.



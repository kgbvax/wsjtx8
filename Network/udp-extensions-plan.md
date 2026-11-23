# UDP extension messages for 75auto

This note is the plan for four UDP extension messages. 75auto uses them to start a CQ and to set the Tx offset. The contract is the section "Extension messages for a patched WSJT-X" in `75auto/docs/interfaces/digimode.md`. `Network/NetworkMessage.hpp` holds the wire format reference.

## Message numbers

This fork uses the message types 0 to 16. Type 16 is AnnotationInfo. Upstream WSJT-X uses the same 17 types. No fork and no upstream code uses 50 to 53. The proposed numbers fit, and this work uses them as final:

| Type | Name | Direction | Fields after the Id |
|---|---|---|---|
| 50 | Extensions | Out | Version (quint32), Names (utf8) |
| 51 | Enable Tx | In | Enable (bool) |
| 52 | Start CQ | In | Modifier (utf8) |
| 53 | Set Tx Offset | In | Offset (quint32, Hz) |

The fields fit the schema 3 framing. Every field type is already in use in the protocol. The extension contract version is 1. The names are `tx-enable,cq,set-tx-df`.

## Where the code goes

1. `Network/NetworkMessage.hpp`: add the four types to the enum, after `maximum_message_type_`, with the fixed values 50 to 53. Document them in the header comment.
2. `Network/NetworkMessage.cpp`: the Reader accepts the stock types and the extension types. Every other type stays unrecognized, as before.
3. `Network/MessageClient`: parse the three incoming messages and emit one signal each. Send the announcement after each Heartbeat.
4. `widgets/mainwindow.cpp`: connect the three signals to the same actions as the operator buttons.
5. `tests/test_message_client_extensions.cpp`: a Qt test that talks to a MessageClient over the loopback interface.

## Accept UDP requests

The MessageClient drops each incoming message while "Accept UDP requests" is off. The three incoming messages use the same path, so they need no extra test. The announcement also goes out only while the setting is on. With the setting off, WSJT-X ignores the messages, so it does not announce them. With the setting off, the patched WSJT-X sends the same datagrams as the stock WSJT-X.

## Behavior of each message

Enable Tx sets the "Enable Tx" button to the requested state. If the button already has that state, nothing changes.

Start CQ checks the modifier with the rule of the Tx6 field: 1 to 4 letters, or 1 to 3 digits. If the modifier is not valid, WSJT-X ignores the message. Otherwise it sets the CQ word, generates the standard messages, and selects Tx6 as a click on the Tx6 button does. Then it turns "Enable Tx" on. A transmission in progress is not restarted with the CQ. The CQ goes out in the next transmit period of the parity that the operator set.

Set Tx Offset ignores an offset outside 200 to 3000 Hz. Otherwise it sets the Tx frequency spin box, as the operator does. It does not change "Hold Tx Freq".

None of the three messages resets the Tx watchdog.

## Findings for the three expected behaviors

The contract names three behaviors as expected, not confirmed. The source gives these results.

### Effect check of Set Tx Offset

Confirmed, with one exception. `on_TxFreqSpinBox_valueChanged` in `widgets/mainwindow.cpp` calls `statusUpdate ()` at its end. A Status with the new Tx DF therefore goes out at once, well within 2 s.

The exception: if the new offset is equal to the current offset, the spin box sends no change signal. Then no new Status goes out. The MessageClient also drops a Status that is equal to the last datagram. 75auto must compare the requested offset with the Tx DF of the last Status before it waits. If the two are equal, the result is `took-effect` without a new Status.

Two more limits apply. In SuperFox Fox mode, WSJT-X forces the offset to 750 Hz. On 60 m, a UK call sign moves the offset below 5358 kHz. In both cases the Status shows the forced value, not the requested value.

For FT8 and FT4, a transmission in progress keeps its offset. WSJT-X computes the waveform at the start of the transmission. FT8 with the x2 or x4 tone spacing option, and the older modes, change the frequency during the transmission.

### Offset out of range

Corrected. The stock Tx frequency spin box has the range 200 to 5000 Hz, and it clamps a value outside this range. The patch does not use the clamp. It ignores an offset below 200 Hz or above 3000 Hz before it touches the spin box, so the Status keeps the old offset. The contract text is then correct for the patched WSJT-X.

### How "CQ: Max dB" picks a caller

Partly corrected. The code is in `readFromStdout` in `widgets/mainwindow.cpp`, at the comment "CQ: Max dB". It works per decode line, at the time the line arrives, not once per receive period:

1. The selector acts only while "Enable Tx" is on and the last transmission was a CQ (`m_bCallingCQ`). The selector must be visible, so "Auto Seq" must be on.
2. A line counts if it contains the own call, does not contain "73 ", and no Blacklist or Whitelist filter dropped it. The line does not need to be on the Rx or Tx offset.
3. The signal-to-noise ratio of the line is compared with the highest value so far. If it is higher, WSJT-X answers this caller at once. It sets the DX call, generates the messages, and selects the report.
4. A later line of the same receive period can replace the caller, also from a later decode pass, if its ratio is higher. The last replacement before the next transmission wins.
5. The highest value starts at -28 dB. WSJT-X resets it when "Enable Tx" goes off, at Halt Tx, at Stop, and when the DX call is cleared. It does not reset it at the start of each period.

Thus WSJT-X answers the caller with the highest signal-to-noise ratio that it decoded before the next transmission. A caller at -28 dB or lower is never answered. If the highest value is not reset between two CQ runs, a caller must be stronger than the strongest caller of the earlier run. Start CQ turns "Enable Tx" on again, but that does not reset the value when "Enable Tx" is already on.

# UDP extension messages

This fork of WSJT-X adds four messages to the WSJT-X UDP message protocol. An automation client, for example 75auto, uses them to start a CQ, to turn transmit on and off, and to set the Tx offset. Stock WSJT-X has no UDP message for these actions.

This document is for the writers of clients. `Network/NetworkMessage.hpp` is the reference for the wire format. `Network/udp-extensions-plan.md` holds the design notes and the findings from the source code.

## Overview

| Type | Name | Direction | Fields after the Id | Extension name |
|---|---|---|---|---|
| 50 | Extensions | Out | Version (quint32), Names (utf8) | none |
| 51 | Enable Tx | In | Enable (bool) | `tx-enable` |
| 52 | Start CQ | In | Modifier (utf8) | `cq` |
| 53 | Set Tx Offset | In | Offset (quint32, Hz) | `set-tx-df` |

"Out" means that WSJT-X sends the message. "In" means that WSJT-X receives the message from a client.

The extension contract has the version 1. The four type numbers are final.

## Message numbers

The stock protocol uses the types 0 to 16. The extensions use fixed numbers from 50. If upstream WSJT-X adds new types, the new types get the next free number after 16. Thus they cannot collide with the extensions.

WSJT-X accepts the stock types and the types 50 to 53. It treats every other type as unrecognized and ignores it. This is the same as in stock WSJT-X.

## Wire format

The extension messages use the same framing as the stock messages. Each message starts with this header:

1. The magic number `0xadbccbda`, as a quint32.
2. The schema number, as a quint32. Use schema 3.
3. The message type, as a quint32.
4. The Id of the WSJT-X instance, as utf8.

All numbers are big endian. A utf8 field is a quint32 length, followed by that number of bytes. The field has no terminator. A bool field is one byte, `0x00` for false and `0x01` for true. Every field type of the extensions is already in use in the stock protocol.

WSJT-X does not compare the Id of an incoming message with its own Id. The stock messages behave the same way. Use the Id from the Heartbeat of the instance that you control.

If an incoming message is shorter than its fields, WSJT-X ignores the message.

## The "Accept UDP requests" option

The operator sets "Accept UDP requests" in Settings, on the Reporting tab. The default is off.

While the option is off, WSJT-X ignores all incoming messages, the extensions included. It also does not send the Extensions message. A patched WSJT-X with the option off thus sends the same datagrams as stock WSJT-X.

WSJT-X does not report the state of this option. If a client sends a message and nothing changes, examine this option first.

## Extensions (50, out)

WSJT-X sends this message directly after each Heartbeat. A Heartbeat goes out every 15 seconds and when the network connection starts.

| Field | Type | Value |
|---|---|---|
| Version | quint32 | The version of the extension contract, now 1. |
| Names | utf8 | A comma-separated list of the supported extensions, now `tx-enable,cq,set-tx-df`. |

A client uses this message to find out whether the WSJT-X instance is patched. Stock WSJT-X never sends type 50. If a client receives a Heartbeat but no Extensions message, the instance is stock WSJT-X, or "Accept UDP requests" is off.

A client must ignore a name that it does not know. A later version of the patch can add names.

## Enable Tx (51, in)

| Field | Type | Value |
|---|---|---|
| Enable | bool | The new state of the "Enable Tx" button. |

WSJT-X sets the "Enable Tx" button to the value of Enable. The effect is the same as a click on the button by the operator. If the button already has that state, nothing changes.

The next Status message shows the new state in its "Tx Enabled" field.

## Start CQ (52, in)

| Field | Type | Value |
|---|---|---|
| Modifier | utf8 | The word of a directed CQ, for example `DX` or `EU`. Empty for a plain CQ. |

WSJT-X removes the spaces at the start and end of the modifier and changes it to upper case. The modifier must then follow the rule of the Tx6 field: 1 to 4 letters, or 1 to 3 digits. If the modifier is not valid, WSJT-X ignores the message.

If the modifier is valid, WSJT-X does these steps:

1. It sets the CQ word, for example `CQ DX`, or `CQ` for an empty modifier.
2. It generates the standard messages.
3. It selects the CQ message (Tx6), as a click on the Tx6 button does.
4. It turns "Enable Tx" on. It also does this step when "Enable Tx" is already on.

A transmission in progress continues to its end. WSJT-X does not restart it with the CQ. The CQ goes out in the next transmit period of the period parity that the operator selected.

Start CQ does not change the Tx offset. It does not reset the Tx watchdog.

In the special operating modes Fox and Hound, WSJT-X ignores Start CQ. A Fox uses its own sequencer, and a Hound does not call CQ.

The "CQ: Max dB" selector and the other CQ selectors answer a caller only while "Enable Tx" is on and the last transmission was a CQ. `Network/udp-extensions-plan.md` describes how "CQ: Max dB" selects a caller.

## Set Tx Offset (53, in)

| Field | Type | Value |
|---|---|---|
| Offset | quint32 | The new Tx audio offset in Hz. |

If the offset is less than 200 Hz or more than 3000 Hz, WSJT-X ignores the message. The Tx offset then does not change.

If the offset is in this range, WSJT-X sets the Tx frequency control to the offset. The effect is the same as a change of the control by the operator. "Hold Tx Freq" does not change. The message does not reset the Tx watchdog.

If the offset changed, WSJT-X sends a Status message with the new "Tx DF" at once. If the new offset is equal to the current offset, WSJT-X sends no new Status. Thus, before a client waits for a Status, it must compare the requested offset with the "Tx DF" of the last Status.

In FT8 and FT4, a transmission in progress keeps the offset that it started with. The new offset applies to the next transmission. FT8 with the x2 or x4 tone spacing option, and the older modes, change the frequency during the transmission.

In two cases, WSJT-X forces a different offset. The Status then shows the forced value, not the requested value:

1. In SuperFox Fox mode, WSJT-X sets the offset to 750 Hz.
2. On 60 m, if the own call sign starts with G, M, or 2, WSJT-X lowers the offset so that the signal stays below 5358 kHz. WSJT-X also shows a message box.

## Example datagrams

These examples use the Id `WSJT-X` and schema 3. The bytes are in hexadecimal.

Enable Tx with Enable set to true:

```text
ad bc cb da  00 00 00 03  00 00 00 33
00 00 00 06  57 53 4a 54 2d 58
01
```

Start CQ with the modifier `DX`:

```text
ad bc cb da  00 00 00 03  00 00 00 34
00 00 00 06  57 53 4a 54 2d 58
00 00 00 02  44 58
```

Set Tx Offset with the offset 1500 Hz:

```text
ad bc cb da  00 00 00 03  00 00 00 35
00 00 00 06  57 53 4a 54 2d 58
00 00 05 dc
```

## Tests

`tests/test_message_client_extensions.cpp` is a Qt test. It talks to a `MessageClient` over the loop-back interface. It makes sure that the type numbers are correct, that the announcement follows the Heartbeat, and that the client ignores the extensions while "Accept UDP requests" is off.

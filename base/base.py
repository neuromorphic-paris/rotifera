"""
base implements methods to send messages tothe buggy.

Encoded message structure:
    0x00 | encoded bytes | 0xff
    The encoded bytes are identical to the original bytes except for the following conversions:
        0x00 -> 0xaa 0xab
        0xaa -> 0xaa 0xac
        0xff -> 0xaa 0xad
    The following special messages are captured by the arbiter and not transmitted to the on-board algorithm:
        0x00 0xaa 0xae 0xff (switch to base control)
        0x00 0xaa 0xaf 0xff (switch to radio control)
        0x00 0xaa 0xba 0xff (request a telemetry dump)
"""
import serial
import threading

radioSerial = serial.Serial('/dev/tty.usbserial-FT1LV4D3', baudrate = 38400)

messageListeners = []
messageListenersLock = threading.Lock()
def listeningWorker():
    message = bytearray()
    readingMessage = False
    escapedCharacter = False
    while True:
        byte = bytearray(radioSerial.read())[0]
        if readingMessage:
            if byte == 0x00:
                message = bytearray()
                escapedCharacter = False
            elif byte == 0xaa:
                escapedCharacter = True
            elif byte == 0xff:
                readingMessage = False
                if not escapedCharacter:
                    messageListenersLock.acquire()
                    for messageListener in messageListeners:
                        messageListener(message)
                    messageListenersLock.release()
            else:
                if escapedCharacter:
                    escapedCharacter = False
                    if byte == 0xab:
                        message.append(0x00)
                    elif byte == 0xac:
                        message.append(0xaa)
                    elif byte == 0xad:
                        message.append(0xff)
                    else:
                        readingMessage = False
                else:
                    message.append(byte)
        else:
            if byte == 0x00:
                message = bytearray()
                readingMessage = True
                escapedCharacter = False

listeningThread = threading.Thread(target = listeningWorker)
listeningThread.daemon = True
listeningThread.start()

def addMessageListener(messageListener):
    """
    addMessageListener registers a message delegate.
    Delegates are called on a dedicated thread.

    Arguments:
        messageListener (function(bytearray)): a delegate function which will be given messages.
    """
    messageListenersLock.acquire()
    messageListeners.append(messageListener)
    messageListenersLock.release()

def sendRawBytes(bytes):
    """
    sendRawBytes writes bytes to the TTY device.

    Arguments:
        bytes (bytearray): the bytes to send.
    """
    for byte in bytes:
        radioSerial.write(bytearray((byte,)))
    radioSerial.flush()

def switchToBaseControl():
    """
    switchToBaseControl tells the arbiter to drive the buggy with base messages.
    """
    sendRawBytes(bytearray((0x00, 0xaa, 0xae, 0xff)))

def switchToRadioControl():
    """
    switchToBaseControl tells the arbiter to drive the buggy with the radio controller.
    """
    sendRawBytes(bytearray((0x00, 0xaa, 0xaf, 0xff)))

def requestTelemetryDump():
    """
    requestTelemetryDump prompts the buggy for its telemtry data.
    """
    sendRawBytes(bytearray((0x00, 0xaa, 0xba, 0xff)))

def sendBytes(bytes):
    """
    sendBytes transmits bytes from the base to the buggy.

    Arguments:
        bytes (bytearray): the bytes to send.
    """
    encodedBytes = bytearray([0x00])
    for byte in bytes:
        if byte == 0x00:
            encodedBytes.extend((0xaa, 0xab))
        elif byte == 0xaa:
            encodedBytes.extend((0xaa, 0xac))
        elif byte == 0xff:
            encodedBytes.extend((0xaa, 0xad))
        else:
            encodedBytes.append(byte)
    encodedBytes.append(0xff)
    sendRawBytes(encodedBytes)

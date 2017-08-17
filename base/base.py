"""
base implements methods to send messages tothe buggy.

Encoded message structure:
    0x00 | original bytes | CRC on original bytes (1 byte) | 0xff
    The following conversions must be applied on the original and CRC bytes:
        0x00 -> 0xaa 0xab
        0xaa -> 0xaa 0xac
        0xff -> 0xaa 0xad
    The following special messages are captured by the arbiter and not transmitted to the on-board algorithm:
        0x00 0xaa 0xae 0xfe 0xff (switch to base control)
        0x00 0xaa 0xaf 0xb7 0xff (switch to radio control)
        0x00 0xaa 0xba 0x6e 0xff (request a telemetry dump)
"""
import serial
import threading

crcTable = bytearray(256)
for index in range(0, len(crcTable)):
    byte = index
    for bit in range(0, 8):
        if (byte & 0x80) != 0:
            byte = (byte << 1) ^ 0x49
        else:
            byte <<= 1
    byte &= 0xff
    crcTable[index] = byte

radioSerial = serial.Serial('/dev/tty.usbserial-FT1LV4D3', baudrate = 57600)

messageListeners = []
messageListenersLock = threading.Lock()
def listeningWorker():
    message = bytearray()
    crc = 0;
    readingMessage = False
    escapedCharacter = False
    while True:
        byte = bytearray(radioSerial.read())[0]
        if readingMessage:
            if byte == 0x00:
                message = bytearray()
                crc = 0
                escapedCharacter = False
            elif byte == 0xaa:
                escapedCharacter = True
            elif byte == 0xff:
                readingMessage = False
                if not escapedCharacter and crc == 0x53 and len(message) > 1:
                    del message[-1]
                    messageListenersLock.acquire()
                    for messageListener in messageListeners:
                        messageListener(message)
                    messageListenersLock.release()
            else:
                if escapedCharacter:
                    escapedCharacter = False
                    if byte == 0xab:
                        crc = crcTable[(0x00 ^ crc) & 0xff]
                        message.append(0x00)
                    elif byte == 0xac:
                        crc = crcTable[(0xaa ^ crc) & 0xff]
                        message.append(0xaa)
                    elif byte == 0xad:
                        crc = crcTable[(0xff ^ crc) & 0xff]
                        message.append(0xff)
                    else:
                        readingMessage = False
                else:
                    crc = crcTable[(byte ^ crc) & 0xff]
                    message.append(byte)
        else:
            if byte == 0x00:
                message = bytearray()
                crc = 0
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
    radioSerial.write(bytes)
    radioSerial.flush()

def calculateCrc(bytes):
    """
    calculateCrc computes the CRC-8/GSM-B value for the given bytes.

    Arguments:
        bytes (bytearray): the bytes to calculate the CRC for.
    """
    crc = 0
    for byte in bytes:
        crc = crcTable[(byte ^ crc) & 0xff]
    return crc ^ 0xff

def switchToBaseControl():
    """
    switchToBaseControl tells the arbiter to drive the buggy with base messages.
    """
    sendRawBytes(bytearray((0x00, 0xaa, 0xae, 0xfe, 0xff)))

def switchToRadioControl():
    """
    switchToBaseControl tells the arbiter to drive the buggy with the radio controller.
    """
    sendRawBytes(bytearray((0x00, 0xaa, 0xaf, 0xb7, 0xff)))

def requestTelemetryDump():
    """
    requestTelemetryDump prompts the buggy for its telemtry data.
    """
    sendRawBytes(bytearray((0x00, 0xaa, 0xba, 0x6e, 0xff)))

def sendBytes(bytes):
    """
    sendBytes transmits bytes from the base to the buggy.

    Arguments:
        bytes (bytearray): the bytes to send.
    """
    if len(bytes) > 4096:
        raise AssertionError('the message cannot contain more than 4096 bytes')
    encodedBytes = bytearray([0x00])
    for byte in bytes + bytearray((calculateCrc(bytes),)):
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

if __name__ == '__main__':
    import random
    for index in range(0, 10):
        bytes = bytearray([random.randint(0, 255) for byte in range(0, random.randint(1, 20))])
        bytes += bytearray((calculateCrc(bytes),))
        crc = calculateCrc(bytes)
        if crc ^ 0xff == 0x53:
            print(str(list(bytes)) + ' -> ok')
        else:
            print(str(list(bytes)) + ' -> error with crc ' + str(crc))

    for bytes in (bytearray((0xaa, 0xae)), bytearray((0xaa, 0xaf)), bytearray((0xaa, 0xba))):
        bytes += bytearray((calculateCrc(bytes),))
        crc = calculateCrc(bytes)
        if crc ^ 0xff == 0x53:
            print(str(list(bytes)) + ' -> ok')
        else:
            print(str(list(bytes)) + ' -> error with crc ' + str(crc))

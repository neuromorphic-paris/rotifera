"""
buggy provides tools to customize the buggy's behavior.
"""
import socket
import threading

inputSocket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
inputSocket.connect('/var/run/rotifera/arbiter.sock')
outputFifo = open('/var/run/rotifera/arbiter.fifo', 'wb')

messageListeners = []
messageListenersLock = threading.Lock()
def listeningWorker():
    bytes = bytearray(4096)
    while True:
        bytesRead = inputSocket.recv_into(bytes, len(bytes))
        if bytesRead > 0:
            message = bytes[:bytesRead]
            messageListenersLock.acquire()
            for messageListener in messageListeners:
                messageListener(message)
            messageListenersLock.release()

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

def setDirection(direction):
    """
    setDirection changes the buggy's wheels direction.

    Arguments:
        direction (integer): the buggy direction, must be in the range [-500, 500].
    """
    if not isinstance(direction, (int, long)):
        raise AssertionError('direction must be an integer')
    if direction < -500 or direction > 500:
        raise AssertionError('direction must be in the range [-500, 500]')
    correctedDirection = direction + 1500
    outputFifo.write(bytearray((0, correctedDirection & 0xff, (correctedDirection >> 8) & 0xff)))
    outputFifo.flush()

def setSpeed(speed):
    """
    setSpeed changes the buggy's wheels speed.

    Arguments:
        speed (integer): the buggy speed, must be in the range [-500, 500].
    """
    if not isinstance(speed, (int, long)):
        raise AssertionError('speed must be an integer')
    if speed < -500 or speed > 500:
        raise AssertionError('speed must be in the range [-500, 500]')
    correctedSpeed = speed + 1500
    outputFifo.write(bytearray((1, correctedSpeed & 0xff, (correctedSpeed >> 8) & 0xff)))
    outputFifo.flush()

def setPan(pan):
    """
    setPan changes the camera's pan angle.

    Arguments:
        pan (integer): the camera's pan angle, must be in the range [-500, 500].
    """
    if not isinstance(pan, (int, long)):
        raise AssertionError('pan must be an integer')
    if pan < -500 or pan > 500:
        raise AssertionError('pan must be in the range [-500, 500]')
    correctedPan = pan + 1500
    outputFifo.write(bytearray((2, correctedPan & 0xff, (correctedPan >> 8) & 0xff)))
    outputFifo.flush()

def setTilt(tilt):
    """
    setTilt changes the camera's tilt angle.

    Arguments:
        tilt (integer): the camera's tilt angle, must be in the range [-500, 500].
    """
    if not isinstance(tilt, (int, long)):
        raise AssertionError('tilt must be an integer')
    if tilt < -500 or tilt > 500:
        raise AssertionError('tilt must be in the range [-500, 500]')
    correctedTilt = tilt + 1500
    outputFifo.write(bytearray((2, correctedTilt & 0xff, (correctedTilt >> 8) & 0xff)))
    outputFifo.flush()
